/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __FUEL_GAUGE_H__
#define __FUEL_GAUGE_H__

typedef struct {
    float voltage;      /* 电压，单位 V */
    float current;      /* 电流，单位 A */
    float temp;         /* 温度，单位 °C */
    float soc;          /* SOC，单位 % */
    float soh;          /* SOH，单位 % */
    int32_t chg_status; /* 充电状态寄存器值 */
} fuel_gauge_data_t;

int fuel_gauge_init(const struct device *charger);

int fuel_gauge_update(const struct device *charger, bool vbus_connected);

void fuel_gauge_data_get(fuel_gauge_data_t *data);

#endif /* __FUEL_GAUGE_H__ */
