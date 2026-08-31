#include "lsm6ds3_angle.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846F
#endif

/* ±2g量程灵敏度 LSB -> g  0.061 mg/LSB = 0.000061 g/LSB */
#define LSM6DS3_2G_SENS  0.000061f

void lsm6ds3_accel_calc_attitude(int16_t acc_x, int16_t acc_y, int16_t acc_z,
                                  float full_scale_g,
                                  lsm6ds3_attitude_t *att)
{
    if (att == NULL) {
        return;
    }

    float sens;
    if (full_scale_g == 2.0f) {
        sens = LSM6DS3_2G_SENS;
    } else {
        /* 其他量程按比例换算 */
        sens = (full_scale_g / 2.0f) * LSM6DS3_2G_SENS;
    }

    /* raw int16 → g 重力加速度 */
    float ax = (float)acc_x * sens;
    float ay = (float)acc_y * sens;
    float az = (float)acc_z * sens;

    /* --------------- Roll：绕X轴 atan2(ay , az) ---------------- */
    /* atan2返回弧度，转角度 RAD_TO_DEG */
    att->roll = atan2f(ay, az) * 180.0f / (float)M_PI;

    /* --------------- Pitch：绕Y轴 ----------------
     * 公式：atan2( -ax , sqrt(ay*ay + az*az) )
     * 限制输出 -90 ~ +90度
     */
    float tmp = sqrtf(ay*ay + az*az);
    att->pitch = atan2f(-ax, tmp) * 180.0f / (float)M_PI;

    /* 只用加速度计无法得到航向Yaw */
    att->yaw = 0.0f;
}

void lsm6ds3_attitude_lowpass(const lsm6ds3_attitude_t *new_att,
                              lsm6ds3_attitude_t *out_att,
                              float alpha)
{
    if (new_att == NULL || out_att == NULL) {
        return;
    }
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    out_att->roll  = out_att->roll  * (1.0f - alpha) + new_att->roll  * alpha;
    out_att->pitch = out_att->pitch * (1.0f - alpha) + new_att->pitch * alpha;
    out_att->yaw   = out_att->yaw   * (1.0f - alpha) + new_att->yaw   * alpha;
}
