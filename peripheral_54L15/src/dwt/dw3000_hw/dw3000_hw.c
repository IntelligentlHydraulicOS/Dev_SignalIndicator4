#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "deca_device_api.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"

#define DW_INST DT_INST(0, decawave_dw3000)

static struct gpio_callback gpio_cb;
static struct k_work dw3000_isr_work;
static bool dw3000_irs_flag = false;

struct dw3000_config {
	struct gpio_dt_spec gpio_irq;
	struct gpio_dt_spec gpio_sync;
	struct gpio_dt_spec gpio_reset;
	struct gpio_dt_spec gpio_wakeup;
	struct gpio_dt_spec gpio_spi_pol;
	struct gpio_dt_spec gpio_spi_pha;
};

static const struct dw3000_config conf = {
	.gpio_irq = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_irq), gpios),
	.gpio_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_rst), gpios),
	.gpio_wakeup = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_wakeup), gpios),
	.gpio_sync = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_sync), gpios),
/*	.gpio_spi_pol = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_cs), gpios),
	.gpio_spi_pha = GPIO_DT_SPEC_GET(DT_NODELABEL(dw3000_cs), gpios),*/
};

int dw3000_hw_init(void)
{
	/* Reset */
	if (conf.gpio_reset.port) {
		gpio_pin_configure_dt(&conf.gpio_reset, GPIO_OUTPUT_ACTIVE);
		printk("[%s %d] RESET on %s pin %d\n", __func__, __LINE__, conf.gpio_reset.port->name,
				conf.gpio_reset.pin);
	}

	/* Wakeup (optional) */
	if (conf.gpio_wakeup.port) {
		gpio_pin_configure_dt(&conf.gpio_wakeup, GPIO_OUTPUT_ACTIVE);
		printk("[%s %d] WAKEUP on %s pin %d\n", __func__, __LINE__, conf.gpio_wakeup.port->name,
				conf.gpio_wakeup.pin);
	}

	if (conf.gpio_sync.port) {
		gpio_pin_configure_dt(&conf.gpio_sync, GPIO_OUTPUT_ACTIVE);
		printk("[%s %d] sync on %s pin %d\n", __func__, __LINE__, conf.gpio_wakeup.port->name,
				conf.gpio_wakeup.pin);
	}

	return dw3000_spi_init();
}

static void dw3000_hw_isr_work_handler(struct k_work* item)
{
	while (gpio_pin_get_dt(&conf.gpio_irq)) {
		dwt_isr();
	}
}

static void dw3000_hw_isr(const struct device* dev, struct gpio_callback* cb,
						  uint32_t pins)
{
	k_work_submit(&dw3000_isr_work);
}

int dw3000_hw_init_interrupt(void)
{
	if (conf.gpio_irq.port) {
		k_work_init(&dw3000_isr_work, dw3000_hw_isr_work_handler);

		gpio_pin_configure_dt(&conf.gpio_irq, GPIO_INPUT);
		gpio_init_callback(&gpio_cb, dw3000_hw_isr, BIT(conf.gpio_irq.pin));
		gpio_add_callback(conf.gpio_irq.port, &gpio_cb);
		// gpio_pin_interrupt_configure_dt(&conf.gpio_irq, GPIO_INT_EDGE_BOTH/*GPIO_INT_EDGE_RISING*/);
		dw3000_irs_flag = false;
		printk("[%s %d] IRQ on %s pin %d\n", __func__, __LINE__, conf.gpio_irq.port->name,
				conf.gpio_irq.pin);
	} else {
		printk("[%s %d] IRQ pin not configured\n", __func__, __LINE__);
		return -ENOENT;
	}
	return 0;
}

void dw3000_hw_interrupt_enable(void)
{
	if (conf.gpio_irq.port) {
		printk("[%s %d] set IRQ pin enable\n", __func__, __LINE__);
		gpio_pin_interrupt_configure_dt(&conf.gpio_irq, GPIO_INT_EDGE_RISING);
		dw3000_irs_flag = true;
	}
}

void dw3000_hw_interrupt_disable(void)
{
	if (conf.gpio_irq.port) {
		printk("[%s %d] set IRQ pin disable\n", __func__, __LINE__);
		gpio_pin_interrupt_configure_dt(&conf.gpio_irq, GPIO_INT_DISABLE);
		dw3000_irs_flag = false;
	}
}

bool dw3000_hw_interrupt_is_enabled(void)
{
	return dw3000_irs_flag; // TODO
}

void dw3000_hw_fini(void)
{
	// TODO
	if (conf.gpio_irq.port) {
		gpio_pin_interrupt_configure_dt(&conf.gpio_irq, GPIO_INT_DISABLE);
		gpio_pin_configure_dt(&conf.gpio_irq, GPIO_DISCONNECTED);
	}
	if (conf.gpio_reset.port) {
		gpio_pin_configure_dt(&conf.gpio_reset, GPIO_DISCONNECTED);
	}
	if (conf.gpio_wakeup.port) {
		gpio_pin_configure_dt(&conf.gpio_wakeup, GPIO_DISCONNECTED);
	}

	if (conf.gpio_sync.port) {
		gpio_pin_configure_dt(&conf.gpio_sync, GPIO_DISCONNECTED);
	}

	dw3000_spi_fini();
}

void dw3000_hw_sync()
{
	if (!conf.gpio_sync.port) {
		printk("[%s %d] No HW sync configured\n", __func__, __LINE__);
		return;
	}

	gpio_pin_configure_dt(&conf.gpio_sync, GPIO_OUTPUT_INACTIVE);
	k_busy_wait(1); // 1usÂö³å£¬¹¤³ÌÎÈÍ×
	gpio_pin_configure_dt(&conf.gpio_sync, GPIO_OUTPUT_ACTIVE);
}

void dw3000_hw_reset()
{
	if (!conf.gpio_reset.port) {
		printk("[%s %d] No HW reset configured\n", __func__, __LINE__);
		return;
	}

	gpio_pin_configure_dt(&conf.gpio_reset, GPIO_OUTPUT_INACTIVE);
	k_msleep(1); // 10 us?
	gpio_pin_configure_dt(&conf.gpio_reset, GPIO_OUTPUT_ACTIVE);
	k_msleep(2);
}

/** wakeup either using the WAKEUP pin or SPI CS */
void dw3000_hw_wakeup(void)
{
	if (conf.gpio_wakeup.port) {
		/* Use WAKEUP pin if available */
		printk("[%s %d] WAKEUP PIN\n", __func__, __LINE__);
		gpio_pin_set_dt(&conf.gpio_wakeup, 0);
		k_msleep(1);
		gpio_pin_set_dt(&conf.gpio_wakeup, 1);

	} else {
		/* Use SPI CS pin */
		printk("[%s %d] WAKEUP CS\n", __func__, __LINE__);
		dw3000_spi_wakeup();
	}
}

/** set WAKEUP pin low if available */
void dw3000_hw_wakeup_pin_low(void)
{
	if (conf.gpio_wakeup.port) {
		gpio_pin_set_dt(&conf.gpio_wakeup, 0);
	}
}
