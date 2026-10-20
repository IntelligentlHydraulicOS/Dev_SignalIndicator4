/*
 * 低功耗状态机 + nPM1300 电源管理
 *
 * 逻辑（按需求实现）：
 *
 *   1) 三轴运动检测中断触发 -> 打开 10s「活动窗口」
 *      - 使能 BUCK2（给 DW3000 供电）
 *      - 开始 dw1000_sendpoll() 轮询发送
 *
 *   2) 10s 窗口内再次触发运动 -> 窗口刷新为 10s（不重启发送）
 *      10s 内无运动          -> 停止发送，关闭 BUCK2（DW3000 断电休眠）
 *
 *   3) 连续 30s 无任何运动 -> 进入深度休眠：
 *      - 关闭 BUCK2（UWB 断电）
 *      - 进入 nPM1300 ship mode（所有 rail 断电，MCU 自己也失电）
 *      - **只能靠 SHPHLD 按键唤醒**；唤醒后 nPM1300 恢复供电，
 *        MCU 冷启动，从 main() 重新执行
 *
 * 【重要】BUCK1 给 nRF54L15 供电，关掉它 = MCU 自己断电，
 * 所以 ship mode 是「最后一步」，写入后程序不会再往下走。
 * 唤醒必须靠按键，运动检测在深度休眠期间不可能生效。
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef POWER_MGR_H
#define POWER_MGR_H

#include <stdint.h>
#include <stdbool.h>

/* 活动窗口时长：检测到运动后，DW3000 持续发送的时间（可调） */
#define POWER_MGR_ACTIVE_WINDOW_MS    10000U

/* 深度休眠超时：连续无运动多久后进入 ship mode（可调） */
#define POWER_MGR_DEEP_SLEEP_MS       30000U

/**
 * @brief 初始化电源管理（获取 PMIC/regulator 句柄，打印初始状态）
 * @return 0 成功
 */
int power_mgr_init(void);

/**
 * @brief 上电/唤醒后使能 UWB 供电（BUCK2）
 *
 * 在 main() 里、dw3000 初始化之前调用。
 * @return 0 成功
 */
int power_mgr_enable_uwb(void);

/**
 * @brief 通知一次运动事件（由 BMA253 中断的工作队列调用）
 *
 * 调用方（bma253_example.c）已经做过噪声过滤：读两次加速度比较合矢量
 * 变化量，超过 BMA253_MOTION_THRESHOLD_MG 才认为真实运动。
 * 不用 BMA253 的 AOI1_SRC.IA 位 —— 实测该位在这颗芯片上不可靠。
 *
 * 本函数会重启 10s 活动窗口定时器。
 */
void power_mgr_notify_motion(void);

/**
 * @brief 主循环里周期调用（建议 50~200ms 一次）
 *
 * **只负责 30s 深度休眠判断**（进 ship mode，会关机）。
 * 10s 活动窗口由内部的 k_work_delayable（延时工作队列）实现，
 * 不依赖这个函数的调用频率。
 */
void power_mgr_sleep_check(void);

/** @brief UWB 当前是否处于工作状态（活动窗口内，可以发轮询） */
bool power_mgr_uwb_is_active(void);

/** @brief 查询运动事件计数（调试用） */
uint32_t power_mgr_get_motion_count(void);

#endif /* POWER_MGR_H */
