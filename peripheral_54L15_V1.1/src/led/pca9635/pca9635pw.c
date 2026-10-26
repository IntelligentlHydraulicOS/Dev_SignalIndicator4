#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>


#include "pca9635pw.h"

/* PCA9635 寄存器 */
#define PCA9635_MODE1       0x00
#define PCA9635_MODE2       0x01
#define PCA9635_PWM0        0x02
#define PCA9635_LEDOUT_BASE  0x14
#define PCA9635_LEDOUT0     0x0A
#define PCA9635_LEDOUT1     0x0B
#define PCA9635_LEDOUT2     0x0C
#define PCA9635_LEDOUT3     0x0D
#define PCA9635_GRPPWM      0x0E
#define PCA9635_GRPFREQ     0x0F


/* MODE1 bit */
#define PCA9635_SLEEP       BIT(4)
#define PCA9635_ALLCALL     BIT(0)
/* MODE2 bit */
#define PCA9635_OUTDRV      BIT(2)
#define PCA9635_DMBLNK      BIT(5)   // 1=Blink闪烁，0=Dimming调光

#define PCA9635_OCH          BIT(3)
#define PCA9635_INVRT        BIT(4)
#define PCA9635_BLINK        BIT(5)

#define I2C0_NODE DT_NODELABEL(nxp_pca9635pw)
static const struct i2c_dt_spec pca9635_spec = I2C_DT_SPEC_GET(I2C0_NODE);
static const struct gpio_dt_spec pca9635_oe_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(pca9635_oe), gpios);
static const struct gpio_dt_spec pca9635_key_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(pca9635_key), gpios);

void pca9635_init()
{
    int ret;
    if (!device_is_ready(pca9635_spec.bus)) 
    {
        printk("I2C bus %s is not ready!\n\r", pca9635_spec.bus->name);
        return;
    }

    ret = gpio_pin_configure_dt(&pca9635_oe_gpio, GPIO_OUTPUT);
    if (ret) {
        printk("[%s %d]Cannot configure LED gpio", __func__, __LINE__);
    }

    ret = gpio_pin_set_dt(&pca9635_oe_gpio, GPIO_OUTPUT_LOW);
	if (ret) {
		printk("[%s %d] Cannot write LED gpio", __func__, __LINE__);
	}
    
    ret = gpio_pin_configure_dt(&pca9635_key_gpio, GPIO_OUTPUT);
    if (ret) {
        printk("[%s %d]Cannot configure LED gpio", __func__, __LINE__);
    }

    ret = gpio_pin_set_dt(&pca9635_key_gpio, GPIO_OUTPUT_HIGH);
	if (ret) {
		printk("[%s %d] Cannot write LED gpio", __func__, __LINE__);
	}
    
#if 0
    /* 1. 唤醒芯片：清除SLEEP位 */
    ret = i2c_reg_update_byte_dt(&pca9635_spec, PCA9635_MODE1, PCA9635_SLEEP, 0);
    if (ret < 0) {
        printk("Wake PCA9635 fail, ret:%d\n", ret);
        return;
    }
    k_msleep(1);  // 等待内部振荡器稳定，datasheet要求

    /* 关闭ALLCALL广播地址（不需要全局广播） */
    i2c_reg_update_byte_dt(&pca9635_spec, PCA9635_MODE1, PCA9635_ALLCALL | BIT(5), 0);

    /* 2. MODE2：开漏输出（匹配共阳极LED），暂时先不开启Blink */
    ret = i2c_reg_update_byte_dt(&pca9635_spec, PCA9635_MODE2,
		PCA9635_OUTDRV | PCA9635_OCH | PCA9635_INVRT | PCA9635_BLINK,
		PCA9635_OUTDRV | PCA9635_OCH);
    if (ret < 0) {
        printk("Set OUTDRV open-drain fail\n");
        return;
    }

/*
    i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 0, 0xAA);
	i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 1, 0xAA);
	i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 2, 0xAA);
	i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 3, 0xAA);
*/
    //PWM全部清零
	for (int i = 0; i < 16; i++) {
		i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_PWM0 + i, 0xC);
	}

    // i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_MODE1, 0x00);
    // k_msleep(1);
    // i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_MODE2, 0x10);
    
    // i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 0, 0x0);
	// i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 1, 0x0);
	// i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 2, 0x0);
	// i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 3, 0x0);

    // for(int i = 0; i < 16; i++)
    // {
    //     i2c_reg_write_byte_dt(&pca9635_spec, 0x2 + 3, 0xFF);
    // }
#endif
#if 1

/* Turn off LEDs by default*/
    for(int i = 0; i < 4; i++)
    {
        i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + i, 0x00);//0x55 亮， 0x00 灭， 0xAA：pwm
    }
    /* Disable LED all-call address, and power down initially */
    i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_MODE2, 0x4);

    i2c_reg_update_byte_dt(&pca9635_spec, PCA9635_MODE1, 0, PCA9635_SLEEP);
    k_usleep(500);
#endif
    printk("[%s %d] pca9635 init ok\r\n", __func__, __LINE__);
}

void pca9635_all_set_brightness(uint8_t br)
{
    int ret;
    const uint8_t ledout_pwm = 0xAAU; // 每通道 LDR=10，独立PWM

    // 第一步：全部LEDOUT切到PWM模式
    ret  = i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 0, ledout_pwm);
    ret |= i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 1, ledout_pwm);
    ret |= i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 2, ledout_pwm);
    ret |= i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_LEDOUT_BASE + 3, ledout_pwm);
    if (ret < 0) {
        printk("all set ledout pwm mode fail %d", ret);
        return;
    }

    // 第二步：批量写16个PWM通道
    for (int ch = 0; ch < 16; ch++) {
        ret = i2c_reg_write_byte_dt(&pca9635_spec, PCA9635_PWM0 + ch, br);
        if (ret < 0) {
            printk("ch %d pwm write fail %d", ch, ret);
            return ;
        }
    }
}

static uint8_t uiNum = 0;
void pca9635_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight)
{
    uiNum = (uiNum + 1) %2;
    if(uiNum)
    {
        pca9635_all_set_brightness(254);
    }
    else
    {
        pca9635_all_set_brightness(5);
    }
}
