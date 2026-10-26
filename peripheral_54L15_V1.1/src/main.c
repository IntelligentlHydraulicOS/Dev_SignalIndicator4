/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample
 */


#include <zephyr/types.h>
#include <soc.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/settings/settings.h>

#include <stdio.h>
#include <string.h>

#include "RS485_deal.h"
#include "zigbee_deal.h"
#include "dw3000_node.h"
#include "ledlight.h"

#include "nus_manage.h"
#include "peripheral_manage.h"
#include "central_manage.h"
#include "nus_central_scan.h"
#include "sensordeal.h"
#include "config.h"

void error(void)
{
	while (true) {
		/* Spin for ever */
		k_sleep(K_MSEC(1000));
	}
}

int main(void)
{
	
	nrfConfigInit();
	LedLightInit();

	RS485_deal_init();
	Zigbee_deal_init();
#if 1
	dw3000_hw_node_init();
#endif

	int err = 0;
	nus_security_init();
	err = bt_enable(NULL);
	if (err) {
		error();
	}

	printk("[%s %d]Bluetooth initialized\n", __func__, __LINE__);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
	

	err = nus_peripheral_init();
	if (err) {
		printk("[%s %d]Failed to initialize peripheral (err: %d)\n", __func__, __LINE__, err);
		return 0;
	}

	err = nus1_central_init();
	if (err) {
		printk("[%s %d]Failed to initialize central service (err: %d)\n", __func__, __LINE__, err);
		return 0;
	}

	bt_central_scan_init();
	bt_centralscan_start();

	RS485_dealsetuartReceive();
	Zigbee_dealsetuartReceive();

	dw3000_node_run();

	for (;;) {
		Function_of_indicator_lights();
		k_msleep(150);
		Sensor_Info_Respone_Interval();
		//dw1000_sendpoll();
	}
}

