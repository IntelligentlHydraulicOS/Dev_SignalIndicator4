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
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include "fuel_gauge.h"
#include <zephyr/drivers/uart.h>
#include "imu_driver/bma253_example.h"
#include "buzzer/buzzer.h"
#include "power_mgr/power_mgr.h"


#if DT_NODE_EXISTS(DT_NODELABEL(npm1300_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_##dev))
#elif DT_NODE_EXISTS(DT_NODELABEL(npm1304_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1304_ek_##dev))
#else
#error "neither npm1300 nor npm1304 found in devicetree"
#endif

static const struct device *pmic = NPM13XX_DEVICE(pmic);
static const struct device *charger = NPM13XX_DEVICE(charger);
static volatile bool vbus_connected;

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

	bms_init();
	nrfConfigInit();
	LedLightInit();

	int err = nfc_config_tag_init();
	if (err < 0) {
		printk("NFC tag initialization failed: %d\n", err);
	}

	// RS485_deal_init();
	// Zigbee_deal_init();

	/* 低功耗状态机：管理 BUCK2(UWB) 上下电与 ship mode。
	 * 注意：UWB 不再在这里常开，改由运动检测触发上电。 */
	power_mgr_init();

	bma253_example_init();

	buzzer_init();

	for (;;) {
		if (k_uptime_get() - last_print_time > 5000) {
			last_print_time = k_uptime_get();
			fuel_gauge_update(charger, vbus_connected);
			bma253_example_poll();
		}
		Function_of_indicator_lights();

		/* 低功耗状态机：只判断 30s 深度休眠（进 ship mode）。
		 * 10s 活动窗口由 power_mgr 内部的 k_timer 精确计时，
		 * 不依赖这个循环的调用频率。 */
		power_mgr_sleep_check();

		/* 只在「运动触发的 10s 活动窗口」内才发送 UWB 轮询。
		 * 窗口外 UWB 已进入低功耗，调用它没有意义。 */
		if (power_mgr_uwb_is_active()) {
			dw1000_sendpoll();
		}

		k_msleep(100);
	}
}
