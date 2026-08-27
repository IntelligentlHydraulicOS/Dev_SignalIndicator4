#ifndef BQ27441_H
#define BQ27441_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

struct bq27441_result {
	uint8_t soc;            //剩余电量 %
	uint16_t voltage_mv;   //电池电压 mV
	int16_t  current_ma;   //瞬时电流 mA
	int16_t  temp_01c;      //温度 0.1℃
	uint16_t full_cap_mah; //满充容量 mAh
};

/**
 * @brief 读取电池全部参数
 * @param dev bq27441设备指针
 * @param res 结果缓存
 * @return 0成功，<0 I2C错误
 */
int bq27441_read(const struct i2c_dt_spec *spec, struct bq27441_result *res);

#endif
