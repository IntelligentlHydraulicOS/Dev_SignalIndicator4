/*
 * 低功耗状态机实现（见 power_mgr.h 的说明）
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/sys/printk.h>

#include "power_mgr.h"
#include "dw3000_node.h"
#include "dw3000_hw.h"

/*
 * nRF54L15 核内复位原因寄存器 RESETREAS。
 *
 * 打印这个非常重要：实测发现进 ship mode 时 **BUCK1/BUCK2 并没有断电**，
 * 但 MCU 却重启了 —— 说明不是 "ship mode 退出" 造成的关机再开机，
 * 而是 PMIC/别的东西复位了 MCU。RESETREAS 能直接给出复位源：
 *   bit8  OFF       从 System OFF 唤醒
 *   bit5  CTRLAPPIN 引脚复位（需读 PIN 字段看是哪个脚）
 *   bit6  SREQ      软件复位请求
 *   bit0  RESETPIN  nRESET 引脚
 *
 * 安全别名基址 0x5010E000，RESETREAS 偏移 0x600（MDK: NRF_RESET_S_BASE）。
 */
#define NRF54L_RESETREAS   (*(volatile uint32_t *)0x5010E600UL)

static void print_cpu_reset_reason(void)
{
	uint32_t r = NRF54L_RESETREAS;
	uint32_t pin = (r >> 16) & 0x1FU;

	printk("power_mgr: 核内复位原因 RESETREAS(0x%08X)%s%s%s%s%s%s%s%s%s%s\n",
	       (unsigned)r,
	       (r & BIT(0)) ? " RESETPIN" : "",
	       (r & BIT(1)) ? " DOG0" : "",
	       (r & BIT(3)) ? " CTRLAPSOFT" : "",
	       (r & BIT(4)) ? " CTRLAPHARD" : "",
	       (r & BIT(5)) ? " CTRLAPPIN" : "",
	       (r & BIT(6)) ? " SREQ" : "",
	       (r & BIT(7)) ? " LOCKUP" : "",
	       (r & BIT(8)) ? " OFF(从SystemOFF唤醒)" : "",
	       (r & BIT(11)) ? " GRTC" : "",
	       (r == 0U) ? " 无记录(POR)" : "");

	if (r & BIT(5)) {
		printk("power_mgr:   CTRLAPPIN 复位引脚号 = P%d.%02u\n",
		       (int)(pin / 32U), (unsigned)(pin % 32U));
	}
}

/* PMIC 设备（overlay 里的 npm1300_ek_pmic） */
#define PMIC_NODE   DT_NODELABEL(npm1300_ek_pmic)

/* BUCK1 = MCU 供电。BUCK2 保持常开，原因见下方说明。 */
#define BUCK1_NODE  DT_NODELABEL(buck1)
#define BUCK2_NODE  DT_NODELABEL(buck2)

/*
 * 掉电检测：nPM1300 的 SCRATCH0/SCRATCH1（ERRLOG 实例 offset 0x1/0x2）。
 * 手册 7.8.1.2/7.8.1.3 原文："scratch register, only cleared by POR"。
 *
 * 只有**真正的 POR（掉电重启）**才会把它们清零；看门狗/软件复位/
 * ship 退出等都不会。所以：
 *   进 ship 前写入魔数 -> 重启后读回
 *   值还在 == 电源从未断过（不是 POR，是普通复位）
 *   值丢了 == 真的掉电了
 * 这比用示波器看 BUCK 更可靠，不受时基/触发设置影响。
 */
#define NPM13XX_ERRLOG_SCRATCH0   0x01U
#define POWER_MGR_SCRATCH_MAGIC   0x5AU

/*
 * DW3000 的 PD（Power Down）引脚 —— 这才是控制 UWB 低功耗的正确手段。
 *
 * 【为什么不关 BUCK2】
 * 实测发现关掉 BUCK2 之后 nPM1300 在 I2C22 上就再也读不到了
 * （ship 命令写入返回 -116 ETIMEDOUT），说明 BUCK2 同时给 I2C22 的
 * 上拉/电平转换供电。一旦断掉 BUCK2：
 *   1) PMIC 失联，连"进 ship mode 关总电源"都做不到；
 *   2) 整个低功耗逻辑自相矛盾。
 * 而 PD 引脚是 DW3000 原生的掉电控制，效果一样（µA 级），
 * 且不影响 PMIC 通信。
 *
 * overlay 里 dw3000_pd = <&gpio2 0 GPIO_ACTIVE_HIGH>，
 * 所以逻辑 1 = 掉电，逻辑 0 = 正常工作。
 */
#define DW3000_PD_NODE  DT_NODELABEL(dw3000_pd)

/* nPM1300 ship 寄存器（驱动内部定义，这里按同样地址使用） */
#define NPM13XX_SHIP_BASE        0x0BU
#define NPM13XX_SHIP_OFFSET_SHIP 0x02U

/* nPM1300 VBUS 实例：STATUS 寄存器 bit0 = VBUS PRESENT */
#define NPM13XX_VBUS_BASE        0x02U
#define NPM13XX_VBUS_OFFSET_STAT 0x07U

/* nPM1300 SHIP 实例：SHPHLDSTATUS bit0 = SHPHLD 引脚电平（0=低 1=高） */
#define NPM13XX_SHIP_OFFSET_SHPHLDSTATUS 0x05U

/* nPM1300 ERRLOG 实例：RSTCAUSE = 复位原因（手册 7.8.1.4）
 * 位映射 A(Bit7) .. H(Bit0)：
 *   B7 SHIPMODEEXIT      由 ship mode 退出引起（正常唤醒）
 *   B6 BOOTMONITORTIMEOUT
 *   B5 WATCHDOGTIMEOUT
 *   B4 LONGPRESSTIMEOUT  SHPHLD 长按复位
 *   B3 THERMALSHUTDOWN
 *   B2 VSYSLOW
 *   B1/B0 软件复位等
 * 该寄存器只在 POR 或 TASKCLRERRLOG 时清除，所以启动时读一次即可。 */
#define NPM13XX_ERRLOG_BASE       0x0EU
#define NPM13XX_ERRLOG_RSTCAUSE   0x03U

static const struct device *const pmic = DEVICE_DT_GET(PMIC_NODE);
static const struct device *const buck1 = DEVICE_DT_GET(BUCK1_NODE);
static const struct device *const buck2 = DEVICE_DT_GET(BUCK2_NODE);
static const struct gpio_dt_spec dw3000_pd = GPIO_DT_SPEC_GET(DW3000_PD_NODE, gpios);

/*
 * 【关键】ship mode 必须调用在 `regulators` 容器设备上，不是 pmic@6b！
 *
 * regulator_npm13xx.c 的定义（L815-817）：
 *     DEVICE_DT_INST_DEFINE(n, ..., &parent_api);
 * 这个 parent_api（含 ship_mode）挂在
 *     compatible = "nordic,npm1300-regulator"
 * 的 `regulators` 容器节点上。
 *
 * 而 PMIC 本身的定义（mfd_npm13xx.c）最后那个参数是 NULL：
 *     DEVICE_DT_INST_DEFINE(n, mfd_npm13xx_init, ..., NULL);
 *     ↑ api == NULL
 *
 * 所以 regulator_parent_ship_mode(pmic) 会执行：
 *     api = (regulator_parent_driver_api *)pmic->api;   // api = NULL
 *     if (api->ship_mode == NULL)                       // 解引用 NULL -> 硬故障！
 *
 * 之前的现象正是这个：函数内崩溃 -> 检查点B 没打印 ->
 * Zephyr 致命错误 -> sys_reboot -> RESETREAS=0x40(SREQ)，
 * 而 PMIC 的 RSTCAUSE=0x00（它从没收到 ship 命令）。
 *
 * `regulators` 节点没有 label，所以用 buck1 的父设备取。
 */
#define REGULATORS_NODE  DT_PARENT(DT_NODELABEL(buck1))

static const struct device *const regulators_dev = DEVICE_DT_GET(REGULATORS_NODE);

static bool uwb_powered = false;           /* UWB 是否处于工作状态（PD 已释放） */
static bool uwb_gpio_inited = false;           /* GPIO/中断回调是否已注册（只注册一次） */
static int64_t last_motion_ms = 0;
static uint32_t motion_count = 0;

/* 10s 活动窗口：延时工作队列。
 * 有运动就 reschedule 成 10s 后；连续 10s 无运动则超时处理运行。 */
static struct k_work_delayable window_work;

/* ------------------------- 内部工具 ------------------------- */

/**
 * 打印并解读 nPM1300 的复位原因（RSTCAUSE）。
 *
 * 用来判断"上次关机后为什么又启动了"：
 *   SHIPMODEEXIT=1    -> 正常从 ship mode 唤醒（按键或 VBUS）
 *   LONGPRESSTIMEOUT=1-> SHPHLD 长按复位
 *   两者都为 0 且看到本次是冷启动 -> 可能是掉电/POR
 */
static void print_reset_cause(void)
{
	uint8_t c = 0U;
	int ret;

	ret = mfd_npm13xx_reg_read(pmic, NPM13XX_ERRLOG_BASE,
				   NPM13XX_ERRLOG_RSTCAUSE, &c);
	if (ret < 0) {
		printk("power_mgr: 读 RSTCAUSE 失败 %d\n", ret);
		return;
	}

	printk("power_mgr: 复位原因 RSTCAUSE(0x%02X)%s%s%s%s%s%s%s\n",
	       c,
	       (c & BIT(7)) ? " SHIPMODEEXIT(从ship唤醒)" : "",
	       (c & BIT(6)) ? " BOOTMONITOR" : "",
	       (c & BIT(5)) ? " WATCHDOG" : "",
	       (c & BIT(4)) ? " LONGPRESS(长按SHPHLD)" : "",
	       (c & BIT(3)) ? " THERMAL" : "",
	       (c & BIT(2)) ? " VSYSLOW" : "",
	       (c == 0U) ? " POR/首次上电(无记录)" : "");
}

static void uwb_power_off(void);
static int uwb_power_on(void);

/*
 * UWB 上下电请求标志。
 *
 * 【为什么必须这样】
 * uwb_power_on()/uwb_power_off() 会走 DW3000 的完整初始化链
 * （dwt_probe / spi / dwt_configure / 多次 printk），栈开销远大于
 * CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE（本工程 = 2048 字节）。
 * 如果直接在系统工作队列线程里调用，会**栈溢出**，踩坏返回地址后
 * 跳到野地址，表现为：
 *     ***** BUS FAULT *****
 *       Instruction bus error
 *     Faulting instruction address: 0x08f38048
 *
 * 所以工作队列/延时的回调**只置标志**，真正的上下电放到主线程
 * （CONFIG_MAIN_STACK_SIZE = 3584）的 power_mgr_sleep_check() 里执行。
 */
static volatile bool uwb_on_req;
static volatile bool uwb_off_req;

/**
 * 10s 活动窗口超时处理
 *
 * 用 k_work_delayable 而不是 k_timer：这个回调在**工作队列线程**执行，
 * k_timer 的 expiry 回调在系统时钟中断里跑，做 I2C/GPIO 会触发内核断言
 * （sem.c:136）。
 *
 * 注意：即使在工作队列线程，也**只置标志**，不做 DW3000 上下电。
 */
static void window_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uwb_off_req = true;      /* 实际的关断由主线程执行 */
}

/** 重启（推迟）活动窗口：有运动时调用 */
static void window_restart(void)
{
	(void)k_work_reschedule(&window_work,
				K_MSEC(POWER_MGR_ACTIVE_WINDOW_MS));
}

/**
 * 唤醒 UWB。
 * @return 0 成功；负值失败（失败时不会把 uwb_powered 置真，
 *         避免主循环对未上电/未初始化的 DW3000 发 SPI 事务）
 */
static int uwb_power_on(void)
{
	int ret;

	if (uwb_powered) {
		return 0;
	}

	/* 【PD 极性】必须与驱动 dw3000_hw.c 的 dw3000_hw_init() 保持一致：
	 *     gpio_pin_configure_dt(&conf.gpio_pd, GPIO_OUTPUT_ACTIVE);
	 * 该函数在原来的启动流程里先执行，之后 SPI 通信正常，
	 * 说明 **ACTIVE 电平 = DW3000 正常工作**（不是掉电）。
	 * 之前写成 set_dt(...,0) 是反的：上电时反而拉成了掉电电平。 */
	ret = gpio_pin_set_dt(&dw3000_pd, 1);
	if (ret < 0) {
		printk("power_mgr: 释放 PD 失败 %d，UWB 未唤醒\n", ret);
		return ret;
	}
	k_msleep(5);

	/* GPIO / 中断回调只注册一次。
	 *
	 * dw3000_hw_node_init() 内部会执行 gpio_add_callback()，
	 * 用**同一个 gpio_cb 结构体**重复调用会破坏回调链表，
	 * 中断触发时跳到野地址（之前的 BUS FAULT）。
	 * 所以这个函数全程只调一次。 */
	if (!uwb_gpio_inited) {
		dw3000_hw_node_init();
		uwb_gpio_inited = true;
	}

	/* 【每次上电都必须重跑】
	 *
	 * PD 拉低是彻底掉电，DW3000 内部的 PLL/信道/前导码等全部配置丢失。
	 * 只释放 PD 而不重新配置，dw1000_sendpoll() 发出去的帧是无效的。
	 *
	 * dw3000_node_run() 内部包含完整恢复序列：
	 *   dwt_probe -> dw3000_hw_reset -> dwt_initialise -> dwt_configure
	 * 正好是掉电后恢复所需，且它**不**重复注册 GPIO 回调，可以安全重入。 */
	dw3000_node_run();

	uwb_powered = true;
	printk("power_mgr: UWB 已唤醒并就绪（已重新配置）\n");
	return 0;
}

static void uwb_power_off(void)
{
	if (!uwb_powered) {
		return;
	}

	/* 只关中断，**不调用 dw3000_hw_fini()**。
	 *
	 * dw3000_hw_fini() 会把 IRQ/RESET/WAKEUP/sync 全部配成
	 * GPIO_DISCONNECTED，并把 SPI 也 fini 掉。之后再次上电时，
	 * 只调 dw3000_node_run() 就无法恢复（引脚已断开）。
	 * 保留引脚配置，只切 DW3000 的电即可。 */
	dw3000_hw_interrupt_disable();
	k_msleep(2);

	/* 拉低 PD 让 DW3000 掉电（µA 级）。
	 *
	 * 【极性】与 dw3000_hw_init() 的 GPIO_OUTPUT_ACTIVE 相反：
	 * ACTIVE = 正常工作，所以拉成非 ACTIVE 才是掉电。
	 *
	 * BUCK2 保持供电 —— 它同时给 I2C22 上拉供电，断了 PMIC 会失联。 */
	(void)gpio_pin_set_dt(&dw3000_pd, 0);
	uwb_powered = false;

	printk("power_mgr: UWB 已进入低功耗（PD 拉低）\n");
}

/**
 * 进入深度休眠：关闭 UWB 供电并让 nPM1300 进入 ship mode。
 *
 * 【重要】这里**绝对不能**用 irq_lock()！
 * MFD/regulator 驱动写寄存器内部要等 I2C 信号量，需要上下文切换；
 * 持锁时切换会触发内核断言崩溃（kswap.h:217
 * "Context switching while holding lock!"）。
 *
 * ship mode 生效后 BUCK1 断电，MCU 立即停止，所以正常情况下不返回。
 * 若仍在运行，说明 PMIC 没有执行 ship 命令。
 */
static void enter_deep_sleep(void)
{
	int ret;
	int64_t deadline;

	printk("power_mgr: %u s 无运动，进入深度休眠（ship mode）\n",
	       (unsigned)(POWER_MGR_DEEP_SLEEP_MS / 1000U));
	printk("power_mgr: 按 SHPHLD 按键可唤醒（冷启动）\n");

	/* 1) UWB 先干净下电 */
	uwb_power_off();

	/* 2) 【手册 7.4 节前置条件】检查 VBUS 是否已断开并放电。
	 *
	 * 手册原文："When VBUS is not present, the device enters Ship mode
	 * immediately. The host software must wait until EVENTSVBUSIN0SET to
	 * ensure VBUS is disconnected and discharged before writing to the
	 * register."
	 *
	 * 也就是说：**VBUS 存在时 ship mode 不会立即进入**。
	 * 调试时插着 USB 取串口日志，VBUS 就一定在。
	 * 这里显式读一次并提示，避免"写了命令却没关机"看起来像 bug。 */
	{
		uint8_t vst = 0U;
		uint8_t shp = 0U;

		if (mfd_npm13xx_reg_read(pmic, NPM13XX_VBUS_BASE,
					NPM13XX_VBUS_OFFSET_STAT, &vst) == 0) {
			bool present = (vst & 0x01U) != 0U;   /* STATUS_PRESENT */

			printk("power_mgr: VBUS 状态(0x%02X) = %s\n", vst,
			       present ? "已接入" : "未接入");
			if (present) {
				printk("!! power_mgr: VBUS 存在，按手册 ship mode 不会立即进入。\n"
				       "!! 请拔掉 USB（断开 VBUS）后再试。\n");
			}
		}

		/* SHPHLD 引脚电平：bit0 = 1 表示高电平（按键未按）。
		 * 若这里读到 0，说明按键被按住或引脚被拉低，
		 * 那么进 ship mode 后会在 96ms 内立刻退出。 */
		if (mfd_npm13xx_reg_read(pmic, NPM13XX_SHIP_BASE,
					 NPM13XX_SHIP_OFFSET_SHPHLDSTATUS,
					 &shp) == 0) {
			printk("power_mgr: SHPHLD 引脚状态(0x%02X) = %s\n", shp,
			       (shp & 0x01U) ? "高（按键未按，正常）"
					     : "低!!（按键被按住或引脚被拉低）");
		}
	}

	/* 3) 让串口日志先发出去 */
	k_msleep(50);

	/* 3.5) 写入 SCRATCH 魔数：它是"仅 POR 清零"的寄存器，
	 *      下次启动读回来就能判断这次 ship 有没有真的掉电。 */
	if (mfd_npm13xx_reg_write(pmic, NPM13XX_ERRLOG_BASE,
				  NPM13XX_ERRLOG_SCRATCH0,
				  POWER_MGR_SCRATCH_MAGIC) == 0) {
		printk("power_mgr: 已写 SCRATCH0=0x%02X（用于判断是否真掉电）\n",
		       (unsigned)POWER_MGR_SCRATCH_MAGIC);
	}

	// printk("power_mgr: [检查点A] 即将调用 regulator_parent_ship_mode(%s)\n",
	//        regulators_dev->name);

	/* 4) 进入 ship mode。
	 *
	 * 【设备选择很关键】必须传 `regulators` 容器设备（buck1 的父节点），
	 * 不能传 pmic@6b —— 后者的 dev->api 是 NULL，会解引用空指针崩溃。
	 *
	 * 优先用官方 API（内部就是写 SHIP_BASE(0x0B) + TASKENTERSHIPMODE(0x02)）；
	 * 若 API 不可用（-ENOSYS）则直接写寄存器作为回退。
	 * 注意：不能持锁调用（内部要走 I2C，需要上下文切换）。 */
	if (!device_is_ready(regulators_dev)) {
		printk("power_mgr: regulators 设备未就绪，直接写寄存器\n");
		ret = -ENOSYS;
	} else {
		ret = regulator_parent_ship_mode(regulators_dev);
	}

	// printk("power_mgr: [检查点B] ship_mode 返回 %d\n", ret);

	if (ret == -ENOSYS) {
		printk("power_mgr: 无 ship_mode API，改用直接写寄存器\n");
		ret = mfd_npm13xx_reg_write(pmic, NPM13XX_SHIP_BASE,
					    NPM13XX_SHIP_OFFSET_SHIP, 1U);
	}
	if (ret < 0) {
		printk("power_mgr: ship 命令失败 %d\n", ret);
	}

	// printk("power_mgr: [检查点C] 进入 5 秒等待循环\n");

	/* 5) 最多等 5 秒确认是否真的断电。
	 *    正常情况下跑不到这里：BUCK1 已断，MCU 停机。
	 *
	 *    加心跳打印：如果"进 ship 后立刻重启"，心跳的次数能精确
	 *    告诉我们 MCU 在 ship 命令之后还活了多久。
	 *      - 心跳没出现         -> 在 regulator_parent_ship_mode() 里就复位了
	 *      - 心跳 1~4 次        -> 在 5 秒等待中间被复位
	 *      - 心跳 50 次 + 警告  -> ship 完全没生效 */
	if (0)
	{
		int beat = 0;

		deadline = k_uptime_get() + 5000;
		while (k_uptime_get() < deadline) {
			k_msleep(100);
			beat++;
			if ((beat % 5) == 0) {
				printk("power_mgr: [心跳 %d] ship 命令后已过 %d ms，仍在运行\n",
				       beat, beat * 100);
			}
		}
	}

	static bool warned = false;
	if (!warned) {
		warned = true;
		printk("!! power_mgr: ship mode 未生效，仍在运行。\n");
		printk("!! 按手册 7.4 节逐项确认：\n");
		printk("!!   1) VBUS 是否已断开（见上面的 VBUS 状态打印）\n");
		printk("!!   2) SHPHLDCONFIG/TASKSHPHLDCFGSTROBE 是否已配置\n");
		printk("!!   3) SHPHLD 按键是否接好（退出 ship 要靠它拉低）\n");
	}
}

/* ------------------------- 对外接口 ------------------------- */

int power_mgr_init(void)
{
	int ret;

	if (!device_is_ready(pmic)) {
		printk("power_mgr: PMIC 未就绪（检查 I2C22 与 BUCK2 是否给上拉供电）\n");
		return -ENODEV;
	}
	if (!device_is_ready(buck1)) {
		printk("power_mgr: BUCK1 regulator 未就绪\n");
		return -ENODEV;
	}
	if (!device_is_ready(buck2)) {
		printk("power_mgr: BUCK2 regulator 未就绪\n");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&dw3000_pd)) {
		printk("power_mgr: DW3000 PD 引脚未就绪\n");
		return -ENODEV;
	}

	last_motion_ms = k_uptime_get();

	/* 初始化 10s 活动窗口（延时工作队列）。
	 * 它在工作队列线程执行，可以直接做 I2C，不需要额外的转发层。 */
	k_work_init_delayable(&window_work, window_work_handler);

	/* 配置 PD 引脚为输出，并置为【非 ACTIVE = 掉电】进入待机。
	 *
	 * 极性与 dw3000_hw_init() 相反：那里用 GPIO_OUTPUT_ACTIVE 表示
	 * "正常工作"，所以 ACTIVE = 通电、非 ACTIVE = 掉电。
	 * （BUCK2 保持供电，它还给 I2C22 上拉供电。） */
	ret = gpio_pin_configure_dt(&dw3000_pd, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("power_mgr: PD 引脚配置失败 %d\n", ret);
		return ret;
	}
	(void)gpio_pin_set_dt(&dw3000_pd, 0);
	uwb_powered = false;

	printk("power_mgr: 初始 BUCK1(MCU)=%s BUCK2(UWB供电)=%s，UWB 已置待机\n",
	       regulator_is_enabled(buck1) ? "开" : "关",
	       regulator_is_enabled(buck2) ? "开" : "关");

	/* 打印本次启动的复位原因，两边都看：
	 *   - PMIC 的 RSTCAUSE  -> 是否由 ship mode 退出引起
	 *   - MCU 的 RESETREAS  -> 核内复位源（引脚/看门狗/软件/SystemOFF）
	 *   - SCRATCH0 魔数     -> 电源到底有没有真的断过
	 * 三者结合能定位到底是谁把系统重启的。 */
	print_reset_cause();
	print_cpu_reset_reason();

	{
		uint8_t scratch = 0U;

		if (mfd_npm13xx_reg_read(pmic, NPM13XX_ERRLOG_BASE,
					 NPM13XX_ERRLOG_SCRATCH0,
					 &scratch) == 0) {
			if (scratch == POWER_MGR_SCRATCH_MAGIC) {
				printk("power_mgr: SCRATCH0=0x%02X 魔数还在 -> "
				       "【没有发生 POR】电源从未断过，是普通复位\n",
				       scratch);
			} else {
				printk("power_mgr: SCRATCH0=0x%02X 魔数已丢 -> "
				       "【发生了 POR】确实掉过电\n", scratch);
			}
			/* 清掉，避免下次误判 */
			(void)mfd_npm13xx_reg_write(pmic, NPM13XX_ERRLOG_BASE,
						    NPM13XX_ERRLOG_SCRATCH0, 0U);
		}
	}

	return 0;
}

int power_mgr_enable_uwb(void)
{
	uwb_power_on();
	return uwb_powered ? 0 : -EIO;
}

void power_mgr_notify_motion(void)
{
	motion_count++;
	last_motion_ms = k_uptime_get();

	/* 【重要】这里**不直接调用 uwb_power_on()**。
	 * 本函数由 motion_work_handler() 在系统工作队列线程里调用，
	 * 而该线程栈只有 2048 字节，跑不动 DW3000 的初始化链（会栈溢出
	 * 导致 BUS FAULT）。所以只置请求标志，由主线程执行。 */
	if (!uwb_powered) {
		printk("power_mgr: 运动触发（第 %u 次），打开 %u ms 活动窗口\n",
		       (unsigned)motion_count, (unsigned)POWER_MGR_ACTIVE_WINDOW_MS);
	} else {
		printk("power_mgr: 运动触发（第 %u 次），窗口刷新为 %u ms\n",
		       (unsigned)motion_count, (unsigned)POWER_MGR_ACTIVE_WINDOW_MS);
	}

	uwb_off_req = false;     /* 新运动取消待处理的关断 */
	uwb_on_req = true;       /* 请求主线程上电（若已上电则空操作） */

	/* 把 10s 窗口往后推。
	 * 每来一次运动就 reschedule 一次，天然实现"10s 内继续触发则刷新窗口"：
	 *   - 窗口内不断有运动 -> 一直往后推，UWB 持续发送
	 *   - 连续 10s 无运动   -> 延时工作到期，置 uwb_off_req
	 * 计时从"运动发生时刻"开始，不受 DW3000 初始化耗时影响。 */
	window_restart();
}

void power_mgr_sleep_check(void)
{
	int64_t now = k_uptime_get();

	/* ---- 先执行 UWB 上下电（主线程，栈 3584，足够 DW3000 初始化）---- */
	if (uwb_on_req) {
		uwb_on_req = false;
		(void)uwb_power_on();
	}
	if (uwb_off_req) {
		uwb_off_req = false;
		if (uwb_powered) {
			printk("power_mgr: %u ms 活动窗口到期，UWB 进入低功耗\n",
			       (unsigned)POWER_MGR_ACTIVE_WINDOW_MS);
			uwb_power_off();
		}
	}

	/* ---- 30s 无运动 -> 进 ship mode（关机，正常情况下不返回）---- */
	if ((now - last_motion_ms) >= (int64_t)POWER_MGR_DEEP_SLEEP_MS) {
		/* 先取消未到期的窗口延时工作，避免关机过程中它又触发关 UWB */
		last_motion_ms = now;
		(void)k_work_cancel_delayable(&window_work);
		enter_deep_sleep();
	}
}

bool power_mgr_uwb_is_active(void)
{
	return uwb_powered;
}

uint32_t power_mgr_get_motion_count(void)
{
	return motion_count;
}
