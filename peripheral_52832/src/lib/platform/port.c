#include "port.h"
#include "deca_device_api.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
#include "node.h"


LOG_MODULE_REGISTER(dw1000_port);

struct gpio_dt_spec dw1000_irq = GPIO_DT_SPEC_GET(DT_NODELABEL(dw1000_irq), gpios);
struct gpio_dt_spec dw1000_cs = GPIO_DT_SPEC_GET(DT_NODELABEL(dw1000_cs), gpios);
struct gpio_dt_spec dw1000_rst = GPIO_DT_SPEC_GET(DT_NODELABEL(dw1000_rst), gpios);

uint8_t uiIRSStatus = 0;

const struct device *spi_dev = DEVICE_DT_GET(SPI1_DEVICE);
struct spi_config spi_cfg = {
    .frequency = 2000000U,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

const struct device *gpio0_dev = DEVICE_DT_GET(MY_GPIO0);

void deca_sleep(unsigned int time_ms){
    k_msleep(time_ms);
}

void DW1000IRQ_init(void);

void dw1000_configure_gpio(){
    int err;
    err = gpio_pin_configure_dt(&dw1000_cs, GPIO_OUTPUT);
    if (err) {
        printf("Cannot configure dw1000 cs");
        return;
    }

    err = gpio_pin_set_dt(&dw1000_cs, 1);
    if (err) {
        printf("Cannot write dw1000 cs");
        return;
    }

    err = gpio_pin_configure_dt(&dw1000_rst, GPIO_OUTPUT);
    if (err) {
        printf("Cannot configure dw1000 rst");
        return;
    }

    err = gpio_pin_set_dt(&dw1000_rst, 1);
    if (err) {
        printf("Cannot write dw1000 rst");
        return;
    }

    DW1000IRQ_init();
}

void dw1000_spi_set_cs(int value)
{
    int err;
    err = gpio_pin_set_dt(&dw1000_cs, value);
    if (err) {
        printf("Cannot write dw1000 cs");
    }
}


int spi_lowrate(){
    spi_cfg.frequency = 2000000U;
    if (!device_is_ready(spi_dev)) {
        /* Not ready, do not use */
        printf("ERROR");
        return -ENODEV;
    }
    return 1;
}

int spi_fastrate(){
    spi_cfg.frequency = 4000000U;
    if (!device_is_ready(spi_dev)) {
        /* Not ready, do not use */
        printf("ERROR");
        return -ENODEV;
    }
    return 1;
}

void reset_DW1000(void){
    int err;
    err = gpio_pin_set_dt(&dw1000_rst, 0);
    if (err) {
        printf("Cannot write dw1000 rst");
        return;
    }
   k_msleep(2);
   
    err = gpio_pin_set_dt(&dw1000_rst, 1);
    if (err) {
        printf("Cannot write dw1000 rst");
        return;
    }
   k_msleep(10); 
}

static struct gpio_callback gpio_cb;

static void button_pressed(const struct device *gpio_dev, struct gpio_callback *cb,
		    uint32_t pins)
{
    // printf("button\r");
    if(uiIRSStatus)
    {
        dw1000_isr_ok_receive();
        // dw1000_isr_receivemsg();
    }
}


void DW1000IRQ_init(void)
{
    int err;

    if (!gpio_is_ready_dt(&dw1000_irq)) {
        LOG_ERR("Button device %s is not ready", dw1000_irq.port->name); 
        return;
    }
#if 0
    gpio_flags_t flags = dw1000_irq.dt_flags & GPIO_ACTIVE_LOW ? GPIO_PULL_UP : GPIO_PULL_DOWN;

     /* Configure button pin as input mode */
    err = gpio_pin_configure_dt(&dw1000_irq, GPIO_INPUT | flags);
    if (err != 0) {
        printf("Failed to configure %s pin %d (error %d)", dw1000_irq.port->name, dw1000_irq.pin, err);
        return;
    }

    err = gpio_pin_interrupt_configure_dt(&dw1000_irq, GPIO_INT_DISABLE);
    if (err) {
        printf("Cannot disable callbacks()");
        return;
    }

    gpio_init_callback(&gpio_cb, button_pressed, BIT(dw1000_irq.pin));

    err = gpio_add_callback(dw1000_irq.port, &gpio_cb);
    if (err) {
        printf("Cannot add callback");
        return;
    }

    printf("****************************");
    // gpio_pin_interrupt_configure_dt(&dw1000_irq, GPIO_INT_LEVEL_ACTIVE);
    printf("****************************");
#endif
    err =  gpio_pin_interrupt_configure_dt(&dw1000_irq, /*GPIO_INT_EDGE_TO_ACTIVE*/GPIO_INT_EDGE_BOTH);
    gpio_init_callback(&gpio_cb, button_pressed, BIT(dw1000_irq.pin));
    gpio_add_callback(dw1000_irq.port, &gpio_cb);
}

void DW1000IRQ_enable(int iEnablke)
{
    uiIRSStatus = iEnablke;
}

int DW1000Get_Irs()
{
    return gpio_pin_get_dt(&dw1000_irq);
}