/*
 * BMA253 三轴加速度计驱动实现（Zephyr / nRF Connect SDK，I2C 接口）
 *
 * 依据《BMA253-HXY》数据手册：
 *   - 11 寄存器列表 / 12 寄存器描述
 *   - WHO_AM_I(0x0F) = 0x11，需配合 0x70 判断版本
 *   - I2C 地址：SDO 悬空/高 -> 0x19；SDO 低 -> 0x18
 *   - 软复位：0x68 写 0xA5
 *   - 数据输出：0x02..0x07（OUT_X_L .. OUT_Z_H），小端
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bma253_i2c_driver.h"

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bma253, CONFIG_LOG_DEFAULT_LEVEL);

/* 1 g 对应的加速度，单位 m/s² */
#define BMA253_G_TO_MS2   9.80665

/* 软复位后的等待时间。手册未给出典型值，这里取 5 ms 由调用方确认 */
#define BMA253_RESET_DELAY_MS  5

/* 三点校准：每个姿态的采样缓存（每轴 raw 平均，已含 data_shift） */
#define BMA253_CALIB_POINTS  3

static struct {
	uint8_t  count;                              /* 已记录的姿态数 */
	double   x[BMA253_CALIB_POINTS];
	double   y[BMA253_CALIB_POINTS];
	double   z[BMA253_CALIB_POINTS];
} calib_pts;

/* 对三轴各采样 samples 次求平均，结果单位与 bma253_read_raw 一致 */
static int bma253_sample_avg(struct bma253_data *dev, uint16_t samples,
			     double *out_x, double *out_y, double *out_z)
{
	int64_t sum_x = 0, sum_y = 0, sum_z = 0;
	uint16_t n = 0;

	for (uint16_t i = 0U; i < samples; i++) {
		int16_t x = 0, y = 0, z = 0;

		if (bma253_read_raw(dev, &x, &y, &z) < 0) {
			continue;
		}
		sum_x += x;
		sum_y += y;
		sum_z += z;
		n++;
		k_msleep(5);
	}

	if (n < 4U) {
		return -EIO;
	}

	*out_x = (double)sum_x / (double)n;
	*out_y = (double)sum_y / (double)n;
	*out_z = (double)sum_z / (double)n;

	return 0;
}

/* ------------------------- 底层寄存器访问 ------------------------- */

int bma253_reg_read(struct bma253_data *dev, uint8_t reg, uint8_t *val)
{
	if ((dev == NULL) || (dev->bus == NULL) || (val == NULL)) {
		return -EINVAL;
	}

	return i2c_reg_read_byte(dev->bus, dev->addr, reg, val);
}

int bma253_reg_write(struct bma253_data *dev, uint8_t reg, uint8_t val)
{
	if ((dev == NULL) || (dev->bus == NULL)) {
		return -EINVAL;
	}

	return i2c_reg_write_byte(dev->bus, dev->addr, reg, val);
}

int bma253_reg_update(struct bma253_data *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	uint8_t old;
	int ret;

	ret = bma253_reg_read(dev, reg, &old);
	if (ret < 0) {
		return ret;
	}

	old = (uint8_t)((old & ~mask) | (val & mask));

	return bma253_reg_write(dev, reg, old);
}

int bma253_reg_read_burst(struct bma253_data *dev, uint8_t start_reg,
			  uint8_t *buf, uint8_t len)
{
	int ret = 0;

	if ((dev == NULL) || (dev->bus == NULL) || (buf == NULL) || (len == 0U)) {
		return -EINVAL;
	}

	/*
	 * BMA253-HXY 手册说它的 I2C 是“近似的标准协议”：子地址最高位表示
	 * 是否地址自增（例：连读三轴用 0xA8 = 0x28|0x80）。
	 *
	 * 但实测这颗器件的行为无法确认是否支持自增位，因此这里**完全不依赖
	 * 自增**：对每个寄存器单独发起一次“写子地址 + 读 1 字节”的独立事务，
	 * 逐步递增地址。这样无论器件是否支持自增都能拿到正确的 6 个字节。
	 */
	for (uint8_t i = 0U; i < len; i++) {
		uint8_t sub_addr = (uint8_t)(start_reg + i);

		ret = i2c_write_read(dev->bus, dev->addr, &sub_addr, 1, &buf[i], 1);
		if (ret < 0) {
			LOG_ERR("顺序读 0x%02X 失败：%d", sub_addr, ret);
			break;
		}
	}
#if 0
	printf("bma253_reg_read_burst: start_reg=0x%02X, len=%u, ret=%d\n",
	       start_reg, len, ret);
	printf("bma253_reg_read_burst: data=");
	for (uint8_t i = 0; i < len; i++) {
		printf("0x%02X ", buf[i]);
	}
	printf("\n");
#endif
	return ret;
}

/* ------------------------- 量程换算 ------------------------- */

/* 手册“灵敏度”表：FS=0 -> 1 mg/digit ... FS=3 -> 8 mg/digit */
static uint32_t range_to_ug_per_lsb(bma253_range_t range)
{
	switch (range) {
	case BMA253_RANGE_2G:
		return 1000U;   /* 1 mg  = 1000 µg */
	case BMA253_RANGE_4G:
		return 2000U;   /* 2 mg  */
	case BMA253_RANGE_8G:
		return 4000U;   /* 4 mg  */
	case BMA253_RANGE_16G:
		return 8000U;   /* 8 mg  */
	default:
		return 1000U;
	}
}

/* ------------------------- 探测与初始化 ------------------------- */

int bma253_probe(const struct device *bus, uint8_t addr, uint8_t *chip_id_out)
{
	uint8_t id = 0U;
	int ret;

	if ((bus == NULL) || !device_is_ready(bus)) {
		return -ENODEV;
	}

	ret = i2c_reg_read_byte(bus, addr, BMA253_REG_WHO_AM_I, &id);
	if (ret < 0) {
		LOG_ERR("读取 WHO_AM_I 失败（addr=0x%02X, ret=%d）", addr, ret);
		return ret;
	}

	if (chip_id_out != NULL) {
		*chip_id_out = id;
	}

	if (id != BMA253_CHIP_ID) {
		LOG_ERR("WHO_AM_I 不匹配：期望 0x%02X，实际 0x%02X", BMA253_CHIP_ID, id);
		return -ENODEV;
	}

	return 0;
}

int bma253_init(struct bma253_data *dev, const struct device *bus, uint8_t addr,
		bma253_range_t range, bma253_odr_t odr)
{
	int ret;

	if ((dev == NULL) || (bus == NULL)) {
		return -EINVAL;
	}

	memset(dev, 0, sizeof(*dev));
	dev->bus = bus;
	dev->addr = addr;
	dev->range = range;
	dev->odr = odr;
	dev->dlpf = BMA253_DLPF_1ST;
	dev->range_ug_per_lsb = range_to_ug_per_lsb(range);
	dev->output_base = BMA253_OUT_BASE_28;
	dev->data_shift = BMA253_DATA_SHIFT_DEFAULT;

	if (!device_is_ready(bus)) {
		LOG_ERR("I2C 总线未就绪");
		return -ENODEV;
	}

	/* 1) 校验身份 */
	ret = bma253_probe(bus, addr, &dev->chip_id);
	if (ret < 0) {
		return ret;
	}

	/* 读版本寄存器（手册要求配合 WHO_AM_I 判断传感器版本） */
	(void)bma253_reg_read(dev, BMA253_REG_VERSION, &dev->version);
	LOG_INF("BMA253 已识别：WHO_AM_I=0x%02X, VERSION(0x70)=0x%02X",
		dev->chip_id, dev->version);

	/* 2) 软复位，确保从确定状态开始 */
	ret = bma253_soft_reset(dev);
	if (ret < 0) {
		return ret;
	}

	/* 3) 配置量程 / 字节序 / BDU
	 *
	 * 【重要】BDU（B7）必须为 0。
	 * 手册：BDU=1 表示"输出数据寄存器不更新，直到 MSB 和 LSB 被读取"。
	 * 实测这颗芯片在 BDU=1 时会表现为**输出寄存器被锁死**：
	 * 连续采样读到完全相同的饱和值 (0x7EF0/0x8000/0x7FF0)，
	 * 三个姿态也毫无变化 —— 看起来就像传感器坏了，实际是被 BDU 锁住。
	 *
	 * 这里直接写整个 DATA_CTRL：BDU=0, BLE=0(小端), FS=量程, DLPF0=0。
	 * 一次性覆盖，避免 reg_update 读-改-写的竞态。 */
	{
		uint8_t fs_bits = (uint8_t)(((uint8_t)range << 4) &
					    (BMA253_DATA_CTRL_FS0 | BMA253_DATA_CTRL_FS1));
		uint8_t data_ctrl = (uint8_t)(fs_bits & ~BMA253_DATA_CTRL_BDU);

		ret = bma253_reg_write(dev, BMA253_REG_DATA_CTRL, data_ctrl);
		if (ret < 0) {
			return ret;
		}

		/* 回读确认 BDU 确实为 0 */
		{
			uint8_t rb = 0U;

			(void)bma253_reg_read(dev, BMA253_REG_DATA_CTRL, &rb);
			LOG_INF("DATA_CTRL(0x%02X) 写入 0x%02X，回读 0x%02X",
				BMA253_REG_DATA_CTRL, data_ctrl, rb);
			printk("DATA_CTRL(0x%02X) 写入 0x%02X 回读 0x%02X "
			       "(BDU=%u, FS=%u)\n",
			       BMA253_REG_DATA_CTRL, data_ctrl, rb,
			       (unsigned)((rb & BMA253_DATA_CTRL_BDU) ? 1U : 0U),
			       (unsigned)((rb >> 4) & 0x03U));
		}
	}

	/* 4) 配置 ODR 并打开三轴 */
	ret = bma253_set_odr(dev, odr);
	if (ret < 0) {
		return ret;
	}

	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG1,
				BMA253_CTRL1_XEN | BMA253_CTRL1_YEN | BMA253_CTRL1_ZEN,
				BMA253_CTRL1_XEN | BMA253_CTRL1_YEN | BMA253_CTRL1_ZEN);
	if (ret < 0) {
		return ret;
	}

	dev->initialized = true;
	LOG_INF("BMA253 初始化完成：量程=%u, ODR=%u", (unsigned int)range, (unsigned int)odr);

	return 0;
}

int bma253_soft_reset(struct bma253_data *dev)
{
	int ret;

	ret = bma253_reg_write(dev, BMA253_REG_SOFT_RESET, BMA253_SOFT_RESET_CMD);
	if (ret < 0) {
		LOG_ERR("软复位写入失败：%d", ret);
		return ret;
	}

	/* 复位后需要等待芯片内部启动完成 */
	k_msleep(BMA253_RESET_DELAY_MS);

	return 0;
}

int bma253_recover_output(struct bma253_data *dev, bma253_odr_t odr)
{
	static const uint8_t out_regs[] = { 0x28U, 0x61U };
	uint8_t before[6] = { 0 };
	uint8_t after[6] = { 0 };
	uint8_t v = 0U;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	printk("\n--- 输出寄存器恢复序列 ---\n");

	/* 记录当前输出，便于前后对比 */
	if (bma253_reg_read_burst(dev, out_regs[0], before, sizeof(before)) < 0) {
		return -EIO;
	}
	printk("  恢复前 OUT = %02X %02X %02X %02X %02X %02X\n",
	       before[0], before[1], before[2], before[3], before[4], before[5]);

	/* 1) 软复位：写 0x68 = 0xA5 */
	ret = bma253_reg_write(dev, BMA253_REG_SOFT_RESET, BMA253_SOFT_RESET_CMD);
	printk("  1) 软复位 写0x68=0xA5 -> %s\n", (ret < 0) ? "I2C失败" : "已写入");
	k_msleep(10);

	/* 2) 电源关断：清掉 CTRL_REG1 的低 4 位（ODR=0000 + 三轴关） */
	(void)bma253_reg_write(dev, BMA253_REG_CTRL_REG1, 0x00U);
	k_msleep(30);
	(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG1, &v);
	printk("  2) 掉电 CTRL_REG1=0x%02X（应为 0x00）\n", v);

	/* 3) 重新上电：ODR 放低 4 位，三轴使能放低 3 位 */
	{
		uint8_t c1 = (uint8_t)((((uint8_t)odr & 0x0FU) << 4) | 0x07U);

		(void)bma253_reg_write(dev, BMA253_REG_CTRL_REG1, c1);
		k_msleep(60);
		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG1, &v);
		printk("  3) 重新上电 写0x%02X 回读0x%02X（ODR=%u 三轴=%u%u%u）\n",
		       c1, v, (unsigned)((v >> 4) & 0xFU),
		       (unsigned)(v & 1U), (unsigned)((v >> 1) & 1U),
		       (unsigned)((v >> 2) & 1U));
	}

	/* 4) 确认量程/BDU 仍是期望值（恢复过程可能被复位清掉） */
	{
		uint8_t fs_bits = (uint8_t)(((uint8_t)dev->range << 4) & 0x30U);

		(void)bma253_reg_write(dev, BMA253_REG_CTRL_REG0, fs_bits);
		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG0, &v);
		printk("  4) CTRL_REG0=0x%02X（BDU=%u FS=%u）\n",
		       v, (unsigned)((v >> 7) & 1U), (unsigned)((v >> 4) & 3U));
	}

	/* 5) 对比恢复前后 */
	k_msleep(20);
	if (bma253_reg_read_burst(dev, out_regs[0], after, sizeof(after)) < 0) {
		return -EIO;
	}
	printk("  恢复后 OUT = %02X %02X %02X %02X %02X %02X\n",
	       after[0], after[1], after[2], after[3], after[4], after[5]);

	if ((after[0] == before[0]) && (after[1] == before[1]) &&
	    (after[2] == before[2]) && (after[3] == before[3]) &&
	    (after[4] == before[4]) && (after[5] == before[5])) {
		printk("  !! 恢复序列无效：输出仍完全不变。\n"
		       "  !! 请【彻底断电 30 秒】；若仍如此，传感器很可能已损坏。\n");
		return -EIO;
	}

	printk("  >>> 恢复成功：输出已更新 <<<\n");

	/* 顺带看第二组输出（0x61..0x66），用于交叉验证 */
	if (bma253_reg_read_burst(dev, out_regs[1], after, sizeof(after)) == 0) {
		printk("  第二组 OUT 0x61..0x66 = %02X %02X %02X %02X %02X %02X\n",
		       after[0], after[1], after[2], after[3], after[4], after[5]);
	}
	printk("--------------------------\n\n");

	return 0;
}

/* ------------------------- 配置项 ------------------------- */

int bma253_set_range(struct bma253_data *dev, bma253_range_t range)
{
	uint8_t bits;
	int ret;

	if (range > BMA253_RANGE_16G) {
		return -EINVAL;
	}

	/* FS1:FS0 位于 0x28 的 bit5:bit4 */
	bits = (uint8_t)(((uint8_t)range << 4) & (BMA253_DATA_CTRL_FS0 | BMA253_DATA_CTRL_FS1));

	ret = bma253_reg_update(dev, BMA253_REG_DATA_CTRL,
				BMA253_DATA_CTRL_FS0 | BMA253_DATA_CTRL_FS1, bits);
	if (ret < 0) {
		return ret;
	}

	dev->range = range;
	dev->range_ug_per_lsb = range_to_ug_per_lsb(range);

	return 0;
}

int bma253_set_odr(struct bma253_data *dev, bma253_odr_t odr)
{
	uint8_t odr_bits;
	int ret;

	if (odr > BMA253_ODR_4_434KHZ) {
		return -EINVAL;
	}

	/* ODR[3:0] 位于 0x20 的 bit7:bit4；低功耗使能 LPen 在 bit3。
	 * 注意：bit7 同时是“高性能/增强模式”位 HR，手册“数据输出率的配置”
	 * 表里 ODR=1001~1011 就是高性能模式，所以写 ODR 值即包含了 HR 位。 */
	odr_bits = (uint8_t)(((uint8_t)odr << BMA253_CTRL1_ODR_Pos) & BMA253_CTRL1_ODR_Msk);

	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG1, BMA253_CTRL1_ODR_Msk, odr_bits);
	if (ret < 0) {
		return ret;
	}

	/* 低功耗位 LPen：手册“工作模式配置”表
	 *   HR=0, LPen=0 -> 正常模式；HR=0, LPen=1 -> 低功耗模式
	 * 因此仅在低速档（ODR 0010~1000，即 12.5Hz~800Hz）使能低功耗；
	 * 关断档和高速档（已被 HR 置位）都必须清零。 */
	if ((odr >= BMA253_ODR_12_5HZ) && (odr <= BMA253_ODR_800HZ)) {
		ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG1,
					BMA253_CTRL1_LPEN, BMA253_CTRL1_LPEN);
	} else {
		ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG1,
					BMA253_CTRL1_LPEN, 0U);
	}

	if (ret < 0) {
		return ret;
	}

	dev->odr = odr;

	return 0;
}

int bma253_set_dlpf(struct bma253_data *dev, bma253_dlpf_t dlpf)
{
	uint8_t d0, d1;
	int ret;

	if (dlpf > BMA253_DLPF_16TH) {
		return -EINVAL;
	}

	/* DLPF0 在 CTRL_REG0(0x1F) 的 bit3；DLPF1 在 CTRL_REG0 的 bit7。
	 * 两者是同一个寄存器，所以一次 reg_update 把两位一起写。
	 * （旧代码误把 DLPF0 归到 0x28，那是只读的输出寄存器。） */
	d0 = (uint8_t)(((uint8_t)dlpf & 0x01U) ? BMA253_DATA_CTRL_DLPF0 : 0U);
	d1 = (uint8_t)(((uint8_t)dlpf & 0x02U) ? BMA253_CTRL0_HR : 0U);

	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG0,
				(uint8_t)(BMA253_DATA_CTRL_DLPF0 | BMA253_CTRL0_HR),
				(uint8_t)(d0 | d1));
	if (ret < 0) {
		return ret;
	}

	dev->dlpf = dlpf;

	return 0;
}

int bma253_selftest(struct bma253_data *dev, bool enable)
{
	/* 手册“自测试模式配置”：ST1:ST0 = 00 禁止；其它组合见手册 */
	uint8_t val = enable ? BMA253_DATA_CTRL_ST0 : 0U;

	return bma253_reg_update(dev, BMA253_REG_DATA_CTRL,
				 BMA253_DATA_CTRL_ST0 | BMA253_DATA_CTRL_ST1, val);
}

int bma253_config_int1(struct bma253_data *dev, uint8_t cfg,
		       bool active_high, bool open_drain, bool latch)
{
	int ret;

	/*
	 * 【地址修正】手册第 11 节寄存器表里**没有** INT1_IO_CTRL(0x26)/
	 * INT2_IO_CTRL(0x27)：0x26 未定义，0x27 是只读的 DRDY_STATUS_REG。
	 * 中断配置只能通过 0x30 AOI1_CFG 完成。
	 *
	 * 引脚的推挽/开漏/有效电平由外部电路决定（本板未接 INT 引脚时
	 * 这些参数无实际作用），因此这里只做记录并配置中断源。
	 */
	ARG_UNUSED(active_high);
	ARG_UNUSED(open_drain);
	ARG_UNUSED(latch);

	ret = bma253_reg_write(dev, BMA253_REG_INT1_CFG, cfg);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/* ------------------------- 运动检测中断 ------------------------- */

/*
 * 阈值灵敏度（手册 12.18 "中断阈值寄存器"）：
 *   FS=2g  -> 16 mg/LSB
 *   FS=4g  -> 32 mg/LSB
 *   FS=8g  -> 64 mg/LSB
 *   FS=16g -> 128 mg/LSB
 * THS 只有低 7 位有效，所以最大 127 LSB。
 */
static const uint16_t motion_mg_per_lsb[4] = { 16U, 32U, 64U, 128U };

int bma253_motion_init(struct bma253_data *dev, uint8_t mode,
		       uint32_t threshold_mg, uint8_t duration)
{
	const uint16_t *lut = motion_mg_per_lsb;
	uint32_t mg_per_lsb;
	uint32_t code;
	uint8_t cfg;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	if ((uint8_t)dev->range > 3U) {
		return -EINVAL;
	}
	mg_per_lsb = (uint32_t)lut[(uint8_t)dev->range];

	/* 阈值：mg -> 寄存器 LSB，四舍五入，至少 1 LSB */
	if (threshold_mg == 0U) {
		threshold_mg = BMA253_MOTION_THRESHOLD_MG;
	}
	code = (threshold_mg + (mg_per_lsb / 2U)) / mg_per_lsb;

	if (code == 0U) {
		code = 1U;
	}
	if (code > 127U) {
		LOG_WRN("运动阈值 %u mg 超过 %u LSB（该量程上限 %u mg），已截断",
			(unsigned int)threshold_mg, 127U,
			(unsigned int)(127U * mg_per_lsb));
		code = 127U;
	}

	if (duration == 0U) {
		duration = BMA253_MOTION_DURATION;
	}
	if (duration == 0U) {
		/* 持续时间为 0 时 AOI 不工作，至少给 1 个采样周期 */
		duration = 1U;
	}
	if (duration > 127U) {
		duration = 127U;
	}

	/* ---------------- 配置顺序（按手册要求） ---------------- */

	/* 1) CTRL_REG2：使能 AOI1 高通滤波（HPIS1）—— 运动检测的必要条件
	 *
	 * 【为什么必须开】HPIS1=0 时 AOI1 比较的是**含重力直流分量的绝对
	 * 加速度**。板子静止时某轴恒为 ±1g（±4g 量程下 = 1000mg），而阈值
	 * 只有 100mg，于是该轴永远"超过阈值"，中断状态恒定不变 ——
	 * 示波器上量不到任何电平跳变。
	 *
	 * 使能高通滤波后，重力直流分量被滤掉，AOI1 只比较**动态加速度**，
	 * 旋转/晃动才会产生中断。
	 *
	 * FDS 保持 0：让数据寄存器仍输出未滤波的原始加速度，
	 * 这样读到的 g 值不受影响。 */
	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG2,
				BMA253_CTRL2_HPIS1 | BMA253_CTRL2_FDS,
				BMA253_CTRL2_HPIS1);
	if (ret < 0) {
		return ret;
	}
	/* 复位高通滤波器，让新配置立即生效 */
	(void)bma253_reg_update(dev, BMA253_REG_CTRL_REG2,
				BMA253_CTRL2_HP_RESET, BMA253_CTRL2_HP_RESET);
	k_msleep(2);
	(void)bma253_reg_update(dev, BMA253_REG_CTRL_REG2,
				BMA253_CTRL2_HP_RESET, 0U);

	/* 2) CTRL_REG4：AOI_EN 是"禁止位"，0 = 使能 AOI（名字是反的！） */
	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG4,
				BMA253_CTRL4_AOI_EN, 0U);
	if (ret < 0) {
		return ret;
	}

	/* 3) AOI1_DURATION：持续时间（不能为 0，否则 AOI 不工作） */
	ret = bma253_reg_write(dev, BMA253_REG_AOI1_DUR,
			       (uint8_t)(duration & 0x7FU));
	if (ret < 0) {
		return ret;
	}

	/* 4) AOI1_THS：阈值 */
	ret = bma253_reg_write(dev, BMA253_REG_AOI1_THS,
			       (uint8_t)(code & 0x7FU));
	if (ret < 0) {
		return ret;
	}

	/* 5) AOI1_CFG：只使能三轴「高事件」+ 指定模式
	 *
	 * 【为什么只开高事件，不开低事件】
	 * 手册对检测原理的原文是"当传感器输出值**超过**设定阈值且达到设置
	 * 时间范围，则判定传感器有动作" —— 只靠"超过阈值"这一个条件。
	 *
	 * 若同时开低事件（XL/YL/ZL），静止时轴值接近 0g，而 0 永远小于
	 * 阈值，低事件就**持续为真** → 中断引脚常亮、不跳变，
	 * 唤醒也会失效（中断一直有效，等于没有事件）。
	 *
	 * 所以运动/唤醒检测只开 XH/YH/ZH。 */
	cfg = (uint8_t)(mode | BMA253_AOI_CFG_X_HIGH |
			BMA253_AOI_CFG_Y_HIGH | BMA253_AOI_CFG_Z_HIGH);
	ret = bma253_reg_write(dev, BMA253_REG_AOI1_CFG, cfg);
	if (ret < 0) {
		return ret;
	}

	/* 6) CTRL_REG4：锁存中断（读 AOI1_SRC 清除） */
	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG4,
				BMA253_CTRL4_LIR_INT1, BMA253_CTRL4_LIR_INT1);
	if (ret < 0) {
		return ret;
	}

	/* 7) CTRL_REG3：把 AOI1 中断路由到 INT1 引脚 */
	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG3,
				BMA253_CTRL3_I1_AOI1, BMA253_CTRL3_I1_AOI1);
	if (ret < 0) {
		return ret;
	}

	/* 8) CTRL_REG5：INT1 引脚高有效 + 推挽输出
	 *    H_LACTIVE=0 → 高有效；INT_PP_OD=0 → 推挽 */
	ret = bma253_reg_update(dev, BMA253_REG_CTRL_REG5,
				BMA253_CTRL5_H_LACTIVE | BMA253_CTRL5_INT_PP_OD, 0U);
	if (ret < 0) {
		return ret;
	}

	printk("运动检测已配置：模式=0x%02X 阈值=%u mg (code=%u, 灵敏度 %u mg/LSB, "
	       "量程=%u) 持续时间=%u\n",
	       cfg, (unsigned int)threshold_mg, (unsigned int)code,
	       (unsigned int)mg_per_lsb, (unsigned int)dev->range,
	       (unsigned int)duration);

	/* 回读关键寄存器：万一中断不出，这几行能直接定位问题。
	 *
	 * 【注意】这里**故意不读 AOI1_SRC(0x31)**：
	 *   1) 它是「读即清除」寄存器，回读会把已有的中断锁存吃掉；
	 *   2) 这颗克隆件的 IA(bit7) 位不可靠（真实运动时也可能是 0），
	 *      用它判断"静止时是否已触发"结论必然是错的。
	 * 需要判断中断源时，应在中断处理流程里读，不要在这里回读。 */
	#if 0
	{
		uint8_t c2 = 0U, c3 = 0U, c4 = 0U, c5 = 0U, ths = 0U, dur = 0U;

		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG2, &c2);
		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG3, &c3);
		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG4, &c4);
		(void)bma253_reg_read(dev, BMA253_REG_CTRL_REG5, &c5);
		(void)bma253_reg_read(dev, BMA253_REG_AOI1_THS, &ths);
		(void)bma253_reg_read(dev, BMA253_REG_AOI1_DUR, &dur);

		printk("  中断配置回读：\n");
		printk("    CTRL_REG2(0x21)=0x%02X  HPIS1=%u(须为1) FDS=%u(须为0)\n",
		       c2, (unsigned)(c2 & BMA253_CTRL2_HPIS1 ? 1 : 0),
		       (unsigned)(c2 & BMA253_CTRL2_FDS ? 1 : 0));
		printk("    CTRL_REG3(0x22)=0x%02X  I1_AOI1=%u(须为1)\n",
		       c3, (unsigned)(c3 & BMA253_CTRL3_I1_AOI1 ? 1 : 0));
		printk("    CTRL_REG4(0x23)=0x%02X  AOI_EN=%u(须为0) LIR_INT1=%u\n",
		       c4, (unsigned)(c4 & BMA253_CTRL4_AOI_EN ? 1 : 0),
		       (unsigned)(c4 & BMA253_CTRL4_LIR_INT1 ? 1 : 0));
		printk("    CTRL_REG5(0x24)=0x%02X  H_LACTIVE=%u INT_PP_OD=%u\n",
		       c5, (unsigned)(c5 & BMA253_CTRL5_H_LACTIVE ? 1 : 0),
		       (unsigned)(c5 & BMA253_CTRL5_INT_PP_OD ? 1 : 0));
		printk("    AOI1_THS(0x32)=%u  AOI1_DUR(0x33)=%u\n",
		       (unsigned)(ths & 0x7FU), (unsigned)(dur & 0x7FU));
	}

	LOG_INF("运动检测：阈值 %u mg -> code %u（%u mg/LSB），duration %u",
		(unsigned int)threshold_mg, (unsigned int)code,
		(unsigned int)mg_per_lsb, (unsigned int)duration);
	#endif
	return 0;
}

int bma253_motion_read_src(struct bma253_data *dev, uint8_t *src)
{
	if ((dev == NULL) || (src == NULL)) {
		return -EINVAL;
	}

	/* 读 AOI1_SRC 同时会清除锁存的中断（LIR_INT1=1） */
	return bma253_reg_read(dev, BMA253_REG_AOI1_SRC, src);
}

/* ------------------------- 数据读取 ------------------------- */

int bma253_read_raw(struct bma253_data *dev, int16_t *x, int16_t *y, int16_t *z)
{
	uint8_t buf[6];
	uint8_t base;
	uint8_t shift;
	int ret;

	if ((dev == NULL) || (x == NULL) || (y == NULL) || (z == NULL)) {
		return -EINVAL;
	}

	base = (dev->output_base != 0U) ? dev->output_base : BMA253_OUT_BASE_61;
	shift = (dev->data_shift <= 8U) ? dev->data_shift : BMA253_DATA_SHIFT_DEFAULT;

	/* 手册：输出寄存器 6 字节连续排列，低字节在前（小端）。
	 * 地址见头文件说明（本机实测为 0x28~0x2D）。 */
	ret = bma253_reg_read_burst(dev, base, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	/* 16 位有符号，小端（BLE=0 时低字节在低地址）。
	 * 实测本器件数据**左对齐**，低位是对齐补零位，因此右移 shift 位
	 * 还原真实读数；若不右移，换算出的 g 值会放大 16 倍。 */
	*x = (int16_t)((int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8)) >> shift);
	*y = (int16_t)((int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8)) >> shift);
	*z = (int16_t)((int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8)) >> shift);

	return 0;
}

int bma253_autodetect_output_base(struct bma253_data *dev)
{
	static const uint8_t candidates[] = { BMA253_OUT_BASE_28, BMA253_OUT_BASE_61 };
	uint8_t saved;
	double best_err = 1.0e30;
	uint8_t best = BMA253_OUT_BASE_28;
	bool found = false;

	if (dev == NULL) {
		return -EINVAL;
	}

	saved = dev->output_base;
	dev->output_base = BMA253_OUT_BASE_28;

	for (size_t i = 0U; i < ARRAY_SIZE(candidates); i++) {
		int16_t x = 0, y = 0, z = 0;
		int ret;

		dev->output_base = candidates[i];

		ret = bma253_read_raw(dev, &x, &y, &z);
		if (ret < 0) {
			continue;
		}

		/* 静止时合矢量应接近 1g。以 mg 为单位计算误差 */
		double mag_g = ((double)x * (double)dev->range_ug_per_lsb) / 1.0e6;
		double my_g  = ((double)y * (double)dev->range_ug_per_lsb) / 1.0e6;
		double mz_g  = ((double)z * (double)dev->range_ug_per_lsb) / 1.0e6;
		double norm  = 0.0;
		double err;

		norm = (mag_g * mag_g) + (my_g * my_g) + (mz_g * mz_g);
		/* 用 sqrt 需要 math.h；这里比较平方距离，等价且无浮点库依赖 */
		err = (norm - 1.0) * (norm - 1.0);

		LOG_INF("输出基址候选 0x%02X: raw(%d,%d,%d) => |a|^2=%.4f g^2",
			candidates[i], x, y, z, norm);

		if ((err < best_err) && (norm > 0.05)) {
			best_err = err;
			best = candidates[i];
			found = true;
		}
	}

	if (!found) {
		dev->output_base = saved;
		LOG_WRN("输出基址自动探测失败，保持 0x%02X", saved);
		return -EIO;
	}

	dev->output_base = best;
	LOG_INF("选定输出基址：0x%02X", best);

	return 0;
}

/* ------------------------- 三点姿态校准 ------------------------- */

/*
 * 为什么需要三个姿态：
 *   只用两个位置时，"两次姿态差多少度"是未知的，方程欠定
 *   （实测 A/B 合矢量分别是 1136 和 902，静止时不应有这种差异）。
 *   用三个明确姿态就每个轴都能得到 ±1g 的一对读数，方程可解：
 *
 *     姿态1：板子平放，Z 轴朝上        -> Z ≈ +1g
 *     姿态2：板子翻过来平放，Z 轴朝下  -> Z ≈ -1g
 *     姿态3：板子长边立起              -> X 或 Y ≈ ±1g
 *
 *   每 g 的 raw 数 = |读数(姿态1) - 读数(姿态2)| / 2
 */

int bma253_calib_record(struct bma253_data *dev, uint16_t samples)
{
	int ret;
	uint8_t i;

	if ((dev == NULL) || (samples == 0U)) {
		return -EINVAL;
	}

	if (calib_pts.count >= BMA253_CALIB_POINTS) {
		calib_pts.count = 0;   /* 重新开始一轮 */
	}

	i = calib_pts.count;

	ret = bma253_sample_avg(dev, samples, &calib_pts.x[i],
				&calib_pts.y[i], &calib_pts.z[i]);
	if (ret < 0) {
		printk("姿态 %u 采样失败\n", (unsigned)(i + 1U));
		return ret;
	}

	calib_pts.count++;
	dev->calibrated = false;

	printk("已记录姿态 %u/%u：raw(%d, %d, %d)\n",
	       (unsigned)calib_pts.count, (unsigned)BMA253_CALIB_POINTS,
	       (int)calib_pts.x[i], (int)calib_pts.y[i], (int)calib_pts.z[i]);

	return 0;
}

int bma253_calib_finish(struct bma253_data *dev)
{
	double per_g[3];
	uint8_t axis;
	int best_axis = -1;
	double best_span = 0.0;

	if (dev == NULL) {
		return -EINVAL;
	}

	if (calib_pts.count < BMA253_CALIB_POINTS) {
		printk("校准未完成：只记录了 %u/%u 个姿态\n",
		       (unsigned)calib_pts.count, (unsigned)BMA253_CALIB_POINTS);
		return -EINVAL;
	}

	/* 对每个轴，取三个姿态里的最大值与最小值 —— 它们必然对应
	 * 该轴指向天和指向地，因此差值 = 2g。 */
	for (axis = 0U; axis < 3U; axis++) {
		double lo, hi;
		double v0, v1, v2;

		v0 = (axis == 0U) ? calib_pts.x[0] :
		     ((axis == 1U) ? calib_pts.y[0] : calib_pts.z[0]);
		v1 = (axis == 0U) ? calib_pts.x[1] :
		     ((axis == 1U) ? calib_pts.y[1] : calib_pts.z[1]);
		v2 = (axis == 0U) ? calib_pts.x[2] :
		     ((axis == 1U) ? calib_pts.y[2] : calib_pts.z[2]);

		hi = v0;
		lo = v0;
		if (v1 > hi) { hi = v1; }
		if (v1 < lo) { lo = v1; }
		if (v2 > hi) { hi = v2; }
		if (v2 < lo) { lo = v2; }

		per_g[axis] = (hi - lo) / 2.0;

		if (per_g[axis] > best_span) {
			best_span = per_g[axis];
			best_axis = (int)axis;
		}

		printk("  轴%d：最低 %d 最高 %d -> 每 g = %d raw\n",
		       (int)(axis + 1), (int)lo, (int)hi, (int)per_g[axis]);
	}

	if ((best_axis < 0) || (best_span < 10.0)) {
		printk("校准失败：没有任何轴观察到 2g 变化，"
		       "请确认三个姿态确实朝向不同方向\n");
		return -EIO;
	}

	/* 三轴使用同一个灵敏度（同一颗芯片、同一量程，理应相同）。
	 * 用变化最大（最可信）的那个轴的结果。 */
	dev->scale_num = 1000U;
	dev->scale_den = (uint32_t)best_span;

	/* 零偏 = 每个轴三个姿态的平均值（三个姿态合起来近似均匀覆盖球面） */
	dev->off_x = (int32_t)((calib_pts.x[0] + calib_pts.x[1] + calib_pts.x[2]) / 3.0);
	dev->off_y = (int32_t)((calib_pts.y[0] + calib_pts.y[1] + calib_pts.y[2]) / 3.0);
	dev->off_z = (int32_t)((calib_pts.z[0] + calib_pts.z[1] + calib_pts.z[2]) / 3.0);

	dev->calibrated = true;

	printk("三点校准完成：每 g = %u raw（取自轴%d），"
	       "零偏 (%d, %d, %d)，数据移位 %u\n",
	       dev->scale_den, best_axis + 1,
	       dev->off_x, dev->off_y, dev->off_z, dev->data_shift);

	LOG_INF("三点校准完成：每 g = %u raw，零偏 (%d, %d, %d)",
		dev->scale_den, dev->off_x, dev->off_y, dev->off_z);

	calib_pts.count = 0;

	return 0;
}

int bma253_read(struct bma253_data *dev, struct bma253_sample *out)
{
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	ret = bma253_read_raw(dev, &out->raw_x, &out->raw_y, &out->raw_z);
	if (ret < 0) {
		return ret;
	}

	if (dev->calibrated && (dev->scale_den != 0U)) {
		/* 已校准：扣零偏 + 应用尺度。静止时合矢量将≈1g */
		double sx = (double)((int32_t)out->raw_x - dev->off_x);
		double sy = (double)((int32_t)out->raw_y - dev->off_y);
		double sz = (double)((int32_t)out->raw_z - dev->off_z);
		double k = (double)dev->scale_num / (double)dev->scale_den;

		out->x_g = sx * k;
		out->y_g = sy * k;
		out->z_g = sz * k;
	} else {
		/* 未校准：按量程灵敏度换算 */
		out->x_g = ((double)out->raw_x * (double)dev->range_ug_per_lsb) / 1.0e6;
		out->y_g = ((double)out->raw_y * (double)dev->range_ug_per_lsb) / 1.0e6;
		out->z_g = ((double)out->raw_z * (double)dev->range_ug_per_lsb) / 1.0e6;
	}

	out->x_ms2 = out->x_g * BMA253_G_TO_MS2;
	out->y_ms2 = out->y_g * BMA253_G_TO_MS2;
	out->z_ms2 = out->z_g * BMA253_G_TO_MS2;

	return 0;
}

int bma253_read_int_status(struct bma253_data *dev, uint8_t *drdy_status, uint8_t *int_src)
{
	int ret;

	if (drdy_status != NULL) {
		ret = bma253_reg_read(dev, BMA253_REG_INT_STATUS, drdy_status);
		if (ret < 0) {
			return ret;
		}
	}

	if (int_src != NULL) {
		/* 【地址修正】0x1E 不存在；中断源状态在 0x31 AOI1_SRC（只读） */
		ret = bma253_reg_read(dev, BMA253_REG_INT1_SRC, int_src);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
