/*
 * PCA9635PW 16 通道 I2C LED 驱动（带 RGB 颜色设置）
 *
 * 通道分配：每颗 RGB 灯占用 3 个连续通道，默认 5 颗灯用掉 0..14，
 *           第 15 通道（LED15）留空。实际布线不同的话，只改
 *           pca9635pw.c 里的 led_ch_map[][] 即可。
 *
 * 亮度模型：每个颜色通道 0..255，再乘一个全局亮度系数（0..255），
 *           实际写入 PWM 寄存器的值 = color * brightness / 255。
 */

#ifndef PCA9635PW_H
#define PCA9635PW_H

#include <stdint.h>
#include "ledcolorrgb.h"

/* 板上 RGB 灯数量（改这里要同步改 pca9635pw.c 的 led_ch_map） */
#define PCA9635_LED_COUNT 5

/* 全局亮度范围 */
#define PCA9635_BRIGHTNESS_MAX 255

/** 初始化：OE/KEY 引脚、唤醒、PWM 模式、全灭。返回 0 或负 errno */
int pca9635_init(void);

/** 设置第 idx 颗灯的颜色，r/g/b 为 0..255。返回 0 或负 errno */
int pca9635_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/** 同 pca9635_set_color()，结构体入参 */
int pca9635_set_color_rgb(uint8_t idx, const struct LED_RGB_T *rgb);

/** 所有灯设成同一颜色 */
int pca9635_set_all_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * 所有灯设成同一颜色 + 指定亮度
 *
 * @param rgb        颜色
 * @param brightness 亮度 0..255；会同时更新全局亮度，
 *                   之后的 set_color()/on() 继续沿用这个亮度
 */
int pca9635_set_all_rgb(const struct LED_RGB_T *rgb, uint8_t brightness);

/** 设置全局亮度 0..255，并按新亮度重刷所有通道 */
int pca9635_set_brightness(uint8_t br);

/** 读取当前全局亮度 */
uint8_t pca9635_get_brightness(void);

/** 全部熄灭（PWM 清 0，保留 PWM 模式） */
int pca9635_off(void);

/** 按已记录的颜色重新点亮 */
int pca9635_on(void);

/** 打印 MODE1/MODE2/LEDOUT/PWM 寄存器，调试用 */
int pca9635_dump(void);

/**
 * 兼容旧接口
 * @param RgbLed  颜色
 * @param uiLight 0 = 常亮；非 0 = 每次调用翻转（配合 1Hz 定时调用即闪烁）
 */
void pca9635_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight);

#endif /* PCA9635PW_H */
