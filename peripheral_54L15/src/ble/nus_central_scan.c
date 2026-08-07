#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/printk.h>


#include "nus_central_scan.h"
#include "central_manage.h"

#define CONFIG_BT_SCAN_CONNECTABLE_CACHE_SIZE 8
#define MAX_CENTRAL_NUM 20

struct bt_scan_control {
	/* Number of active filters. */
	uint8_t filter_cnt;

	/* Number of matched filters. */
	uint8_t filter_match_cnt;

	/* Indicates whether at least one filter has been fitted. */
	bool filter_match;

	/* Indicates in which mode filters operate. */
	bool all_mode;

	/* Inform that device is connectable. */
	bool connectable;

	/* Data needed to establish connection and advertising information. */
	struct bt_scan_device_info device_info;

	/* Scan filter status. */
	// struct bt_scan_filter_match filter_status;
};

static struct bt_scan {
	/* Filter data. */
	//struct bt_scan_filters scan_filters;

#if CONFIG_BT_CENTRAL
	/* If set to true, the module automatically connects
	 * after a filter match.
	 */
	bool connect_if_match;
#endif /* CONFIG_BT_CENTRAL */

	/* Scan parameters required to initialize the module.
	 * Can be initialized as NULL. If NULL, the parameters required to
	 * initialize the module are loaded from the static configuration.
	 */
	struct bt_le_scan_param scan_param;

	/* Connection parameters. Can be initialized as NULL.
	 * If NULL, the default static configuration is used.
	 */
	struct bt_le_conn_param conn_param;

	/* Cache of connectable advertiser addresses.
	 * Scan response PDUs do not carry the connectable bit; it can only be
	 * determined based on the type of the initial advertising PDU.
	 * We cache an address when we see a connectable ADV; for SCAN_RSP we treat
	 * the device as connectable if its address is in this cache.
	 */
	bt_addr_le_t connectable_cache[CONFIG_BT_SCAN_CONNECTABLE_CACHE_SIZE];
	uint8_t connectable_cache_idx;
	uint8_t connectable_cache_count;

#if CONFIG_BT_SCAN_CONN_ATTEMPTS_FILTER
	/* Scan Connection attempts filter. */
	struct conn_attempts_filter attempts_filter;
#endif /* CONFIG_BT_SCAN_CONN_ATTEMPTS_FILTER */

#if CONFIG_BT_SCAN_BLOCKLIST
	/* Scan device blocklist. */
	struct conn_blocklist blocklist;
#endif /* CONFIG_BT_SCAN_BLOCKLIST */

} bt_scan;


static void scan_default_param_set(void)
{
	struct bt_le_scan_param *scan_param = BT_LE_SCAN_PASSIVE;

	/* Set the default parameters. */
	bt_scan.scan_param = *scan_param;
}

static void scan_default_conn_param_set(void)
{
	struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM_DEFAULT;

	/* Set default Connection params. */
	bt_scan.conn_param = *conn_param;
}


static struct bt_le_scan_cb scan_cb;

void bt_scan_init_val(const struct bt_scan_init_param *init)
{
	bt_le_scan_cb_register(&scan_cb);

	/* If the pointer to the initialization structure exist,
	 * use it to scan the configuration.
	 */
	if (init) {
        // bt_scan.connect_if_match = init->connect_if_match;

		if (init->scan_param) {
			bt_scan.scan_param = *init->scan_param;
		} else {
			/* Use the default static configuration. */
			scan_default_param_set();
		}

		if (init->conn_param) {
			bt_scan.conn_param = *init->conn_param;
		} else {
			/* Use the default static configuration. */
			scan_default_conn_param_set();
		}
	}
}

void bt_central_scan_init(void)
{
	struct bt_scan_init_param param = {
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT
	};

	printk("[%s %d] scan init\n", __func__, __LINE__);
	bt_scan_init_val(&param);
}

static void scan_connect_with_target(const bt_addr_le_t *addr)
{
	struct bt_conn *conn = NULL;
	int err;

	printk("[%s %d] scan connect target\n", __func__, __LINE__);

	bt_central_scan_stop();

	err = bt_conn_le_create(addr,
			       BT_CONN_LE_CREATE_CONN,
			       &bt_scan.conn_param, &conn);

	printk("[%s %d] Connecting (%d)\n", __func__, __LINE__, err);

	if (err) {
		/* If an error occurred, send an event to
		 * the all interested.
		 */
		bt_centralscan_start();
	} else {
		//notify_connecting(&control->device_info, conn);
		bt_conn_unref(conn);
	}

}

static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *ad)
{
	int iRet = 0;
	
	
	if(info->rssi < -95)
	{
		return;
	}

#if 1
	while (ad->len > 1) {
		struct bt_data data;
		uint8_t len;

		len = net_buf_simple_pull_u8(ad);
		if (len == 0U) {
			/* Early termination */
			return;
		}

		if (len > ad->len) {
			printk("[%s %d] malformed advertising data %u / %u\n", __func__, __LINE__, len, ad->len);
			return;
		}

		data.type = net_buf_simple_pull_u8(ad);
		data.data_len = len - 1;
		data.data = ad->data;

		switch(data.type)
		{
			case BT_DATA_NAME_COMPLETE:
				printk("[%s %d] name %s, data len %d\n", __func__, __LINE__, data.data, data.data_len);
				if(data.data_len == 8 && 0 == strncmp("SY_ANGLE", data.data, data.data_len))
				{
					// LOG_WRN("rssi(%d) addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)", info->rssi, info->addr->a.val[0], info->addr->a.val[1], info->addr->a.val[2], info->addr->a.val[3], info->addr->a.val[4], info->addr->a.val[5]);

					if(info->addr->a.val[0] >= 0x60  && info->addr->a.val[0] <= 0x95  && info->addr->a.val[1] == 0x1  && info->addr->a.val[2] == 0x12  && info->addr->a.val[3] == 0x25)
					{
						iRet++;
					}
					
				}
				break;
			case BT_DATA_MANUFACTURER_DATA:
				// LOG_WRN("manu %x %x %x %x %x", data.data[0], data.data[1], data.data[2], data.data[3], data.data[4]);

				if(data.data[0] == 0x3)
				{
					iRet++;
				}
				
				break;
			default:
				break;
		}

		net_buf_simple_pull(ad, len - 1);
	}

	if(iRet == 2)
	{
		scan_connect_with_target(info->addr);
	}
#endif
}

static struct bt_le_scan_cb scan_cb = {
	.recv = scan_recv,
};

int bt_centralscan_start(void)
{
	int iCentralN = getCentralNumber();

	if(iCentralN >= MAX_CENTRAL_NUM)
	{
		return 1;
	}

    bt_scan.scan_param.type = BT_LE_SCAN_TYPE_ACTIVE;//BT_LE_SCAN_TYPE_PASSIVE BT_LE_SCAN_TYPE_ACTIVE

    int err = bt_le_scan_start(&bt_scan.scan_param, NULL);

	if (!err) {
		printk("[%s %d] Scanning\n", __func__, __LINE__);
	}
	else
	{
		printk("[%s %d] success to start scan\n", __func__, __LINE__);
	}

	return err;
}

int bt_central_scan_stop(void)
{
	printk("[%s %d] stop Scan\n", __func__, __LINE__);
	return bt_le_scan_stop();
}