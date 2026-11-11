/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample
 */


#include <zephyr/types.h>
#include <soc.h>

#include <stdio.h>
#include <string.h>

#include "dw3000_node.h"
#include "ledlight.h"
#include "nfc_config_tag.h"

#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/watchdog.h>
#include "fuel_gauge.h"
#include <zephyr/drivers/uart.h>
#include "imu_driver/bma253_example.h"
#include "buzzer/buzzer.h"
#include "power_mgr/power_mgr.h"

#define MAIN_LOOP_SLEEP_BACKOFF_NUM      12U
#define MAIN_LOOP_SLEEP_BACKOFF_DEN      10U
#define UWB_NO_RESPONSE_GRACE_MS         3000U
#define WATCHDOG_TIMEOUT_MS              25000U


#if DT_NODE_EXISTS(DT_NODELABEL(npm1300_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_##dev))
#elif DT_NODE_EXISTS(DT_NODELABEL(npm1304_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1304_ek_##dev))
#else
#error "neither npm1300 nor npm1304 found in devicetree"
#endif

static const struct device *pmic = NPM13XX_DEVICE(pmic);
static const struct device *charger = NPM13XX_DEVICE(charger);
static const struct device *const watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static volatile bool vbus_connected;

static void business_idle_callback(void)
{
	/*
	 * System-ON-IDLE 前的业务处理放在这里。
	 * 此回调运行在 idle 上下文且中断已锁定，不能阻塞或睡眠。
	 */
}

static int watchdog_init(void)
{
	static const struct wdt_timeout_cfg config = {
		.window = {
			.min = 0,
			.max = WATCHDOG_TIMEOUT_MS,
		},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int channel_id;
	int err;

	if (!device_is_ready(watchdog)) {
		printk("Watchdog device is not ready\n");
		return -ENODEV;
	}

	channel_id = wdt_install_timeout(watchdog, &config);
	if (channel_id < 0) {
		printk("Failed to install watchdog timeout: %d\n", channel_id);
		return channel_id;
	}

	err = wdt_setup(watchdog, 0);
	if (err) {
		printk("Failed to start watchdog: %d\n", err);
		return err;
	}

	printk("Watchdog started with a %u ms timeout\n", WATCHDOG_TIMEOUT_MS);
	return channel_id;
}

static void event_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (pins & BIT(NPM13XX_EVENT_VBUS_DETECTED)) {
		printk("Vbus connected\n");
		vbus_connected = true;
	}

	if (pins & BIT(NPM13XX_EVENT_VBUS_REMOVED)) {
		printk("Vbus removed\n");
		vbus_connected = false;
	}
}

int bms_init(void)
{
	int err;

	if (!device_is_ready(pmic)) {
		printk("Pmic device not ready.\n");
		return 0;
	}

	if (!device_is_ready(charger)) {
		printk("Charger device not ready.\n");
		return 0;
	}

	if (fuel_gauge_init(charger) < 0) {
		printk("Could not initialise fuel gauge.\n");
		return 0;
	}

	static struct gpio_callback event_cb;

	gpio_init_callback(&event_cb, event_callback,
			   BIT(NPM13XX_EVENT_VBUS_DETECTED) | BIT(NPM13XX_EVENT_VBUS_REMOVED));

	err = mfd_npm13xx_add_callback(pmic, &event_cb);
	if (err) {
		printk("Failed to add pmic callback.\n");
		return 0;
	}

	/* Initialise vbus detection status. */
	struct sensor_value val;
	int ret = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &val);

	if (ret < 0) {
		return false;
	}

	vbus_connected = (val.val1 != 0) || (val.val2 != 0);

	/* 【诊断】直接读"期望充电电流"属性：驱动返回的是 CHGR 的
	 * EN_SET 寄存器（CHGR_BASE+0x04）。它 != 0 才说明充电被使能。
	 * 若此处读到 0，就是 charging-enable 没生效。 */
	{
		struct sensor_value chg_en;

		if (sensor_attr_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT,
				    SENSOR_ATTR_CONFIGURATION, &chg_en) == 0) {
			printk("充电使能位 CHGR EN_SET = 0x%02X (%s)\n",
			       (unsigned)chg_en.val1,
			       (chg_en.val1 != 0) ? "已使能" : ">>> 未使能 <<<");
		}
	}

	printk("PMIC device ok (VBUS %s)\n", vbus_connected ? "已接入" : "未接入");
#if 0
	while (1) {
		fuel_gauge_update(charger, vbus_connected);
		k_msleep(100);
	}
#endif
	return 0;
}


int main(void)
{
	printf("Starting signal convert v4 app...\n");

	uint64_t last_print_time = 0;
	uint64_t last_buzzer_time = 0;
	uint64_t curr_ts = 0;
	uwb_settings_t uwb_settings;
	uint32_t main_loop_sleep_ms;
	uint64_t uwb_power_on_time_ms = 0U;
	bool uwb_active = false;
	bool nfc_initialized = false;
	int watchdog_channel;
	int err;

	watchdog_channel = watchdog_init();
	if (watchdog_channel < 0) {
		return watchdog_channel;
	}

	bms_init();
	nrfConfigInit();
	err = uwb_settings_get(&uwb_settings);
	if (err != 0) {
		printk("Failed to load UWB settings: %d\n", err);
		return err;
	}
	main_loop_sleep_ms = uwb_settings.min_interval_ms;
	LedLightInit();

	err = nfc_config_tag_init();
	if (err < 0) {
		printk("NFC tag initialization failed: %d\n", err);
	} else {
		nfc_initialized = true;
	}

	// RS485_deal_init();
	// Zigbee_deal_init();

	/* 低功耗状态机：管理 BUCK2(UWB) 上下电与 ship mode。
	 * 注意：UWB 不再在这里常开，改由运动检测触发上电。 */
	power_mgr_init();
	if (power_mgr_register_idle_callback(business_idle_callback) != 0) {
		printk("Failed to register System-ON-IDLE callback\n");
		return -EALREADY;
	}

	bma253_example_init();

	buzzer_init();

	buzzer_on(100);

	for (;;) {
		err = uwb_settings_get(&uwb_settings);
		if (err != 0) {
			printk("Failed to read UWB settings: %d\n", err);
			return err;
		}
		if (main_loop_sleep_ms < uwb_settings.min_interval_ms) {
			main_loop_sleep_ms = uwb_settings.min_interval_ms;
		} else if (main_loop_sleep_ms > uwb_settings.max_interval_ms) {
			main_loop_sleep_ms = uwb_settings.max_interval_ms;
		}

		curr_ts = k_uptime_get();
		if (curr_ts - last_print_time > 5000 || last_print_time == 0) {
			last_print_time = curr_ts;
			fuel_gauge_update(charger, vbus_connected);
			//该功能暂时注释掉，因为会导致在主循环中频繁触发蜂鸣器，影响正常使用
			fuel_gauge_data_t fuel_gauge_data = {0};
			fuel_gauge_data_get(&fuel_gauge_data);
			if(fuel_gauge_data.soc < 10.0f && curr_ts - last_buzzer_time > 60000 && fuel_gauge_data.chg_status == 0) {
				buzzer_beep(50, 150, 2);
				printk("Warning: Battery SoC is below 10%%, please charge the battery soon!\n");
				last_buzzer_time = curr_ts;
			}
			if (nfc_initialized) {
				err = nfc_config_tag_refresh_info();
				if (err != 0) {
					printk("Failed to refresh NFC status: %d\n", err);
				}
			}
			bma253_example_poll();
			printf("main loop: uptime=%llu ms, motion_count=%u, "
			       "main_loop_sleep_ms=%u\n",
			       curr_ts, (unsigned)power_mgr_get_motion_count(), main_loop_sleep_ms);
		}

		/* 低功耗状态机：只判断 30s 深度休眠（进 ship mode）。
		 * 10s 活动窗口由 power_mgr 内部的 k_timer 精确计时，
		 * 不依赖这个循环的调用频率。 */
		power_mgr_sleep_check();

		/* 只在「运动触发的 10s 活动窗口」内才发送 UWB 轮询。
		 * 窗口外 UWB 已进入低功耗，调用它没有意义。
		 *
		 * 规则：
		 *   1. 从 UWB 上电成功的时刻开始计时；
		 *   2. 前 3s 内未收到应答时，不启动退避；
		 *   3. 超过 3s 仍未收到应答时，才按指数/线性退避增加 sleep；
		 *   4. 任何有效应答都立即恢复到最短间隔，并重置 3s 窗口。
		 */
		bool uwb_is_active = power_mgr_uwb_is_active();
		if (uwb_is_active && !uwb_active) {
			uwb_active = true;
			uwb_power_on_time_ms = curr_ts;
			main_loop_sleep_ms = uwb_settings.min_interval_ms;
		}
		if (!uwb_is_active) {
			uwb_active = false;
			uwb_power_on_time_ms = 0U;
			main_loop_sleep_ms = uwb_settings.max_interval_ms;
			Indicator_lights_off();
		} else {
			Function_of_indicator_lights();
			dw1000_sendpoll(curr_ts);
			if (dw1000_take_poll_response()) {
				uwb_power_on_time_ms = curr_ts;
				main_loop_sleep_ms = uwb_settings.min_interval_ms;
			} else {
				uint32_t elapsed_since_uwb_on_ms = (uint32_t)(curr_ts - uwb_power_on_time_ms);

				if (elapsed_since_uwb_on_ms >= UWB_NO_RESPONSE_GRACE_MS) {
					if (main_loop_sleep_ms < uwb_settings.max_interval_ms) {
						uint32_t next_sleep_ms =
							(main_loop_sleep_ms * MAIN_LOOP_SLEEP_BACKOFF_NUM) /
							MAIN_LOOP_SLEEP_BACKOFF_DEN;
						if (next_sleep_ms < main_loop_sleep_ms) {
							next_sleep_ms = main_loop_sleep_ms;
						}
						if (next_sleep_ms > uwb_settings.max_interval_ms) {
							next_sleep_ms = uwb_settings.max_interval_ms;
						}
						main_loop_sleep_ms = next_sleep_ms;
					}
				} else {
					main_loop_sleep_ms = uwb_settings.min_interval_ms;
				}
			}
		}

		if (wdt_feed(watchdog, watchdog_channel) != 0) {
			printk("Watchdog feed failed\n");
			return -EIO;
		}

		(void)power_mgr_wait_for_motion(main_loop_sleep_ms);
	}
}
