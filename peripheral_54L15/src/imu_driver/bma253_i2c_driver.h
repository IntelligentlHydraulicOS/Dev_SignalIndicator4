/*
 * BMA253 三轴加速度计驱动（Zephyr / nRF Connect SDK）
 * 依据《BMA253-HXY》数据手册编写
 *
 * 目录：src/imu_driver/bma253_i2c_driver.c / .h
 * 接口：I2C
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BMA253_I2C_DRIVER_H
#define BMA253_I2C_DRIVER_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================ 寄存器地址 ============================
 * 严格摘自数据手册“11 寄存器列表”（第 11 节表格）：
 *
 *   0F  WHO_AM_I        r     = 0x11
 *   1F  CTRL_REG0       rw    <- BDU/BLE/FS1/FS0/DLPF0/ST1/ST0/SIM
 *   20  CTRL_REG1       rw    <- ODR[3:0] / LPen / Zen / Yen / Xen
 *   21  CTRL_REG2       rw
 *   22  CTRL_REG3       rw
 *   23  CTRL_REG4       rw
 *   24  CTRL_REG5       rw
 *   25  CTRL_REG6       rw
 *   27  DRDY_STATUS_REG r
 *   28  OUT_X_L         r     <- 三轴数据 0x28..0x2D（只读！）
 *   2E  FIFO_CTRL_REG   rw
 *   2F  FIFO_SRC_REG    r
 *   30  AOI1_CFG        rw
 *   31  AOI1_SRC        r
 *   32  AOI1_THS        rw
 *   33  AOI1_DURATION   rw
 *   34  AOI2_CFG        rw
 *   35  AOI2_SRC        r
 *   36  AOI2_THS        rw
 *   37  AOI2_DURATION   rw
 *   38  CLICK_CTRL_REG  rw
 *   39  CLICK_SRC       r
 *   3A  CLICK_COEFF1    rw
 *   3B  CLICK_COEFF2    rw
 *   3C  CLICK_COEFF3    rw
 *   3D  CLICK_COEFF4    rw
 *   40-5F  Reserved（手册警告：不要更改，可能引起永久性破坏）
 *   61  OUT_X_L(第二组) r     <- 0x61..0x66 镜像
 *   68  Soft_Reset      rw    <- 写 0xA5
 *   69  FIFO_DATA       r
 *   6F  I2C_CTRL        rw
 *   70  VERSION         r     = 0x28（配合 0x0F 判版本）
 *
 * 【重要教训】0x10~0x1E 在本表中**不存在**。早期版本把它们当作
 * INT_EN/INT_MAP/INT_SRC 使用，且把 DATA_CTRL 错误地定为 0x28 ——
 * 而 0x28 其实是**只读的输出寄存器 OUT_X_L**。往它写量程/BDU 会把
 * 数据输出打乱，表现为读数冻结在饱和值。
 */
#define BMA253_REG_WHO_AM_I     0x0FU   /* 只读，值 0x11 */
#define BMA253_REG_CTRL_REG0    0x1FU   /* BDU/BLE/FS/DLPF0/ST/SIM */
#define BMA253_REG_CTRL_REG1    0x20U   /* ODR / 电源模式 / 轴使能 */
#define BMA253_REG_CTRL_REG2    0x21U
#define BMA253_REG_CTRL_REG3    0x22U
#define BMA253_REG_CTRL_REG4    0x23U
#define BMA253_REG_CTRL_REG5    0x24U
#define BMA253_REG_CTRL_REG6    0x25U
#define BMA253_REG_INT_STATUS   0x27U   /* DRDY_STATUS_REG，只读 */
#define BMA253_REG_DRDY_STATUS  0x27U

#define BMA253_REG_FIFO_CTRL    0x2EU
#define BMA253_REG_FIFO_SRC     0x2FU
#define BMA253_REG_INT1_CFG     0x30U   /* AOI1_CFG：运动检测配置 */
#define BMA253_REG_INT1_SRC     0x31U   /* AOI1_SRC：中断源状态 */
#define BMA253_REG_INT1_THS     0x32U   /* AOI1_THS：阈值 */
#define BMA253_REG_INT1_DUR     0x33U   /* AOI1_DURATION：持续时间 */

/* 语义化别名（推荐用这组名字，AOI 才是手册里的正式叫法） */
#define BMA253_REG_AOI1_CFG     BMA253_REG_INT1_CFG
#define BMA253_REG_AOI1_SRC     BMA253_REG_INT1_SRC
#define BMA253_REG_AOI1_THS     BMA253_REG_INT1_THS
#define BMA253_REG_AOI1_DUR     BMA253_REG_INT1_DUR
#define BMA253_REG_INT2_CFG     0x34U   /* AOI2_CFG */
#define BMA253_REG_INT2_SRC     0x35U
#define BMA253_REG_INT2_THS     0x36U
#define BMA253_REG_INT2_DUR     0x37U
#define BMA253_REG_CLICK_CFG    0x38U
#define BMA253_REG_CLICK_SRC    0x39U
#define BMA253_REG_CLICK_THS    0x3AU   /* CLICK_COEFF1 */
#define BMA253_REG_TIME_LIMIT   0x3BU   /* CLICK_COEFF2 */
#define BMA253_REG_TIME_LATENCY 0x3CU   /* CLICK_COEFF3 */
#define BMA253_REG_TIME_WINDOW  0x3DU   /* CLICK_COEFF4 */

#define BMA253_REG_DATA_ALT_X_L 0x61U   /* 第二组输出寄存器 0x61..0x66 */
#define BMA253_REG_DATA_ALT_X_H 0x62U
#define BMA253_REG_DATA_ALT_Y_L 0x63U
#define BMA253_REG_DATA_ALT_Y_H 0x64U
#define BMA253_REG_DATA_ALT_Z_L 0x65U
#define BMA253_REG_DATA_ALT_Z_H 0x66U
#define BMA253_REG_SOFT_RESET   0x68U   /* 写 0xA5 复位整个电路 */
#define BMA253_REG_FIFO_DATA    0x69U
#define BMA253_REG_I2C_CTRL     0x6FU
#define BMA253_REG_VERSION      0x70U   /* 配合 WHO_AM_I 判版本 */

/* 兼容别名：配置寄存器 0x1F 就是数据控制寄存器（量程/BDU/DLPF0/自测） */
#define BMA253_REG_DATA_CTRL    BMA253_REG_CTRL_REG0

/* ==================== 数据输出寄存器基址 ====================
 * 手册存在两处互相冲突的说法：
 *   a) “11 寄存器列表”表：OUT_X_L..OUT_Z_H 位于 **0x28..0x2D**，
 *      配置寄存器 CTRL_REG0..6 位于 0x1F..0x25，DRDY_STATUS 位于 0x27
 *   b) “批量读取”示例：连读三轴用 0x28..0x2D（子地址 0xA8 = 0x28|0x80）
 *
 * 而“12 寄存器描述”章节里，那些 OSR/DLPF/HR、BDU/BLE/FS/ST/SIM 位定义的
 * 控制寄存器**没有标出地址**，与列表对不上 —— 手册本身不自洽。
 *
 * 实测裁决（区间转储 0x02..0x3F）：
 *   0x28 0x29 0x2A 0x2B 0x2C 0x2D 六个字节全部有变化 —— 符合“三轴各 2 字节”
 *   0x02..0x05 有变化，但 0x06/0x07 恒为 0 —— 不符合三轴数据特征
 * 因此采用 (a)(b) 一致的 0x28 作为数据基址。
 *
 * 注意：手册说 0x61..0x66 是第二组 OUT_X_L..OUT_Z_H，可作为备选。
 */
#define BMA253_OUT_BASE_28   0x28U   /* 手册寄存器列表/批量读示例：三轴数据（默认） */
#define BMA253_OUT_BASE_02   0x02U   /* 实测有变化但 Z 恒 0，非三轴数据 */
#define BMA253_OUT_BASE_61   0x61U   /* 手册中的第二组三轴数据 */

/* 兼容旧名字：默认使用 0x28 */
#define BMA253_OUT_BASE_DEFAULT BMA253_OUT_BASE_28

/* ============================ 关键常量 ============================ */

/* WHO_AM_I（0x0F）固定值。手册备注：0x11，需配合 0x70 判断版本 */
#define BMA253_CHIP_ID           0x11U

/* 7 位 I2C 从机地址，由 SDO 引脚电平决定：
 *   SDO 悬空 / 接逻辑高 -> 0x19（手册 8 位写地址 0x32，读 0x33）
 *   SDO 接逻辑低        -> 0x18（手册 8 位写地址 0x30，读 0x31），需关闭 SDO 内部上拉
 */
#define BMA253_I2C_ADDR_SDO_HIGH 0x19U
#define BMA253_I2C_ADDR_SDO_LOW  0x18U
#define BMA253_I2C_ADDR_DEFAULT  BMA253_I2C_ADDR_SDO_HIGH

/* 软复位命令：向 0x68 写入 0xA5，复位整个电路并清数据 */
#define BMA253_SOFT_RESET_CMD    0xA5U

/* 0x26 / 0x27 中断引脚控制位 */
#define BMA253_INT_IO_LATCH      BIT(0)   /* 1: 锁存中断 */
#define BMA253_INT_IO_OD         BIT(1)   /* 1: 开漏 */
#define BMA253_INT_IO_LVL        BIT(2)   /* 1: 高有效 */
#define BMA253_INT_IO_EN         BIT(3)   /* 1: 输出使能 */

/* 0x28 DATA_CTRL 位 */
#define BMA253_DATA_CTRL_SIM     BIT(0)   /* 1: 3 线 SPI */
#define BMA253_DATA_CTRL_ST0     BIT(1)   /* 自测试 */
#define BMA253_DATA_CTRL_ST1     BIT(2)
#define BMA253_DATA_CTRL_DLPF0   BIT(3)   /* DLPF[0]，DLPF[1] 在 0x1F */
#define BMA253_DATA_CTRL_FS0     BIT(4)   /* FS[0] */
#define BMA253_DATA_CTRL_FS1     BIT(5)   /* FS[1] */
#define BMA253_DATA_CTRL_BLE     BIT(6)   /* 1: 高字节在低地址 */
#define BMA253_DATA_CTRL_BDU     BIT(7)   /* 1: 读取期间不更新数据 */

/* 0x20 CTRL_REG1 位 */
#define BMA253_CTRL1_XEN         BIT(0)
#define BMA253_CTRL1_YEN         BIT(1)
#define BMA253_CTRL1_ZEN         BIT(2)
#define BMA253_CTRL1_LPEN        BIT(3)   /* 1: 低功耗模式 */
#define BMA253_CTRL1_ODR_Pos     4U
#define BMA253_CTRL1_ODR_Msk     (0x0FU << BMA253_CTRL1_ODR_Pos)

/* 0x1F CTRL_REG0 位 */
#define BMA253_CTRL0_HR          BIT(7)   /* 1: 高性能/增强模式 */

/* ============================ 中断相关寄存器 ============================
 * 地址来自手册第 11 节寄存器表；位定义来自第 12 节（该节只给位含义、
 * 不给地址，地址是按章节顺序对应到 CTRL_REG0..6 = 0x1F..0x25 的）。
 */

/* 0x22 CTRL_REG3 —— INT1 中断使能（每位置 1 表示"该中断路由到 INT1"） */
#define BMA253_CTRL3_I1_CLICK    BIT(7)
#define BMA253_CTRL3_I1_AOI1     BIT(6)   /* 运动检测走这里 */
#define BMA253_CTRL3_I1_AOI2     BIT(5)
#define BMA253_CTRL3_I1_DRDY     BIT(4)
#define BMA253_CTRL3_I1_WTM      BIT(2)
#define BMA253_CTRL3_I1_OVERRUN  BIT(1)
#define BMA253_CTRL3_FIFO_MODE   BIT(0)

/* 0x23 CTRL_REG4 —— 中断全局配置 */
#define BMA253_CTRL4_BOOT        BIT(7)
#define BMA253_CTRL4_FIFO_EN     BIT(6)
#define BMA253_CTRL4_AOI_EN      BIT(5)   /* 【注意】0 = 使能 AOI，1 = 禁止！ */
#define BMA253_CTRL4_LIR_INT1    BIT(3)   /* 1: 锁存中断（读中断寄存器清除） */
#define BMA253_CTRL4_D4D_INT1    BIT(2)   /* 1: INT1 上用 4D，需同时置 6D */
#define BMA253_CTRL4_LIR_INT2    BIT(1)
#define BMA253_CTRL4_D4D_INT2    BIT(0)

/* 0x24 CTRL_REG5 —— INT2 中断使能 + 引脚电气属性 */
#define BMA253_CTRL5_I2_CLICK    BIT(7)
#define BMA253_CTRL5_I2_AOI1     BIT(6)
#define BMA253_CTRL5_I2_AOI2     BIT(5)
#define BMA253_CTRL5_I2_BOOT     BIT(4)
#define BMA253_CTRL5_I2_DRDY     BIT(3)
#define BMA253_CTRL5_CS_PU_EN0   BIT(2)
#define BMA253_CTRL5_H_LACTIVE   BIT(1)   /* 0: 高有效；1: 低有效 */
#define BMA253_CTRL5_INT_PP_OD   BIT(0)   /* 0: 推挽；1: 开漏 */

/* 0x21 CTRL_REG2 —— 高通滤波配置 */
#define BMA253_CTRL2_HDS         BIT(6)   /* 1: 用 ADC 原始数据 */
#define BMA253_CTRL2_HPCF2       BIT(5)
#define BMA253_CTRL2_HPCF1       BIT(4)
#define BMA253_CTRL2_FDS         BIT(3)   /* 1: 高通滤波后的数据送数据寄存器/FIFO */
#define BMA253_CTRL2_HP_RESET    BIT(2)   /* 1: 复位高通滤波器 */
#define BMA253_CTRL2_HPIS2       BIT(1)   /* AOI2 高通滤波使能 */
#define BMA253_CTRL2_HPIS1       BIT(0)   /* AOI1 高通滤波使能（运动检测必需！） */

/* 0x30 AOI1_CFG —— 运动检测核心配置 */
#define BMA253_AOI_CFG_IA        BIT(7)   /* 0 = 或(OR) 模式；1 = 与(AND) 模式 */
#define BMA253_AOI_CFG_6D        BIT(6)   /* 1 = 打开 6D 方向识别 */
#define BMA253_AOI_CFG_Z_HIGH    BIT(5)
#define BMA253_AOI_CFG_Z_LOW     BIT(4)
#define BMA253_AOI_CFG_Y_HIGH    BIT(3)
#define BMA253_AOI_CFG_Y_LOW     BIT(2)
#define BMA253_AOI_CFG_X_HIGH    BIT(1)
#define BMA253_AOI_CFG_X_LOW     BIT(0)

/* 三轴、双向全部使能（0x3F）：任一轴超阈值（高）或低于阈值（低）都触发。
 * 配合 IA=0/6D=0 就是"或事件"——最常用的运动检测。 */
#define BMA253_AOI_CFG_ALL_AXES  (BMA253_AOI_CFG_Z_HIGH | BMA253_AOI_CFG_Z_LOW | \
				  BMA253_AOI_CFG_Y_HIGH | BMA253_AOI_CFG_Y_LOW | \
				  BMA253_AOI_CFG_X_HIGH | BMA253_AOI_CFG_X_LOW)

/* AOI1 工作模式（手册 12.16 "中断模式" 表）：
 *   IA=0 6D=0 : 或中断事件      —— 任一使能轴触发即中断
 *   IA=0 6D=1 : 6 个方向运动识别 —— 所有使能轴都触发才中断
 *   IA=1 6D=0 : 与中断事件
 *   IA=1 6D=1 : 6 个方向位置检测
 */
#define BMA253_AOI_MODE_OR        (0U)
#define BMA253_AOI_MODE_6D_MOVE   (BMA253_AOI_CFG_6D)
#define BMA253_AOI_MODE_AND       (BMA253_AOI_CFG_IA)
#define BMA253_AOI_MODE_6D_POS    (BMA253_AOI_CFG_IA | BMA253_AOI_CFG_6D)

/* 0x31 AOI1_SRC —— 中断源状态（只读，判断是哪个轴触发的） */
#define BMA253_AOI_SRC_IA        BIT(7)   /* 1: 有中断产生 */
#define BMA253_AOI_SRC_Z_HIGH    BIT(5)
#define BMA253_AOI_SRC_Z_LOW     BIT(4)
#define BMA253_AOI_SRC_Y_HIGH    BIT(3)
#define BMA253_AOI_SRC_Y_LOW     BIT(2)
#define BMA253_AOI_SRC_X_HIGH    BIT(1)
#define BMA253_AOI_SRC_X_LOW     BIT(0)

/* ==================== 运动检测阈值（可自行调整）====================
 *
 * 阈值与模式已移到独立文件 bma253_motion_config.h，
 * 平时只改那个文件即可，不用动本驱动。
 */
#include "bma253_motion_config.h"

/* 兜底默认值：若配置文件里没定义也能编译 */
#ifndef BMA253_MOTION_THRESHOLD_MG
#define BMA253_MOTION_THRESHOLD_MG   100U
#endif

#ifndef BMA253_MOTION_DURATION
#define BMA253_MOTION_DURATION       1U
#endif

/* 0x09 DRDY_STATUS 位 */
#define BMA253_STATUS_DRDY       BIT(7)   /* 1: 新数据就绪 */

/* ============================ 枚举与结构体 ============================ */

/** 量程，对应 0x28 的 FS1:FS0；灵敏度见手册“灵敏度”表 */
typedef enum {
	BMA253_RANGE_2G  = 0x00,  /* ±2g,  1 mg/digit */
	BMA253_RANGE_4G  = 0x01,  /* ±4g,  2 mg/digit */
	BMA253_RANGE_8G  = 0x02,  /* ±8g,  4 mg/digit */
	BMA253_RANGE_16G = 0x03,  /* ±16g, 8 mg/digit */
} bma253_range_t;

/** ODR / 电源模式（ODR[3:0]），见手册“数据输出率的配置”表 */
typedef enum {
	BMA253_ODR_POWER_DOWN = 0x00,  /* 电源关断模式        */
	BMA253_ODR_1_56HZ     = 0x01,  /* 全工作模式 1.56Hz   */
	BMA253_ODR_12_5HZ     = 0x02,  /* 全工作模式 12.5Hz   */
	BMA253_ODR_25HZ       = 0x03,  /* 全工作模式 25Hz     */
	BMA253_ODR_50HZ       = 0x04,  /* 全工作模式 50Hz     */
	BMA253_ODR_100HZ      = 0x05,  /* 全工作模式 100Hz    */
	BMA253_ODR_200HZ      = 0x06,  /* 全工作模式 200Hz    */
	BMA253_ODR_400HZ      = 0x07,  /* 全工作模式 400Hz    */
	BMA253_ODR_800HZ      = 0x08,  /* 全工作模式 800Hz    */
	BMA253_ODR_1_48KHZ    = 0x09,  /* 高性能模式 1.48kHz  */
	BMA253_ODR_2_66KHZ    = 0x0A,  /* 高性能模式 2.66kHz  */
	BMA253_ODR_4_434KHZ   = 0x0B,  /* 高性能模式 4.434kHz */
} bma253_odr_t;

/** DLPF[1:0]：DLPF0 在 0x28 bit3，DLPF1 在 0x1F */
typedef enum {
	BMA253_DLPF_1ST  = 0x00,  /* 1 阶  */
	BMA253_DLPF_4TH  = 0x01,  /* 4 阶  */
	BMA253_DLPF_8TH  = 0x02,  /* 8 阶  */
	BMA253_DLPF_16TH = 0x03,  /* 16 阶 */
} bma253_dlpf_t;

/** 一次采样结果 */
struct bma253_sample {
	int16_t raw_x;
	int16_t raw_y;
	int16_t raw_z;
	double  x_g;
	double  y_g;
	double  z_g;
	double  x_ms2;
	double  y_ms2;
	double  z_ms2;
};

/** 驱动运行数据 */
struct bma253_data {
	const struct device *bus;      /* I2C 总线 */
	uint8_t  addr;                 /* 7 位从机地址 */
	uint8_t  chip_id;              /* 读回的 WHO_AM_I */
	uint8_t  version;              /* 0x70 读回 */
	bma253_range_t range;
	bma253_odr_t   odr;
	bma253_dlpf_t  dlpf;
	uint32_t range_ug_per_lsb;     /* 每 LSB 的 µg 数：1000/2000/4000/8000 */
	uint8_t  output_base;          /* 数据输出寄存器基址，默认 BMA253_OUT_BASE_28 */
	uint8_t  data_shift;           /* 有效数据右移位数（左对齐补偿），默认 4 */
	int32_t  off_x;                /* 校准零偏（单位：raw，右移之后） */
	int32_t  off_y;
	int32_t  off_z;
	uint32_t scale_num;            /* 尺度分子，校准后使用 */
	uint32_t scale_den;            /* 尺度分母；g = raw_cal * scale_num / scale_den / 1000 */
	bool     calibrated;           /* 是否已完成校准 */
	bool     initialized;
};

/*
 * 数据格式说明（依据实测确定）：
 *
 * 输出寄存器是 16 位，但有效数据**左对齐**存放，低位是对齐补零位。
 * 实测证据：
 *   - 静止与翻转过程中，原始 16 位值的低半字节恒为 0 或 8；
 *   - 原始最高字节不超过约 0x4B（19344），而 ÷16 后得到的量级才符合
 *     重力（X 在 ±1000 左右摆动 = ±1g）。
 *
 * 因此真实读数 = raw16 >> data_shift（默认 4）。
 * 若不右移，换算出的加速度会放大 16 倍 —— 这正是"合矢量 16g"的来源。
 *
 * 另外：这颗器件（克隆件）三轴零偏较大，静止时合矢量并不等于 1g。
 * 用 bma253_calibrate() 在静止状态下测出零偏与尺度即可修正。
 */
#define BMA253_DATA_SHIFT_DEFAULT 4U

/* ============================ 对外 API ============================ */

/**
 * @brief 初始化 BMA253：校验 WHO_AM_I、软复位、配置量程/ODR/轴使能
 * @param dev   调用方提供的结构体，生命周期需覆盖使用期
 * @param bus   I2C 总线设备，如 DEVICE_DT_GET(DT_NODELABEL(i2c_sensor))
 * @param addr  7 位从机地址
 * @param range 量程
 * @param odr   输出数据率
 * @return 0 成功，负值为错误码
 */
int bma253_init(struct bma253_data *dev, const struct device *bus, uint8_t addr,
		bma253_range_t range, bma253_odr_t odr);

/** @brief 仅探测：读 WHO_AM_I 并校验 */
int bma253_probe(const struct device *bus, uint8_t addr, uint8_t *chip_id_out);

/** @brief 读取一次三轴数据（原始值 + g + m/s²） */
int bma253_read(struct bma253_data *dev, struct bma253_sample *out);

/** @brief 只读原始三轴值，最轻量 */
int bma253_read_raw(struct bma253_data *dev, int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief 三点姿态校准：记录当前姿态的采样
 *
 * 为什么是三个姿态：
 *   两个位置时"两次差多少度"未知，方程欠定（实测静止合矢量会不一致）。
 *   三个明确姿态能让每个轴都取到 ±1g 的一对读数，方程可解。
 *
 * 用法（每个姿态保持静止，依次调用）：
 *   姿态1 板子平放（Z 朝上）    -> bma253_calib_record(dev, 24)
 *   姿态2 板子翻过来平放（Z 朝下）-> bma253_calib_record(dev, 24)
 *   姿态3 板子长边立起          -> bma253_calib_record(dev, 24)
 *   然后 bma253_calib_finish(dev)
 *
 * finish 会打印每个轴的最低/最高值，据此算出"每 g 多少 raw"。
 * 判读：每 g ≈ 2000 表示量程 ±2g（1mg/LSB）；≈4000 表示 ±4g。
 *
 * @param samples 采样次数，建议 16~32
 */
int bma253_calib_record(struct bma253_data *dev, uint16_t samples);

/** @brief 三点校准收尾：算出灵敏度与零偏，之后读数为校准值 */
int bma253_calib_finish(struct bma253_data *dev);

/**
 * @brief 自动判断数据输出寄存器基址（0x02 还是 0x28）
 *
 * 手册中两处地址不一致（详见 .h 顶部说明）。本函数在两种基址下各读一次，
 * 选择“三轴静止时合矢量最接近 1g”的那一组，并把结果写入 dev->output_base。
 *
 * 使用前提：调用时传感器已配置好量程并使能，且板子基本静止。
 *
 * @return 0 成功（已选定基址）；负值为错误码
 */
int bma253_autodetect_output_base(struct bma253_data *dev);

/** @brief 设置量程 */
int bma253_set_range(struct bma253_data *dev, bma253_range_t range);

/** @brief 设置 ODR / 电源模式 */
int bma253_set_odr(struct bma253_data *dev, bma253_odr_t odr);

/** @brief 设置数字低通滤波器阶数 */
int bma253_set_dlpf(struct bma253_data *dev, bma253_dlpf_t dlpf);

/** @brief 软复位（0x68 = 0xA5） */
int bma253_soft_reset(struct bma253_data *dev);

/**
 * @brief 输出寄存器恢复序列（软复位 + 掉电再上电 + 重设量程）
 *
 * 用于解救"配置正确、引擎在跑，但输出寄存器不再更新"的锁死状态。
 *
 * 已验证的正常机制：BDU(CTRL_REG0 bit7)=1 时输出寄存器会被冻结，
 * 所以本函数最后会把 BDU 明确清零。若执行后输出仍完全不变，
 * 且彻底断电也无法恢复，则传感器很可能已损坏。
 *
 * @param odr 恢复后重新使用的 ODR
 * @return 0 输出已恢复更新；-EIO 输出仍冻结
 */
int bma253_recover_output(struct bma253_data *dev, bma253_odr_t odr);

/** @brief 使能/关闭自测试 */
int bma253_selftest(struct bma253_data *dev, bool enable);

/**
 * @brief 配置三轴运动检测中断，并路由到 INT1 引脚
 *
 * 用 AOI1（任何/全部方向中断）实现"或事件"运动检测：
 *   AOI1_CFG = 0x3F  -> 三轴、高/低双向事件全部使能
 *   模式     = IA=0, 6D=0 -> 任一轴事件即产生中断
 *
 * 阈值由宏 BMA253_MOTION_THRESHOLD_MG 给出（单位 mg）。
 * 驱动按当前量程换算成寄存器 LSB：
 *   FS=2g  -> 16 mg/LSB
 *   FS=4g  -> 32 mg/LSB
 *   FS=8g  -> 64 mg/LSB
 *   FS=16g -> 128 mg/LSB
 *
 * 配置完成后：
 *   - AOI1 中断已路由到 INT1 引脚（CTRL_REG3 的 I1_AOI1）
 *   - 中断被锁存（CTRL_REG4 的 LIR_INT1），需读 AOI1_SRC 清除
 *
 * @param dev       驱动实例
 * @param mode      BMA253_AOI_MODE_* 之一（默认用 BMA253_AOI_MODE_OR）
 * @param threshold_mg 阈值（mg）。传 0 则使用宏 BMA253_MOTION_THRESHOLD_MG
 * @param duration  需连续满足的采样周期数（防抖），传 0 用宏默认值
 * @return 0 成功
 */
int bma253_motion_init(struct bma253_data *dev, uint8_t mode,
		       uint32_t threshold_mg, uint8_t duration);

/**
 * @brief 读取运动检测中断源，判断是哪个轴触发
 *
 * @param dev 驱动实例
 * @param src 输出 AOI1_SRC（0x31）原始值，用 BMA253_AOI_SRC_* 判断
 * @return 0 成功
 */
int bma253_motion_read_src(struct bma253_data *dev, uint8_t *src);

/** @brief 配置 INT1 引脚与中断源（cfg 为 0x30 寄存器值，0 表示不使能中断源） */
int bma253_config_int1(struct bma253_data *dev, uint8_t cfg,
		       bool active_high, bool open_drain, bool latch);

/** @brief 读中断状态：0x09 数据就绪状态 与 0x1E 中断源 */
int bma253_read_int_status(struct bma253_data *dev, uint8_t *drdy_status, uint8_t *int_src);

/** @brief 单寄存器读 / 写 / 读改写 */
int bma253_reg_read(struct bma253_data *dev, uint8_t reg, uint8_t *val);
int bma253_reg_write(struct bma253_data *dev, uint8_t reg, uint8_t val);
int bma253_reg_update(struct bma253_data *dev, uint8_t reg, uint8_t mask, uint8_t val);

/** @brief 连续读多个寄存器（一次读 6 字节 XYZ 数据用） */
int bma253_reg_read_burst(struct bma253_data *dev, uint8_t start_reg,
			  uint8_t *buf, uint8_t len);

#endif /* BMA253_I2C_DRIVER_H */
