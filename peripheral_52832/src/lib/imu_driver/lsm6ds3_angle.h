#ifndef LSM6DS3_ANGLE_H
#define LSM6DS3_ANGLE_H

#include <zephyr/kernel.h>
#include <math.h>

/**
 * @brief 姿态角度结构体
 * roll: 绕X轴横滚角 [-180 ~ +180] °
 * pitch: 绕Y轴俯仰角 [-90 ~ +90] °
 * yaw: 仅加速度计无法计算，固定0
 */
typedef struct {
    float roll;
    float pitch;
    float yaw;
} lsm6ds3_attitude_t;

/**
 * @brief 由原始加速度计int16采样值，转换g单位，再计算roll/pitch倾角
 * @param acc_x raw原始int16加速度X
 * @param acc_y raw原始int16加速度Y
 * @param acc_z raw原始int16加速度Z
 * @param full_scale_g 量程，当前你的配置 ±2g，填2.0f
 * @param att 输出姿态角度
 */
void lsm6ds3_accel_calc_attitude(int16_t acc_x, int16_t acc_y, int16_t acc_z,
                                  float full_scale_g,
                                  lsm6ds3_attitude_t *att);

/**
 * @brief 简单低通滤波，平滑角度输出，抑制抖动
 * @param new_att 新计算出来角度
 * @param out_att 滤波输出姿态
 * @param alpha 滤波系数 0~1；越小越平滑，响应越慢，推荐0.08f
 */
void lsm6ds3_attitude_lowpass(const lsm6ds3_attitude_t *new_att,
                              lsm6ds3_attitude_t *out_att,
                              float alpha);

#endif
