#include "lsm6ds3_spi_driver.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lsm6ds3, LOG_LEVEL_INF);

#define LSM6DS3_CS_PIN 22

static lsm6ds3_motion_cb_t g_motion_callback;
static const struct spi_dt_spec *g_cfg;
void lsm6ds3_clear_activity_int_flag(const struct spi_dt_spec *spec);

/* gpio中断回调 */
static void lsm6ds3_int1_handler(const struct device *dev,
                                 struct gpio_callback *cb,
                                 uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    if(g_motion_callback != NULL)
    {
        g_motion_callback();
    }
    /* 读取寄存器清除中断锁存标志 */
    if(g_cfg != NULL)
    {
        lsm6ds3_clear_activity_int_flag(g_cfg);
    }
}

static struct gpio_callback int1_gpio_cb_data;
const struct gpio_dt_spec lsm6ds3_cs = GPIO_DT_SPEC_GET(DT_NODELABEL(lsm6ds3_cs), gpios);
// static struct gpio_dt_spec int1_gpio = GPIO_DT_SPEC_GET_OR(LSM6DS3_INT1_NODE, gpios, {0});
static struct gpio_dt_spec int1_gpio = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 23,
	.dt_flags = GPIO_PULL_UP,
};

void lsm6ds3_spi_set_cs(int value)
{
    int err;
    err = gpio_pin_set_dt(&lsm6ds3_cs, value);
    if (err) {
        printf("Cannot write lsm6ds3 cs");
    }
}

static uint8_t lsm6ds3_read_reg(const struct spi_dt_spec *spec, uint8_t reg)
{
    uint8_t tx[2];
    uint8_t rx[2];
    tx[0] = reg | BIT(7); /* SPI读bit7置1 */

    struct spi_buf tx_bufs[] = {{.buf = tx, .len = 1}};
    struct spi_buf rx_bufs[] = {{.buf = rx, .len = 2}};
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
    struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 1};
    
    lsm6ds3_spi_set_cs(0);
    spi_transceive_dt(spec, &tx_set, &rx_set);
    lsm6ds3_spi_set_cs(1);

    return rx[1];
}

static int lsm6ds3_write_reg(const struct spi_dt_spec *spec, uint8_t reg, uint8_t val)
{
    int ret = 0;
    uint8_t tx[2];
    tx[0] = reg & ~BIT(7);
    tx[1] = val;

    struct spi_buf tx_bufs[] = {{.buf = tx, .len = 2}};
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
    lsm6ds3_spi_set_cs(0);
    ret = spi_write_dt(spec, &tx_set);
    lsm6ds3_spi_set_cs(1);

    return ret;
}

/**
 * @brief 开启 Activity / Inactivity 活动静止检测
 * @param spec spi设备
 * @param wu_th 活动检测阈值 0~127，1LSB=62.5mg；例 16 ≈ 1g
 * @param wu_dur 运动样本计数，多少个样本持续变化才算Activity；0‑7
 * @param inact_dur 静止样本计数，多少样本不动才算Inactivity；0‑7
 * @return 0成功
 *
 * 示例参数： wu_th=8, wu_dur=2, inact_dur=4
 *  wu_th=8 → 8*62.5mg = 500mg 加速度变化门限
 *  wu_dur=2 → 连续2个样本有变化才判定【活动】
 *  inact_dur=4 → 连续4个样本无变化判定【静止】
 */
int lsm6ds3_enable_activity_inactivity(const struct spi_dt_spec *spec,
                                        uint8_t wu_th,
                                        uint8_t wu_dur,
                                        uint8_t inact_dur)
{
    int ret;

    /* 1. WAKE_UP_THS 设置活动阈值，低7位WU_THS */
    ret = lsm6ds3_write_reg(spec, LSM6DS3_WAKE_UP_THS, wu_th & 0x7FU);
    if(ret != 0) return ret;

    /* 2. WAKE_UP_DUR
     * bit4 INACT_EN = 1 开启Inactivity静止检测
     * bit[2:0] WU_DUR  活动持续样本
     * bit[7:5] INACT_DUR 静止持续样本
     */
    uint8_t dur_val = ((inact_dur & 0x07U) << 5U) | BIT(4U) | (wu_dur & 0x07U);
    ret = lsm6ds3_write_reg(spec, LSM6DS3_WAKE_UP_DUR, dur_val);
    if(ret != 0) return ret;

    /* 3. CTRL8_XL：HP_SLOPE_EN=bit6=1，开启斜率高通滤波器，Activity必须！滤除静态重力 */
    ret = lsm6ds3_write_reg(spec, LSM6DS3_CTRL8_XL, BIT(6U));
    if(ret != 0) return ret;

    /* 4. MD1_CFG：路由 Activity + Inactivity 事件输出到INT1引脚
     * bit6 ACT_on_INT1 =1；bit5 INACT_on_INT1 =1
     */
    ret = lsm6ds3_write_reg(spec, LSM6DS3_MD1_CFG, BIT(6U) | BIT(5U));
    if(ret != 0) return ret;

    return 0;
}

/**
 * @brief 关闭 Activity / Inactivity
 */
int lsm6ds3_disable_activity_inactivity(const struct spi_dt_spec *spec)
{
    lsm6ds3_write_reg(spec, LSM6DS3_WAKE_UP_THS, 0x00U);
    lsm6ds3_write_reg(spec, LSM6DS3_WAKE_UP_DUR, 0x00U);
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL8_XL, 0x00U);
    lsm6ds3_write_reg(spec, LSM6DS3_MD1_CFG, 0x00U);
    return 0;
}

/**
 * @brief 清除Activity/Inactivity中断标志，读WAKE_UP_SRC寄存器
 */
void lsm6ds3_clear_activity_int_flag(const struct spi_dt_spec *spec)
{
    uint8_t flag = lsm6ds3_read_reg(spec, LSM6DS3_WAKE_UP_SRC);
    printf("WAKE_UP_SRC:0x%02X ACT=%d INACT=%d\r\n",
           flag,
           (flag & BIT(1U)) ? 1 : 0,
           (flag & BIT(2U)) ? 1 : 0);
}

int lsm6ds3_init(const struct spi_dt_spec *spec, lsm6ds3_motion_cb_t cb)
{
    /* 手动初始化CS引脚！！nrf spim不会自动初始化cs‑gpios的gpio */
	gpio_pin_configure_dt(&lsm6ds3_cs, GPIO_OUTPUT);
    gpio_pin_set_dt(&lsm6ds3_cs, 1);

    if (!spi_is_ready_dt(spec)) {
        printf("SPI device not ready\r\n");
        return -ENODEV;
    }
    g_cfg = spec;
    g_motion_callback = cb;

    k_msleep(10);

    uint8_t id = lsm6ds3_read_reg(spec, LSM6DS3_WHO_AM_I);
    if(id != LSM6DS3_WHO_AM_I_VAL)
    {
        printf("WHO_AM_I wrong:0x%02X expect 0x%02X\r\n", id, LSM6DS3_WHO_AM_I_VAL);
        return -EIO;
    }
    printf("LSM6DS3TR-C detected ok\r\n");

    /* 软件复位 */
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL3_C, 0x44 | BIT(0));
    k_msleep(5);
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL3_C, 0x44);

    /* CTRL9_XL 打开X/Y/Z三轴加速度输出 */
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL9_XL, 0x38);

    /* CTRL1_XL ODR=52Hz ±2g */
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL1_XL, 0x20);

    /* 陀螺仪断电，不需要 */
    lsm6ds3_write_reg(spec, LSM6DS3_CTRL2_G, 0x00);

    /* ==========开启 Activity / Inactivity ==========
     * 阈值8≈500mg，运动持续2样本，静止判定4样本
     */
    int ret = lsm6ds3_enable_activity_inactivity(spec, 8U, 2U, 4U);
    if(ret != 0)
    {
        printf("enable activity inactivity fail %d\r\n", ret);
        return ret;
    }

    /* INT1 GPIO P0.23，电平中断！锁存输出，禁止边沿中断 */
    if (!gpio_is_ready_dt(&int1_gpio))
    {
        printf("int1 gpio port not ready\r\n");
        return -ENODEV;
    }
    gpio_init_callback(&int1_gpio_cb_data, lsm6ds3_int1_handler, BIT(int1_gpio.pin));
    gpio_add_callback(int1_gpio.port, &int1_gpio_cb_data);
    gpio_pin_configure_dt(&int1_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure(int1_gpio.port, int1_gpio.pin, GPIO_INT_LEVEL_HIGH);

    k_msleep(20);
    /* 清除WAKE_UP_SRC中断标志 */
    lsm6ds3_clear_activity_int_flag(spec);
    
    return 0;
}

int lsm6ds3_read_data(const struct spi_dt_spec *spec, struct lsm6ds3_data *data)
{
    uint8_t buf[15];
    uint8_t tx_cmd[2] = {0x20 | BIT(7), 0};

    struct spi_buf tx_bufs[] = {{.buf = &tx_cmd, .len = 1}};
    struct spi_buf rx_bufs[] = {{.buf = buf, .len = 15}};
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
    struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 1};

    lsm6ds3_spi_set_cs(0);
    int ret = spi_transceive_dt(spec, &tx_set, &rx_set);
    if(ret !=0)
    {
        return ret;
    }
    lsm6ds3_spi_set_cs(1);
#if 0
    printf("lsm6ds3_read_data[");
    for(int i = 0; i < sizeof(buf); i++)
    {
        printf("%02X ", buf[i]);
    }
    printf("]\r\n");
#endif
    data->temp     = (int16_t)((buf[2] << 8U) | buf[1]);
    data->gyro_x   = (int16_t)((buf[4] << 8U) | buf[3]);
    data->gyro_y   = (int16_t)((buf[6] << 8U) | buf[5]);
    data->gyro_z   = (int16_t)((buf[8] << 8U) | buf[7]);
    data->acc_x    = (int16_t)((buf[10] << 8U) | buf[9]);
    data->acc_y    = (int16_t)((buf[12] << 8U)| buf[11]);
    data->acc_z    = (int16_t)((buf[14] << 8U)| buf[13]);
    return 0;
}
