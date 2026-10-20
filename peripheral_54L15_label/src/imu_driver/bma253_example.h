#ifndef BMA253_EXAMPLE_H
#define BMA253_EXAMPLE_H


/**
 * @brief 扫描总线并初始化 BMA253
 * @return 0 成功
 */
int bma253_example_init(void);

/** @brief 轮询一次并打印三轴数据 */
void bma253_example_poll(void);

/**
 * @brief 配置 BMA253 运动检测中断（INT1 -> P0.00）
 *
 * 阈值取自宏 BMA253_MOTION_THRESHOLD_MG（定义在 bma253_i2c_driver.h，
 * 单位 mg）。需要调整灵敏度时改那个宏即可，不用动函数调用。
 *
 * @return 0 成功
 */
int bma253_motion_example_setup(void);

/**
 * @brief 查询已发生的运动事件次数
 *
 * 运动事件在 GPIO 中断里只提交 work，实际的 I2C 读取与打印在
 * 工作队列线程完成（中断里不能调 I2C / printk）。
 */
uint32_t bma253_motion_get_count(void);

/**
 * @brief 转储 BMA253 关键寄存器（诊断用）
 *
 * 用来确认量程/ODR/电源模式等配置是否真的写进了芯片。
 * 在 bma253_example_init() 成功后会自动调用一次。
 */
void bma253_dump_regs(void);

/**
 * @brief 只读巡检配置寄存器（**不写入任何寄存器**）
 *
 * 曾经用"写探针值再读回"的方式判断地址是否存在，结果写坏了
 * 0x1B~0x1F（数据输出 / softreset / 自测），导致输出寄存器冻结在
 * 饱和值。因此这里一律只读。
 *
 * 要判断寄存器是否真实存在，请改用 bma253_check_alive() 看数据活性。
 */
void bma253_probe_ctrl_regs(void);

/**
 * @brief 传感器活性检查：输出寄存器是否真的在随姿态更新
 *
 * 连续采样若三轴**完全不变**，说明输出被冻结（多半是被写坏了），
 * 需要完全断电 10 秒重新上电。
 *
 * @return 0 数据在变化（正常）；-EIO 疑似冻结
 */
int bma253_check_alive(struct bma253_data *dev, uint8_t samples);

/**
 * @brief 打印一组"四视图"原始数据，用于确定数据格式
 *
 * 同时对同一份 6 字节给出 4 种解释：
 *   16 位有符号小端 / >>3 / >>4 / >>5（12、13 位左对齐）
 * 并给出按当前量程换算的 g 值。
 *
 * 判读方法：把板子分别"平放"和"竖直立起"，哪种解释能让某一轴
 * 稳定接近 ±1g、另两轴接近 0，那种就是正确的格式。
 */
void bma253_print_raw_views(void);

/**
 * @brief 数据就绪中断方式读取（可选）
 *
 * 需要把 BMA253 的 INT1 接到一个 GPIO 并在 overlay 里配置中断，
 * 然后在中断回调里调用 bma253_read()。
 */
void bma253_example_int_setup(void);

#endif /* BMA253_EXAMPLE_H */