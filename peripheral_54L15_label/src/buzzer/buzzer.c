#include "buzzer.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

static struct gpio_dt_spec buzzer_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_io), gpios);
static struct k_work_delayable buzzer_work_delayable;

static void buzzer_work_off_handler(struct k_work* item)
{
    gpio_pin_configure_dt(&buzzer_gpio, GPIO_OUTPUT_ACTIVE);
    printf("Buzzer off\n");
}

void buzzer_init(void)
{
    if (!device_is_ready(buzzer_gpio.port)) {
        printk("Buzzer GPIO device not ready\n");
        return;
    }

    k_work_init_delayable(&buzzer_work_delayable, buzzer_work_off_handler);

    gpio_pin_configure_dt(&buzzer_gpio, GPIO_OUTPUT_ACTIVE);
}

void buzzer_on(uint32_t duration_ms)
{
    printf("Buzzer on for %u ms\n", duration_ms);
    gpio_pin_configure_dt(&buzzer_gpio, GPIO_OUTPUT_INACTIVE);
    k_work_reschedule(&buzzer_work_delayable, K_MSEC(duration_ms));
}