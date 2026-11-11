/*
 * PCA9635PW 16 通道 I2C LED 驱动（带 RGB 颜色设置）
 *
 * 实现要点（对应 NXP PCA9635 数据手册 Rev.7.1）：
 *   - MODE1.SLEEP 必须清 0，且等 >=500us 振荡器才稳定（Table 6 脚注）；
 *   - LEDOUTx 每通道 2 bit：00 灭 / 01 全亮 / 10 独立 PWM / 11 PWM+组调光
 *     （Table 11），写亮度必须用 10；
 *   - PWMx 只在 LDRx=10 或 11 时生效（Table 8）；
 *   - OE 为低有效使能，OE=1 时输出由 MODE2.OUTNE 决定（Table 14）。
 *
 * 本板实测：overlay 里 OE 引脚配成 GPIO_ACTIVE_LOW，逻辑 0（物理高）
 *           时输出才使能；换板子时改 pca9635_init() 里那两行 GPIO 即可。
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "pca9635pw.h"

/* ---------------- 寄存器 ---------------- */
#define PCA9635_MODE1        0x00
#define PCA9635_MODE2        0x01
#define PCA9635_PWM0         0x02
#define PCA9635_LEDOUT0      0x14   /* LEDOUT0..LEDOUT3 = 0x14..0x17 */

/* MODE1 bit */
#define PCA9635_SLEEP        BIT(4)

/* MODE2 位定义（Table 7） */
#define PCA9635_OUTDRV       BIT(2)   /* 1 = 图腾柱输出 */
#define PCA9635_INVRT        BIT(4)   /* 1 = 输出反相 */

/* OUTNE[1:0]：OE=1（输出禁止）时 LEDn 的状态 */
#define PCA9635_OUTNE_MASK   (BIT(1) | BIT(0))
#define PCA9635_OUTNE_0      0x00U    /* 00：强制输出 0 */
#define PCA9635_OUTNE_1      BIT(0)   /* 01：输出 1（配 OUTDRV=1） */
#define PCA9635_OUTNE_HIZ    BIT(1)   /* 10：高阻 */

/* LEDOUT 每通道 2 bit */
#define PCA9635_LEDOUT_PWM   0xAAU    /* 10 10 10 10：四路独立 PWM */

/*
 * MODE2 配置 = 0x15：
 *   OUTDRV=1  图腾柱
 *   OUTNE=01  OE=1 时输出高
 *   INVRT=1   输出反相
 *
 * 本板 LED 是“输出高电平点亮”，而芯片的 duty 默认按“输出低=点亮”
 * （灌电流/共阳）定义，所以必须开 INVRT，PWM 值才和亮度同向：
 * PWM=0 灭、PWM=255 最亮。若换到共阳接法的板子，把 INVRT 去掉（0x05）。
 */
#define PCA9635_MODE2_CFG \
	(PCA9635_OUTDRV | PCA9635_OUTNE_1 | PCA9635_INVRT)

BUILD_ASSERT(PCA9635_MODE2_CFG == 0x15, "MODE2 默认配置应为 0x15");

/* ---------------- 器件 ---------------- */
#define PCA9635_NODE DT_NODELABEL(nxp_pca9635pw)

static const struct i2c_dt_spec pca9635_spec = I2C_DT_SPEC_GET(PCA9635_NODE);
static const struct gpio_dt_spec pca9635_oe_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(pca9635_oe), gpios);
static const struct gpio_dt_spec pca9635_key_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(pca9635_key), gpios);

/*
 * 通道映射：led_ch_map[i] = {R, G, B} 三个通道号。
 * 默认按 0,1,2 / 3,4,5 ... 顺序排，最后一路 LED15 空着。
 * 如果实际是 R/B 反接，把某一行的 0 和 2 对调即可。
 */
static const uint8_t led_ch_map[PCA9635_LED_COUNT][3] = {
	{ 0,  1,  2},
	{ 3,  4,  5},
	{ 6,  7,  8},
	{ 9, 10, 11},
	{12, 13, 14},
};

/* ---------------- 内部状态 ---------------- */
static struct LED_RGB_T led_color[PCA9635_LED_COUNT];   /* 各灯原始颜色 */
static uint8_t master_brightness = PCA9635_BRIGHTNESS_MAX;
static bool pca9635_ready;

static int pca9635_write_reg(uint8_t reg, uint8_t val)
{
	int err = i2c_reg_write_byte_dt(&pca9635_spec, reg, val);

	return err < 0 ? err : 0;
}

/* 亮度缩放：color * brightness / 255 */
static uint8_t pca9635_scale(uint8_t color)
{
	return (uint8_t)(((uint16_t)color * master_brightness) / 255U);
}

/* 把第 idx 颗灯（R/G/B 三个通道）写进芯片 */
static int pca9635_apply_led(uint8_t idx)
{
	const struct LED_RGB_T *c = &led_color[idx];
	uint8_t ch_r = led_ch_map[idx][0];
	uint8_t ch_g = led_ch_map[idx][1];
	uint8_t ch_b = led_ch_map[idx][2];
	int err = 0;

	err |= pca9635_write_reg(PCA9635_PWM0 + ch_r, pca9635_scale(c->r));
	err |= pca9635_write_reg(PCA9635_PWM0 + ch_g, pca9635_scale(c->g));
	err |= pca9635_write_reg(PCA9635_PWM0 + ch_b, pca9635_scale(c->b));

    // printf("[%s %d] pca9635_apply_led: idx=%d, r=%d, g=%d, b=%d, brightness=%d\n", __func__, __LINE__, idx, c->r, c->g, c->b, master_brightness);
	return err;
}

/* ---------------- 初始化 ---------------- */
int pca9635_init(void)
{
	int err;

	if (!device_is_ready(pca9635_spec.bus)) {
		printk("[pca9635] I2C bus %s not ready\n", pca9635_spec.bus->name);
		return -ENODEV;
	}

	/* OE：overlay 里是 GPIO_ACTIVE_LOW，逻辑 0 = 物理高 = 输出使能（实测） */
	if (gpio_pin_configure_dt(&pca9635_oe_gpio, GPIO_OUTPUT) == 0) {
		gpio_pin_set_dt(&pca9635_oe_gpio, GPIO_OUTPUT_LOW);
	}

	/* KEY：板上置高 */
	if (gpio_pin_configure_dt(&pca9635_key_gpio, GPIO_OUTPUT) == 0) {
		gpio_pin_set_dt(&pca9635_key_gpio, GPIO_OUTPUT_HIGH);
	}

	/* 唤醒：MODE1 = 0，清 SLEEP */
	err = pca9635_write_reg(PCA9635_MODE1, 0x00);
	if (err < 0) {
		printk("[pca9635] wake fail %d\n", err);
		return err;
	}
	k_msleep(1);   /* 振荡器起振 < 500us */

	/* 输出模式 */
	err = pca9635_write_reg(PCA9635_MODE2, PCA9635_MODE2_CFG);
	if (err < 0) {
		printk("[pca9635] set MODE2 fail %d\n", err);
		return err;
	}

	/* 16 路全部切到“独立 PWM” */
	for (int i = 0; i < 4; i++) {
		err = pca9635_write_reg(PCA9635_LEDOUT0 + i, PCA9635_LEDOUT_PWM);
		if (err < 0) {
			printk("[pca9635] set LEDOUT%d fail %d\n", i, err);
			return err;
		}
	}

	memset(led_color, 0, sizeof(led_color));
	master_brightness = PCA9635_BRIGHTNESS_MAX;
	pca9635_ready = true;

	err = pca9635_off();
	if (err < 0) {
		return err;
	}

	printk("[pca9635] init ok (%d LEDs)\n", PCA9635_LED_COUNT);
	return 0;
}

/* ---------------- 颜色 ---------------- */
int pca9635_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
	if (!pca9635_ready) {
		return -ENODEV;
	}
	if (idx >= PCA9635_LED_COUNT) {
		return -EINVAL;
	}

	led_color[idx].r = r;
	led_color[idx].g = g;
	led_color[idx].b = b;

	return pca9635_apply_led(idx);
}

int pca9635_set_color_rgb(uint8_t idx, const struct LED_RGB_T *rgb)
{
	if (rgb == NULL) {
		return -EINVAL;
	}

	return pca9635_set_color(idx, rgb->r, rgb->g, rgb->b);
}

int pca9635_set_all_color(uint8_t r, uint8_t g, uint8_t b)
{
	int err = 0;

	for (uint8_t i = 0; i < PCA9635_LED_COUNT; i++) {
		err |= pca9635_set_color(i, r, g, b);
	}

	return err;
}

int pca9635_set_all_rgb(const struct LED_RGB_T *rgb, uint8_t brightness)
{
    // printf("[%s %d] pca9635_set_all_rgb: r=%d, g=%d, b=%d, brightness=%d\n", __func__, __LINE__, rgb->r, rgb->g, rgb->b, brightness);
	if (rgb == NULL) {
		return -EINVAL;
	}
	if (!pca9635_ready) {
		return -ENODEV;
	}

	/* 颜色 + 亮度一次性下发，并把亮度记为当前全局亮度 */
	master_brightness = brightness;

	//return pca9635_set_all_color(rgb->r, rgb->g, rgb->b);
	return pca9635_set_all_color(rgb->b, rgb->g, rgb->r);
}

int pca9635_set_brightness(uint8_t br)
{
	int err = 0;

	if (!pca9635_ready) {
		return -ENODEV;
	}

	master_brightness = br;

	for (uint8_t i = 0; i < PCA9635_LED_COUNT; i++) {
		err |= pca9635_apply_led(i);
	}

	return err;
}

uint8_t pca9635_get_brightness(void)
{
	return master_brightness;
}

int pca9635_off(void)
{
	int err = 0;

	if (!pca9635_ready) {
		return -ENODEV;
	}

	for (uint8_t ch = 0; ch < 16; ch++) {
		err |= pca9635_write_reg(PCA9635_PWM0 + ch, 0x00);
	}

	return err;
}

int pca9635_on(void)
{
	int err = 0;

	if (!pca9635_ready) {
		return -ENODEV;
	}

	for (uint8_t i = 0; i < PCA9635_LED_COUNT; i++) {
		err |= pca9635_apply_led(i);
	}

	return err;
}

int pca9635_dump(void)
{
	uint8_t v;
	int err = 0;

	if (!pca9635_ready) {
		return -ENODEV;
	}

	err |= i2c_reg_read_byte_dt(&pca9635_spec, PCA9635_MODE1, &v);
	printk("[pca9635] MODE1 =0x%02X\n", v);
	err |= i2c_reg_read_byte_dt(&pca9635_spec, PCA9635_MODE2, &v);
	printk("[pca9635] MODE2 =0x%02X\n", v);

	for (int i = 0; i < 4; i++) {
		err |= i2c_reg_read_byte_dt(&pca9635_spec, PCA9635_LEDOUT0 + i, &v);
		printk("[pca9635] LEDOUT%d=0x%02X\n", i, v);
	}

	printk("[pca9635] PWM:");
	for (int ch = 0; ch < 16; ch++) {
		err |= i2c_reg_read_byte_dt(&pca9635_spec, PCA9635_PWM0 + ch, &v);
		printk(" %3d", v);
	}
	printk("\n");

	return err < 0 ? err : 0;
}

/* ---------------- 兼容旧接口 ---------------- */
void pca9635_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight)
{
	static uint8_t lit = 1;

	if (RgbLed == NULL) {
		return;
	}

	if (uiLight == 0) {
		lit = 1;              /* 常亮 */
	} else {
		lit = 0;             /* 每次调用翻转 */
	}

	if (lit) {
		pca9635_set_all_rgb(RgbLed, 50);
	} else {
		pca9635_set_all_rgb(RgbLed, 0);
	}
}
