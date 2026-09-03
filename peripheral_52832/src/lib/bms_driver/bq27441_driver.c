#include "bq27441_driver.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(bq27441, LOG_LEVEL_INF);

#define BQ27441_REG_SOC             0x1C
#define BQ27441_REG_VOLTAGE         0x04
#define BQ27441_REG_CURRENT         0x10
#define BQ27441_REG_TEMP            0x02
#define BQ27441_REG_FULL_CAP        0x0E

/* 驱动私有数据 */
struct bq27441_dev_data {
	const struct i2c_dt_spec i2c;
};

struct bq27441_info_t bat_info;
/* 新增：保护bat_info全局结构体互斥锁 */
K_MUTEX_DEFINE(bat_info_mutex);

#define BQ27441_NODE DT_ALIAS(bq27441)
static const struct i2c_dt_spec bq27441_i2c = I2C_DT_SPEC_GET(BQ27441_NODE);

static int bq27441_read_word(const struct i2c_dt_spec *spec, uint8_t reg, uint16_t *val)
{
	uint8_t buf[2];
	int ret;

	ret = i2c_write_read_dt(spec, &reg, 1, buf, 2);
	if (ret < 0) {
		printf("read reg 0x%02X failed %d", reg, ret);
		return ret;
	}
    // printf("reg:%x, val:[%02x %02x]\r\n", reg, buf[0], buf[1]);
	*val = sys_le16_to_cpu((buf[1] << 8U) | buf[0]);
	return 0;
}

int bq27441_read(const struct i2c_dt_spec *spec, struct bq27441_info_t *res)
{
	uint16_t raw;
	int ret;

	memset(res, 0, sizeof(*res));

	ret = bq27441_read_word(spec, BQ27441_REG_VOLTAGE, &raw);
	if (ret) return ret;
	res->voltage_mv = raw;

	ret = bq27441_read_word(spec, BQ27441_REG_SOC, &raw);
	if (ret) return ret;
	res->soc = (uint8_t)raw;

	ret = bq27441_read_word(spec, BQ27441_REG_CURRENT, &raw);
	if (ret) return ret;
	res->current_ma = (int16_t)raw;

	ret = bq27441_read_word(spec, BQ27441_REG_TEMP, &raw);
	if (ret) return ret;
	res->temp_01c = (int16_t)raw;

	ret = bq27441_read_word(spec, BQ27441_REG_FULL_CAP, &raw);
	if (ret) return ret;
	res->full_cap_mah = raw;

	return 0;
}


static void i2c_bus_scan(void)
{
	int ret;
	uint8_t addr;
	printf("Start scan i2c bus 0\r\n");
	for(addr = 1; addr < 127; addr++)
	{
		ret = i2c_write(bq27441_i2c.bus, NULL, 0, addr);
		if(ret == 0)
		{
			printf("Found device at 0x%02X\r\n",addr);
		}
	}
	printf("Scan finished\r\n");
}


int bq27441_init()
{
	if (!device_is_ready(bq27441_i2c.bus)) {
		printf("BQ27441 device not ready!\r\n");
		return -1;
	}
	printf("BQ27441 device ready OK!\r\n");
	return 0;
}


void bms_status_update()
{
	int ret = bq27441_read(&bq27441_i2c, &bat_info);
	if (ret == 0) {
		printf("SOC:%d%%,Volt:%dmV,Cur:%dmA,Temp:%.1fC,Cap:%dmAh\r\n",
			bat_info.soc,
			bat_info.voltage_mv,
			bat_info.current_ma,
			bat_info.temp_01c / 10.0f,
			bat_info.full_cap_mah);
	} else {
		printf("Read battery fail\r\n");
	}

}


/* 设备树匹配 */
// #define BQ27441_INST(inst)									\
// 	static struct bq27441_dev_data bq27441_data_##inst = {			\
// 		.i2c = I2C_DT_SPEC_INST_GET(inst),					\
// 	};												\
// 	DEVICE_DT_INST_DEFINE(inst, bq27441_init, NULL,			\
// 		&bq27441_data_##inst, NULL, POST_KERNEL,			\
// 		50, NULL);

// DT_INST_FOREACH_STATUS_OKAY(BQ27441_INST)

