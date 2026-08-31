#ifndef LSM6DS3_SPI_DRIVER_H
#define LSM6DS3_SPI_DRIVER_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

/* LSM6DS3TR‑C Register */
#define LSM6DS3_WHO_AM_I        0x0F
#define LSM6DS3_WHO_AM_I_VAL    0x6A

#define LSM6DS3_CTRL1_XL        0x10
#define LSM6DS3_CTRL2_G         0x11
#define LSM6DS3_CTRL3_C         0x12

#define LSM6DS3_CTRL9_XL        0x18
#define LSM6DS3_CTRL10_C        0x19
#define LSM6DS3_INT1_CTRL       0x0D
#define LSM6DS3_MD1_CFG         0x5E
#define LSM6DS3_FUNC_SRC1       0x53

#define LSM6DS3_WAKE_UP_THS     0x5B
#define LSM6DS3_WAKE_UP_DUR     0x5C
#define LSM6DS3_WAKE_UP_SRC     0x1B
#define LSM6DS3_CTRL8_XL        0x17

/* INT1 gpio P0.23 */
// #define LSM6DS3_INT1_NODE DT_NODELABEL(lsm6ds3-int1)
#define LSM6DS3_NODE DT_NODELABEL(lsm6ds3)
#define LSM6DS3_INT1_NODE DT_ALIAS(lsm6ds3-int1)
// #define LSM6DS3_INT1_GPIO_SPEC GPIO_DT_SPEC_GET_BY_IDX(LSM6DS3_INT1_NODE, gpios, 0)

struct lsm6ds3_data {
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t temp;
};

/* 中断回调：检测到显著运动触发 */
typedef void (*lsm6ds3_motion_cb_t)(void);

/**
 * @brief lsm6ds3初始化，配置显著运动检测，开启INT1中断输出
 * @param cfg spi配置
 * @param cb 运动事件回调，可以传NULL
 * @return 0成功
 */
int lsm6ds3_init(const struct spi_dt_spec *spec, lsm6ds3_motion_cb_t cb);

/**
 * @brief 读取原始6轴+温度数据
 */
int lsm6ds3_read_data(const struct spi_dt_spec *spec, struct lsm6ds3_data *data);


#endif
