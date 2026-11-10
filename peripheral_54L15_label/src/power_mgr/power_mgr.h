/*
 * 低功耗状态机 + nPM1300 电源管理
 *
 * 逻辑（按需求实现）：
 *
 *   1) 三轴运动检测中断触发 -> 打开 NFC 配置的「活动窗口」
 *      - 使能 BUCK2（给 DW3000 供电）
 *      - 开始 dw1000_sendpoll() 轮询发送
 *
 *   2) 活动窗口内再次触发运动 -> 刷新活动窗口（不重启发送）
 *      窗口内无运动           -> 停止发送，关闭 BUCK2（DW3000 断电休眠）
 *
 *   3) 连续达到配置的无运动时长 -> 进入深度休眠：
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

/**
 * @brief Callback invoked immediately before the idle task enters
 *        System-ON-IDLE.
 *
 * The callback runs in idle-thread context with interrupts locked. It must
 * not block, sleep, allocate memory, or perform operations that can wait.
 */
typedef void (*power_mgr_idle_callback_t)(void);

/**
 * @brief 初始化电源管理（获取 PMIC/regulator 句柄，打印初始状态）
 * @return 0 成功
 */
int power_mgr_init(void);

/**
 * @brief Register the business callback executed before System-ON-IDLE.
 *
 * Registration is intended to happen once during application startup.
 *
 * @param callback Callback to invoke before the CPU executes WFI.
 * @return 0 on success, -EINVAL if callback is NULL, -EALREADY if already set.
 */
int power_mgr_register_idle_callback(power_mgr_idle_callback_t callback);

/**
 * @brief Wait for a filtered three-axis motion event or timeout.
 *
 * A motion event releases the wait immediately. The timeout keeps the main
 * loop periodic when no motion occurs.
 *
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return 0 if motion woke the caller, -EAGAIN if the timeout expired.
 */
int power_mgr_wait_for_motion(uint32_t timeout_ms);

/**
 * @brief Wake the main loop directly from a motion GPIO ISR.
 *
 * This must not perform I2C or other blocking operations.
 */
void power_mgr_wake_from_isr(void);

/**
 * @brief Serialize sensor I2C work with the low-power transition.
 *
 * Motion work must hold this lock while accessing BMA253.
 */
void power_mgr_sensor_io_lock(void);
void power_mgr_sensor_io_unlock(void);

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
 * GPIO ISR 已负责唤醒主线程；本函数只更新活动窗口和 UWB 请求状态。
 * 本函数会按当前 NFC 配置重启活动窗口定时器。
 */
void power_mgr_notify_motion(void);

/**
 * @brief Apply updated NFC-configured power timeouts immediately.
 *
 * Reschedules the active-window deadline relative to the most recent motion.
 */
void power_mgr_settings_changed(void);

/**
 * @brief 主循环里周期调用（建议 50~200ms 一次）
 *
 * **只负责配置的深度休眠判断**（进 ship mode，会关机）。
 * 活动窗口由内部的 k_work_delayable（延时工作队列）实现，
 * 不依赖这个函数的调用频率。
 */
void power_mgr_sleep_check(void);

/** @brief UWB 当前是否处于工作状态（活动窗口内，可以发轮询） */
bool power_mgr_uwb_is_active(void);

/** @brief 查询运动事件计数（调试用） */
uint32_t power_mgr_get_motion_count(void);

#endif /* POWER_MGR_H */
