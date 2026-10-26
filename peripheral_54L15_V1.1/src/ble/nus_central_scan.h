#ifndef BT_NUS_CENTRAL_SCAN_H_
#define BT_NUS_CENTRAL_SCAN_H_

#include <zephyr/types.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif


struct bt_scan_device_info {
	/** Received advertising packet information */
	const struct bt_le_scan_recv_info *recv_info;

	/** Connection parameters for LE connection. */
	const struct bt_le_conn_param *conn_param;

	/** Received advertising data. If further
	 *  data processing is needed, you should
	 *  use @em bt_data_parse() to get specific
	 *  advertising data type.
	 */
	struct net_buf_simple *adv_data;
};

struct bt_scan_init_param {
	/** Scan parameters required to initialize the module.
	 * Can be initialized as NULL. If NULL, the parameters required to
	 * initialize the module are loaded from the static configuration.
	 */
	const struct bt_le_scan_param *scan_param;

#if CONFIG_BT_CENTRAL
	/** If set to true, the module automatically
	 * connects after a filter match.
	 */
	bool connect_if_match;
#endif /* CONFIG_BT_CENTRAL */

	/** Connection parameters. Can be initialized as NULL.
	 * If NULL, the default static configuration is used.
	 */
	const struct bt_le_conn_param *conn_param;
};

void bt_central_scan_init(void);

int bt_central_scan_stop(void);
int bt_centralscan_start(void);



#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */