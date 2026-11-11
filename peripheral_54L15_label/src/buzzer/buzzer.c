#include <errno.h>

#include "buzzer.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

static struct gpio_dt_spec buzzer_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_io), gpios);
static struct k_work_delayable buzzer_work_delayable;
static K_MUTEX_DEFINE(buzzer_request_mutex);
static K_MUTEX_DEFINE(buzzer_mutex);
static uint32_t beep_on_ms;
static uint32_t beep_interval_ms;
static uint16_t beeps_remaining;
static bool buzzer_initialized;
static bool buzzer_is_on;

static int buzzer_set(bool on)
{
	/* This board's buzzer is active-low: low turns it on, high turns it off. */
	int ret = gpio_pin_set_raw(buzzer_gpio.port, buzzer_gpio.pin, on ? 0 : 1);
	if (ret == 0) {
		buzzer_is_on = on;
	}

	return ret;
}

static void buzzer_work_off_handler(struct k_work* item)
{
	ARG_UNUSED(item);
	k_mutex_lock(&buzzer_mutex, K_FOREVER);

	int ret;
	if (buzzer_is_on) {
		ret = buzzer_set(false);
		if (ret != 0) {
			printk("Buzzer: failed to turn off: %d\n", ret);
			beeps_remaining = 0;
			k_mutex_unlock(&buzzer_mutex);
			return;
		}

		if (beeps_remaining > 0) {
			beeps_remaining--;
		}
		if (beeps_remaining == 0) {
			k_mutex_unlock(&buzzer_mutex);
			return;
		}

		ret = k_work_reschedule(&buzzer_work_delayable,
					K_MSEC(beep_interval_ms));
	} else {
		ret = buzzer_set(true);
		if (ret == 0) {
			ret = k_work_reschedule(&buzzer_work_delayable,
						K_MSEC(beep_on_ms));
		}
	}

	if (ret < 0) {
		printk("Buzzer: failed to schedule next transition: %d\n", ret);
		(void)buzzer_set(false);
		beeps_remaining = 0;
	}

	k_mutex_unlock(&buzzer_mutex);
}

void buzzer_init(void)
{
	if (!device_is_ready(buzzer_gpio.port)) {
		printk("Buzzer GPIO device not ready\n");
		return;
	}

	k_mutex_lock(&buzzer_mutex, K_FOREVER);
	if (buzzer_initialized) {
		k_mutex_unlock(&buzzer_mutex);
		return;
	}

	int ret = gpio_pin_configure_dt(&buzzer_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		printk("Buzzer: GPIO configuration failed: %d\n", ret);
		k_mutex_unlock(&buzzer_mutex);
		return;
	}

	k_work_init_delayable(&buzzer_work_delayable, buzzer_work_off_handler);
	buzzer_initialized = true;
	buzzer_is_on = false;
	k_mutex_unlock(&buzzer_mutex);
}

int buzzer_beep(uint32_t on_ms, uint32_t interval_ms, uint16_t count)
{
	if (on_ms == 0 || count == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&buzzer_request_mutex, K_FOREVER);
	k_mutex_lock(&buzzer_mutex, K_FOREVER);
	if (!buzzer_initialized) {
		k_mutex_unlock(&buzzer_mutex);
		k_mutex_unlock(&buzzer_request_mutex);
		return -ENODEV;
	}
	k_mutex_unlock(&buzzer_mutex);

	struct k_work_sync sync;
	(void)k_work_cancel_delayable_sync(&buzzer_work_delayable, &sync);

	k_mutex_lock(&buzzer_mutex, K_FOREVER);
	beep_on_ms = on_ms;
	beep_interval_ms = interval_ms;
	beeps_remaining = count;
	int ret = buzzer_set(true);
	if (ret == 0) {
		ret = k_work_reschedule(&buzzer_work_delayable, K_MSEC(on_ms));
	}
	if (ret < 0) {
		(void)buzzer_set(false);
		beeps_remaining = 0;
	}
	k_mutex_unlock(&buzzer_mutex);
	k_mutex_unlock(&buzzer_request_mutex);

	return ret < 0 ? ret : 0;
}

void buzzer_on(uint32_t duration_ms)
{
	int ret = buzzer_beep(duration_ms, 0, 1);
	if (ret != 0) {
		printk("Buzzer: single beep request failed: %d\n", ret);
	}
}