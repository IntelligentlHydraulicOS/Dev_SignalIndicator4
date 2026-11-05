/*
 * BMA253 使用示例（Zephyr / nRF Connect SDK）
 *
 * 本示例对应你工程里已有的设备树配置：
 *
 *   overlay:
 *     i2c_bma253: &i2c30 {
 *         status = "okay";
 *         pinctrl-0 = <&i2c30_default>;   // SDA=P1.10, SCL=P1.11
 *         pinctrl-1 = <&i2c30_sleep>;
 *         pinctrl-names = "default", "sleep";
 *     };
 *
 *   prj.conf:
 *     CONFIG_I2C=y
 *
 *   CMakeLists.txt（加入 target_sources(app PRIVATE ...) 列表）：
 *     src/imu_driver/bma253_i2c_driver.c
 *     src/imu_driver/bma253_example.c    // 不想用示例可只加驱动那一个
 *
 * 注意：i2c30 与 uart30 / spi30 共用 0x104000 这一个硬件实例（三选一），
 *      所以不要再同时使能 uart30 或 spi30。
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include <math.h>

#include "bma253_i2c_driver.h"
#include "bma253_example.h"

/* I2C 总线：对应 overlay 里的 i2c_bma253: &i2c30 */
#define BMA253_BUS_NODE  DT_NODELABEL(hxy_bma253)

static struct i2c_dt_spec pca9635_spec = I2C_DT_SPEC_GET(BMA253_BUS_NODE);
static struct bma253_data bma253;

/* 被忽略的伪中断累计次数（电气干扰） */
static uint32_t noise_irq_count;

static uint16_t uipitch;
static uint16_t uiroll;

/**
 * @brief 扫描总线并初始化 BMA253
 * @return 0 成功
 */
int bma253_example_init(void)
{
	int ret;

	if (!device_is_ready(pca9635_spec.bus)) {
		printk("I2C 总线未就绪\n");
		return -ENODEV;
	}

	/* 第一步：扫总线，确认从机地址。
	 * SDO 悬空/接逻辑高 -> 0x19；SDO 接逻辑低 -> 0x18。 */
	for (uint8_t a = 0x08U; a <= 0x77U; a++) {
		uint8_t id = 0U;

		if (i2c_reg_read_byte(pca9635_spec.bus, a, BMA253_REG_WHO_AM_I, &id) == 0) {
			printk("发现 I2C 器件:addr=0x%02X, WHO_AM_I(0x0F)=0x%02X\n", a, id);
		}
	}

	/* 第二步：初始化，量程 ±4g，ODR 100Hz */
	ret = bma253_init(&bma253, pca9635_spec.bus, BMA253_I2C_ADDR_SDO_HIGH,
			  BMA253_RANGE_2G, BMA253_ODR_12_5HZ);
	if (ret < 0) {
		printk("BMA253 初始化失败：%d\n", ret);
		return ret;
	}

	/* 可选：数字低通滤波器，1 / 4 / 8 / 16 阶 */
	(void)bma253_set_dlpf(&bma253, BMA253_DLPF_4TH);

	// /* ---------- 运动检测中断 ----------
	//  * BMA253 INT1 (P0.00) 高有效推挽输出，检测到运动时拉高。
	//  * 阈值用宏 BMA253_MOTION_THRESHOLD_MG（单位 mg），后续自行调整。
	//  * 传 0 表示使用宏里的默认值。 */
	// ret = bma253_motion_example_setup();
	// if (ret < 0) {
	// 	printk("运动检测初始化失败：%d\n", ret);
	// }

	printk("BMA253 就绪\n");

#if 0
	/* ---------- 先确认传感器活着 ----------
	 * 若输出寄存器被冻结，三个姿态会读到完全相同的值，
	 * 此时校准毫无意义，先尝试恢复序列。 */
	bma253_dump_regs();

	if (bma253_check_alive(&bma253, 8U) < 0) {
		printk("!! 检测到输出冻结，尝试恢复序列...\n");

		if (bma253_recover_output(&bma253, BMA253_ODR_100HZ) < 0) {
			printk("!! 恢复失败：请【彻底断电 30 秒】后重新上电；\n"
			       "!! 若仍冻结，传感器很可能已损坏，需要更换。\n"
			       "!! 已跳过校准。\n");
			return -EIO;
		}

		/* 恢复成功，再确认一次 */
		if (bma253_check_alive(&bma253, 8U) < 0) {
			printk("!! 恢复后仍然冻结，已跳过校准\n");
			return -EIO;
		}
	}

	/* ---------- 三点姿态校准 ----------
	 * 每个姿态保持静止，依次记录。
	 *
	 * 姿态定义（尽量做准，这决定校准是否正确）：
	 *   1) 板子【平放】在桌面上（芯片正面朝上）
	 *   2) 板子【翻过来】平放（芯片正面朝下）
	 *   3) 板子【长边立起】站着（此时某一水平轴竖直朝上）
	 */
	printk("\n=== 三点姿态校准 ===\n");
	printk(">>> 姿态1：把板子【平放】在桌面上，保持静止 <<<\n");
	k_msleep(8000);
	(void)bma253_calib_record(&bma253, 24U);

	printk(">>> 姿态2：把板子【翻过来】平放（正面朝下），保持静止 <<<\n");
	k_msleep(10000);
	(void)bma253_calib_record(&bma253, 24U);

	printk(">>> 姿态3：把板子【长边立起】站着，保持静止 <<<\n");
	k_msleep(10000);
	(void)bma253_calib_record(&bma253, 24U);

	(void)bma253_calib_finish(&bma253);

	/* ---------- 诊断（全部只读，不写任何寄存器） ---------- */
	(void)bma253_probe_ctrl_regs();
#endif
	return 0;
}

/**
 * @brief 转储 BMA253 关键寄存器（诊断用，全部只读）
 *
 * 按手册第 11 节寄存器列表，期望值（±2g / 100Hz / 三轴使能）：
 *   0x0F WHO_AM_I  : 0x11
 *   0x1F CTRL_REG0 : BDU=0, FS1:FS0=00(±2g) -> 0x00（±4g 时为 0x10）
 *   0x20 CTRL_REG1 : ODR=0101(100Hz)<<4 | Xen|Yen|Zen = 0x57
 *   0x27 DRDY       : 数据就绪时 bit7 = 1
 *   0x28..0x2D     : 三轴数据（只读）
 */
void bma253_dump_regs(void)
{
	static const uint8_t regs[] = {
		/* 只列出手册第 11 节真实存在的地址，0x10~0x1E 不存在 */
		0x0F, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
		0x27, 0x2E, 0x2F, 0x38, 0x61, 0x68, 0x6F, 0x70
	};
	uint8_t v;

	printk("--- BMA253 寄存器转储（按手册 §11 地址表）---\n");
	for (size_t i = 0U; i < ARRAY_SIZE(regs); i++) {
		v = 0U;
		if (bma253_reg_read(&bma253, regs[i], &v) == 0) {
			printk("  reg[0x%02X] = 0x%02X\n", regs[i], v);
		} else {
			printk("  reg[0x%02X] = <读失败>\n", regs[i]);
		}
	}

	/* 把关键配置寄存器按位域解读出来，直接看出量程/BDU/ODR */
	{
		uint8_t c0 = 0U, c1 = 0U;

		(void)bma253_reg_read(&bma253, BMA253_REG_CTRL_REG0, &c0);
		(void)bma253_reg_read(&bma253, BMA253_REG_CTRL_REG1, &c1);

		printk("  CTRL_REG0(0x1F)=0x%02X -> BDU=%u BLE=%u FS=%u DLPF0=%u\n",
		       c0, (unsigned)((c0 >> 7) & 1U), (unsigned)((c0 >> 6) & 1U),
		       (unsigned)((c0 >> 4) & 3U), (unsigned)((c0 >> 3) & 1U));
		printk("  CTRL_REG1(0x20)=0x%02X -> ODR=%u LPen=%u Xen=%u Yen=%u Zen=%u\n",
		       c1, (unsigned)((c1 >> 4) & 0xFU), (unsigned)((c1 >> 3) & 1U),
		       (unsigned)(c1 & 1U), (unsigned)((c1 >> 1) & 1U),
		       (unsigned)((c1 >> 2) & 1U));
	}

	/* 手册寄存器列表：三轴数据 OUT_X_L..OUT_Z_H 在 0x28..0x2D */
	{
		uint8_t raw6[6] = { 0 };

		if (bma253_reg_read_burst(&bma253, BMA253_OUT_BASE_28, raw6, sizeof(raw6)) == 0) {
			int16_t x = (int16_t)((uint16_t)raw6[0] | ((uint16_t)raw6[1] << 8));
			int16_t y = (int16_t)((uint16_t)raw6[2] | ((uint16_t)raw6[3] << 8));
			int16_t z = (int16_t)((uint16_t)raw6[4] | ((uint16_t)raw6[5] << 8));

			printk("  OUT 0x28..0x2D = %02X %02X %02X %02X %02X %02X\n",
			       raw6[0], raw6[1], raw6[2], raw6[3], raw6[4], raw6[5]);
			printk("  小端16位: X=%d Y=%d Z=%d\n", x, y, z);
			printk("  12位左对齐(>>4): X=%d Y=%d Z=%d\n", x >> 4, y >> 4, z >> 4);
		}

		/* 对照：手册里 0x61..0x66 是第二组数据 */
		raw6[0] = raw6[1] = raw6[2] = raw6[3] = raw6[4] = raw6[5] = 0U;
		if (bma253_reg_read_burst(&bma253, BMA253_OUT_BASE_61, raw6, sizeof(raw6)) == 0) {
			printk("  OUT 0x61..0x66 = %02X %02X %02X %02X %02X %02X\n",
			       raw6[0], raw6[1], raw6[2], raw6[3], raw6[4], raw6[5]);
		}
	}

	/* 连续区间转储 0x02..0x3F：用来找这颗器件"真实"的数据寄存器
	 * 与配置寄存器落在哪里（克隆件的寄存器映射常与手册不一致）。
	 * 多按几次可看出哪些地址在变化（那就是数据寄存器）。 */
	{
		uint8_t v2;

		printk("--- 区间转储 0x02..0x3F ---\n");
		for (uint16_t r = 0x02U; r <= 0x3FU; r++) {
			v2 = 0U;
			if (bma253_reg_read(&bma253, (uint8_t)r, &v2) == 0) {
				printk("%02X:%02X ", r, v2);
			} else {
				printk("%02X:-- ", r);
			}
			if ((r % 8U) == 0U) {
				printk("\n");
			}
		}
		printk("\n");
	}
	printk("------------------------\n");
}

#if 0
/* ==================== 运动检测中断（INT1 -> P0.00）==================== */

/* 中断引脚：对应 overlay 里的 bma253_int1 节点（P0.00） */
#define BMA253_INT1_NODE  DT_NODELABEL(bma253_int1)

static const struct gpio_dt_spec bma253_int1 = GPIO_DT_SPEC_GET(BMA253_INT1_NODE, gpios);
static struct gpio_callback bma253_int1_cb;
#endif

/* 中断里只提交 work，I2C 读放在工作队列线程里做 */
static volatile uint32_t motion_count;      /* 引脚中断次数 */
static volatile uint8_t motion_src;
static struct k_work motion_work;

static void motion_work_handler(struct k_work *work);

static void bma253_int1_handler(const struct device *port,
				struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* 【重要】中断上下文的两条铁律：
	 *
	 * 1) 不能调 I2C（也不能调任何会阻塞等待的 API）。
	 *    I2C 驱动内部用 k_sem_take 带超时等待，中断里不允许阻塞，
	 *    否则触发内核断言崩溃（sem.c:136）。
	 * 2) 不能用 printf，要用 printk。
	 *    printf 走 picolibc 的 stdout，是带缓冲的：ISR 里打完就返回，
	 *    永远不会 fflush，输出留在缓冲区里看不到；而且 printf 内部
	 *    有 stdio 锁，主线程若正持有会死锁。
	 *    printk 用内部 spinlock，中断安全且不缓冲。 */
	motion_count++;
	k_work_submit(&motion_work);

	printk("[%6d ms] INT1 上升沿 #%u\n",
	       (int)k_uptime_get(), (unsigned)motion_count);
}

/** @brief 两点欧氏距离的平方根（避免引入 math.h） */
static double dist3(double dx, double dy, double dz)
{
	double v = dx * dx + dy * dy + dz * dz;
	double g = v;

	if (g > 1.0) {
		for (int i = 0; i < 32; i++) {
			g = 0.5 * (g + v / g);
		}
	}
	return g;
}

/** @brief 工作队列线程：清锁存、校验真伪运动、上报状态机 */
static void motion_work_handler(struct k_work *work)
{
	/* 上一次的加速度样本（用于变化量比较） */
	static bool have_last;
	static double last_x, last_y, last_z;
	static int64_t last_ms;

	int64_t now = k_uptime_get();
	uint32_t n = motion_count;
	uint8_t src = 0U;
	struct bma253_sample s;
	double delta;
	int ret;

	ARG_UNUSED(work);

	/* 1) 读 AOI1_SRC —— 必须读，它会清除锁存让 INT1 释放，
	 *    否则下一次运动不会产生新的上升沿。
	 *    注意：这颗克隆件的 IA(bit7) 不可靠（真实运动时也可能是 0），
	 *    所以 **不能** 用 IA 做过滤判据。 */
	ret = bma253_motion_read_src(&bma253, &src);
	motion_src = src;

	/* 2) 读当前加速度，与上次样本比较合矢量变化量。
	 *    这个判据由我们自己的换算决定，不依赖芯片的任何状态位。 */
	if (bma253_read(&bma253, &s) < 0) {
		printk("[%6lld ms] 运动候选 #%u：读加速度失败（%d），已忽略\n",
		       now, (unsigned)n, ret);
		last_ms = now;
		return;
	}

	if (!have_last) {
		last_x = s.x_g;
		last_y = s.y_g;
		last_z = s.z_g;
		have_last = true;
		delta = 0;
	} else {
		delta = dist3(s.x_g - last_x, s.y_g - last_y, s.z_g - last_z);
		last_x = s.x_g;
		last_y = s.y_g;
		last_z = s.z_g;
	}

	/* 3) 判据：合矢量变化量 >= 阈值才算真实运动。
	 *    阈值取自 bma253_motion_config.h 的宏（单位 mg -> g）。 */
	{
		double need = (double)BMA253_MOTION_THRESHOLD_MG / 1000.0;

		if (delta >= need) {
			printk("[%6lld ms, +%4lld ms] 运动 #%u：Δ|a|=%.3f g "
			       "(阈值 %.3f) SRC=0x%02X\n",
			       now, (last_ms != 0) ? (now - last_ms) : 0LL,
			       (unsigned)n, delta, need, src);

			// power_mgr_notify_motion();
		} else {
			/* 引脚动了但加速度没变 —— 电气干扰，忽略 */
			noise_irq_count++;
			printk("[%6lld ms, +%4lld ms] 伪中断 #%u（忽略，累计 %u）："
			       "Δ|a|=%.3f g < 阈值 %.3f，a=(%.3f,%.3f,%.3f)\n",
			       now, (last_ms != 0) ? (now - last_ms) : 0LL,
			       (unsigned)n, (unsigned)noise_irq_count,
			       delta, need, s.x_g, s.y_g, s.z_g);
		}
	}

	last_ms = now;
}

#if 0
int bma253_motion_example_setup(void)
{
	int ret;

	if (!gpio_is_ready_dt(&bma253_int1)) {
		printk("INT1 引脚设备未就绪（检查 overlay 的 bma253_int1 节点）\n");
		return -ENODEV;
	}

	/* 初始化事件工作项（I2C 读必须在工作队列线程里做，不能在 ISR 里） */
	k_work_init(&motion_work, motion_work_handler);

	/* 输入；BMA253 为推挽输出，无需内部上拉/下拉
	 * （Zephyr 的 GPIO 中断也不支持内部下拉） */
	ret = gpio_pin_configure_dt(&bma253_int1, GPIO_INPUT);
	if (ret < 0) {
		printk("INT1 引脚配置失败：%d\n", ret);
		return ret;
	}

	gpio_init_callback(&bma253_int1_cb, bma253_int1_handler,
			   BIT(bma253_int1.pin));
	ret = gpio_add_callback(bma253_int1.port, &bma253_int1_cb);
	if (ret < 0) {
		printk("INT1 回调注册失败：%d\n", ret);
		return ret;
	}

	/* 配置 BMA253 侧：模式/阈值/持续时间全部取自 bma253_motion_config.h */
	ret = bma253_motion_init(&bma253, BMA253_MOTION_MODE,
				 BMA253_MOTION_THRESHOLD_MG,
				 BMA253_MOTION_DURATION);
	if (ret < 0) {
		printk("BMA253 运动检测配置失败：%d\n", ret);
		return ret;
	}

	/* 【关键】使能引脚中断之前，先把 BMA253 的中断锁存清掉。
	 *
	 * 中断配置为锁存（LIR_INT1=1）：一旦触发，INT1 会**一直保持高**，
	 * 直到读 AOI1_SRC 才释放。如果在 INT1 已经为高的时候才去配置
	 * 「上升沿触发」，那么后续运动只是让这条线继续维持高电平，
	 * 不会产生新的上升沿 —— 中断就永远不会再进来。
	 *
	 * 这里读一次 AOI1_SRC 清锁存，让 INT1 回到低电平，
	 * 这样下一次运动才会产生真正的 低→高 跳变。 */
	{
		uint8_t src = 0U;

		(void)bma253_motion_read_src(&bma253, &src);
	}

	printk("运动检测中断就绪（INT1 = P0.%02u，阈值 %u mg，当前电平=%d）\n",
	       (unsigned)bma253_int1.pin,
	       (unsigned)BMA253_MOTION_THRESHOLD_MG,
	       gpio_pin_get_dt(&bma253_int1));

	/* 配置都好了，最后才打开引脚中断，避免配置过程中被误触发 */
	ret = gpio_pin_interrupt_configure_dt(&bma253_int1,
					     GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		printk("INT1 中断配置失败：%d\n", ret);
		return ret;
	}

	/* 使能后再读一次电平，确认清锁存是否成功 */
	k_msleep(2);
	printk("INT1 中断已使能，当前电平=%d（0=低，可正常触发上升沿）\n",
	       gpio_pin_get_dt(&bma253_int1));

	return 0;
}
#endif

/* ------------------------- 姿态角计算 ------------------------- */

#define BMA253_RAD_TO_DEG   (180.0 / 3.14159265358979323846)

double bma253_pitch_deg(const struct bma253_sample *s)
{
	double horiz;

	if (s == NULL) {
		return 0.0;
	}

	/* 水平分量 |(ay, az)|；两者全零说明数据无效（自由落体或读取失败） */
	horiz = sqrt((s->y_g * s->y_g) + (s->z_g * s->z_g));
	if ((horiz == 0.0) && (s->x_g == 0.0)) {
		return 0.0;
	}

	/* 平放（Z 轴朝上）为 0；X 轴方向抬头为正、低头为负 */
	return atan2(-s->x_g, horiz) * BMA253_RAD_TO_DEG;
}

double bma253_roll_deg(const struct bma253_sample *s)
{
	if (s == NULL) {
		return 0.0;
	}
	if ((s->y_g == 0.0) && (s->z_g == 0.0)) {
		return 0.0;
	}

	return atan2(s->y_g, s->z_g) * BMA253_RAD_TO_DEG;
}

double bma253_accel_mag_g(const struct bma253_sample *s)
{
	if (s == NULL) {
		return 0.0;
	}

	return sqrt((s->x_g * s->x_g) + (s->y_g * s->y_g) + (s->z_g * s->z_g));
}

/** @brief 轮询一次并打印三轴数据 + 俯仰角/横滚角 */
void bma253_example_poll(void)
{
	struct bma253_sample s;
	double pitch;
	double roll;
	double mag;

	/* 运动事件由 motion_work 在工作队列线程里处理并打印，
	 * 这里不需要再做什么。 */

	if (bma253_read(&bma253, &s) < 0) {
		return;
	}

	pitch = bma253_pitch_deg(&s);
	roll  = bma253_roll_deg(&s);
	mag   = bma253_accel_mag_g(&s);

	uipitch = (uint16_t)(round(pitch * 10));
	uiroll = (uint16_t)(round(roll * 10));

	// printk("BMA253: raw=%6d,%6d,%6d  g=%.3f,%.3f,%.3f  m/s²=%.3f,%.3f,%.3f  "
	//        "|a|=%.3fg  pitch=%.2f deg  roll=%.2f deg\n",
	//        s.raw_x, s.raw_y, s.raw_z,
	//        s.x_g, s.y_g, s.z_g,
	//        s.x_ms2, s.y_ms2, s.z_ms2,
	//        mag, pitch, roll);
}

void bma253_example_get_angle(uint16_t *pitch, uint16_t *roll)
{
	*pitch = uipitch;
	*roll = uiroll;
}

/**
 * @brief 查询已发生的运动事件次数
 *
 * 事件本身由工作队列处理，这里是给应用查询用的。
 */
uint32_t bma253_motion_get_count(void)
{
	return motion_count;
}

/**
 * @brief 只读巡检配置寄存器 0x1F~0x25
 *
 * 【重要】这里**只读不写**。
 *
 * 曾经用过"写 0x5A 再读回"来判断哪些地址可写，结果写坏了：
 *   0x1B/0x1C/0x1D = 数据输出寄存器 -> 篡改输出
 *   0x1E           = softreset     -> 触发复位
 *   0x1F           = 自测          -> 进入自测模式
 * 后果是输出寄存器被冻结在饱和值 (0x7EF0 / 0x8000 / 0x7FF0)，
 * 三个姿态读到完全相同的数据，传感器停止测量。
 *
 * 因此诊断一律只读。需要判断"寄存器是否存在"，改用下面的
 * 活性检查（bma253_check_alive）——看数据是否随姿态变化。
 */
void bma253_probe_ctrl_regs(void)
{
	static const uint8_t regs[] = {
		0x0E, 0x0F, 0x14, 0x15, 0x16, 0x17,
		0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
		0x2E, 0x2F, 0x3E, 0x3F, 0x6F, 0x70
	};

	printk("--- 配置寄存器只读巡检（不写入任何寄存器）---\n");

	for (size_t i = 0U; i < ARRAY_SIZE(regs); i++) {
		uint8_t v = 0U;

		if (bma253_reg_read(&bma253, regs[i], &v) == 0) {
			printk("  0x%02X = 0x%02X\n", regs[i], v);
		}
	}
	printk("-------------------------------------------\n");
}

/**
 * @brief 传感器活性检查：确认输出寄存器真的在随姿态更新
 *
 * 判据：连续采样 N 次，若三轴数据**始终完全不变**，说明输出被冻结
 * （通常是被写坏了，需要断电重启）。
 *
 * 返回 0 表示数据在变化（正常），-EIO 表示疑似冻结。
 */
int bma253_check_alive(struct bma253_data *dev, uint8_t samples)
{
	int16_t px = 0, py = 0, pz = 0;
	bool changed = false;

	if ((dev == NULL) || (samples < 2U)) {
		return -EINVAL;
	}

	(void)bma253_read_raw(dev, &px, &py, &pz);

	for (uint8_t i = 1U; i < samples; i++) {
		int16_t x = 0, y = 0, z = 0;

		k_msleep(20);
		if (bma253_read_raw(dev, &x, &y, &z) < 0) {
			continue;
		}
		if ((x != px) || (y != py) || (z != pz)) {
			changed = true;
			break;
		}
	}

	if (!changed) {
		printk("!! 警告：连续 %u 次采样三轴完全不变 (raw %d, %d, %d)\n"
		       "!! 输出寄存器疑似被冻结。请【完全断电 10 秒】后重新上电。\n"
		       "!! 若上电后仍不变，检查：0x20 是否非 0（三轴使能/ODR）\n",
		       (unsigned)samples, px, py, pz);
		return -EIO;
	}

	printk("活性检查通过：数据随噪声正常变化\n");

	return 0;
}

/**
 * @brief 四视图原始数据打印，用于确定数据格式
 *
 * 同一份 6 字节给出 4 种解释，并换算成 g。把板子换姿态对比，
 * 哪种解释下"某轴≈±1g、另两轴≈0"，就是正确的格式。
 */
void bma253_print_raw_views(void)
{
	uint8_t b[6] = { 0 };
	int16_t x16, y16, z16;
	int16_t x3, y3, z3, x4, y4, z4, x5, y5, z5;
	/* 按当前量程的 µg/LSB 换算（4g 量程时 = 2000） */
	double ug = (double)bma253.range_ug_per_lsb / 1.0e6;

	if (bma253_reg_read_burst(&bma253, BMA253_OUT_BASE_28, b, sizeof(b)) < 0) {
		return;
	}

	x16 = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
	y16 = (int16_t)((uint16_t)b[2] | ((uint16_t)b[3] << 8));
	z16 = (int16_t)((uint16_t)b[4] | ((uint16_t)b[5] << 8));
	x3 = x16 >> 3; y3 = y16 >> 3; z3 = z16 >> 3;
	x4 = x16 >> 4; y4 = y16 >> 4; z4 = z16 >> 4;
	x5 = x16 >> 5; y5 = y16 >> 5; z5 = z16 >> 5;

	printk("RAW %02X %02X %02X %02X %02X %02X | "
	       "16b %6d %6d %6d | >>3 %5d %5d %5d | >>4 %5d %5d %5d | >>5 %5d %5d %5d\n",
	       b[0], b[1], b[2], b[3], b[4], b[5],
	       x16, y16, z16, x3, y3, z3, x4, y4, z4, x5, y5, z5);
	printk("    g值: 16b(%.2f %.2f %.2f)  >>3(%.2f %.2f %.2f)  >>4(%.2f %.2f %.2f)  >>5(%.2f %.2f %.2f)\n",
	       x16 * ug, y16 * ug, z16 * ug,
	       x3 * ug, y3 * ug, z3 * ug,
	       x4 * ug, y4 * ug, z4 * ug,
	       x5 * ug, y5 * ug, z5 * ug);
}

/**
 * @brief 数据就绪中断方式读取（可选）
 *
 * 需要把 BMA253 的 INT1 接到一个 GPIO 并在 overlay 里配置中断，
 * 然后在中断回调里调用 bma253_read()。
 */
void bma253_example_int_setup(void)
{
	/* 0x30 = 0x80 只使能“数据就绪”中断（按手册 INT1_CFG 位定义，
	 * 具体位请对照你手册里 0x30 的说明），推挽、低有效、不锁存 */
	(void)bma253_config_int1(&bma253, 0x80U, false, false, false);
}

/* 用法：
 *   main() 里调用 bma253_example_init();
 *   然后在循环里按需调用 bma253_example_poll();
 */
