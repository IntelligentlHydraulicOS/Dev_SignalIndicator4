#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/nus_client.h>
#include <zephyr/sys/printk.h>

#include "nus_peripheral.h"
#include "central_manage.h"
#include "nus_central_scan.h"


struct bt_central_client CentralClient_list[20] = {0};




/*
********************************UUID********************************
*/

enum {
	NUS_C_INITIALIZED,
	NUS_C_TX_NOTIF_ENABLED,
	NUS_C_RX_WRITE_PENDING
};

int bt_nus_central_handles_assign(struct bt_gatt_dm *dm,
			  struct bt_central_client *nus_c)
{
	const struct bt_gatt_dm_attr *gatt_service_attr =
			bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service =
			bt_gatt_dm_attr_service_val(gatt_service_attr);
	const struct bt_gatt_dm_attr *gatt_chrc;
	const struct bt_gatt_dm_attr *gatt_desc;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_NUS_PERIPHERAL_SERVICE)) {
		return -ENOTSUP;
	}
	printk("Getting handles from NUS service.");
	memset(&nus_c->handles, 0xFF, sizeof(nus_c->handles));

	/* NUS TX Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_NUS_PERIPHERAL_TX);
	if (!gatt_chrc) {
		printk("Missing NUS TX characteristic.");
		return -EINVAL;
	}

	/* NUS TX */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_NUS_PERIPHERAL_TX);
	if (!gatt_desc) {
		printk("Missing NUS TX value descriptor in characteristic.");
		return -EINVAL;
	}
	printk("Found handle for NUS TX characteristic.");
	nus_c->handles.tx = gatt_desc->handle;
	/* NUS TX CCC */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		printk("Missing NUS TX CCC in characteristic.");
		return -EINVAL;
	}
	printk("Found handle for CCC of NUS TX characteristic.");
	nus_c->handles.tx_ccc = gatt_desc->handle;

	/* NUS RX Characteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_NUS_PERIPHERAL_RX);
	if (!gatt_chrc) {
		printk("Missing NUS RX characteristic.");
		return -EINVAL;
	}
	/* NUS RX */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_NUS_PERIPHERAL_RX);
	if (!gatt_desc) {
		printk("Missing NUS RX value descriptor in characteristic.");
		return -EINVAL;
	}
	printk("Found handle for NUS RX characteristic.");
	nus_c->handles.rx = gatt_desc->handle;

	/* Assign connection instance. */
	nus_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
}

static uint8_t on_received(struct bt_conn *conn,
			struct bt_gatt_subscribe_params *params,
			const void *data, uint16_t length)
{
	struct bt_central_client *nus;
	uint8_t addr_str[32] = {0};
	/* Retrieve NUS Client module context. */
	nus = CONTAINER_OF(params, struct bt_central_client, tx_notif_params);

	if (!data) {
		printk("[UNSUBSCRIBED]");
		params->value_handle = 0;
		atomic_clear_bit(&nus->state, NUS_C_TX_NOTIF_ENABLED);
		if (nus->cb.unsubscribed) {
			nus->cb.unsubscribed(nus);
		}
		return BT_GATT_ITER_STOP;
	}

	const bt_addr_le_t *addr = bt_conn_get_dst(conn);

	for(int i = 5; i >= 0; i--)
	{
		addr_str[5 - i] = addr->a.val[i];
	}

	memcpy(addr_str + 6, data, length);
	// LOG_INF("[NOTIFICATION] data %p length %u", data, length);
	//sendMsgdata2uart(addr_str, length + 6, 0);
	// bt_nus_peripheral_send(NULL, data, length);
	if (nus->cb.received) {
		return nus->cb.received(nus, data, length);
	}

	return BT_GATT_ITER_CONTINUE;
}

int bt_nus_central_subscribe_receive(struct bt_central_client *nus_c)
{
	int err;

	if (atomic_test_and_set_bit(&nus_c->state, NUS_C_TX_NOTIF_ENABLED)) {
		return -EALREADY;
	}

	nus_c->tx_notif_params.notify = on_received;
	nus_c->tx_notif_params.value = BT_GATT_CCC_NOTIFY;
	nus_c->tx_notif_params.value_handle = nus_c->handles.tx;
	nus_c->tx_notif_params.ccc_handle = nus_c->handles.tx_ccc;
	atomic_set_bit(nus_c->tx_notif_params.flags,
		       BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(nus_c->conn, &nus_c->tx_notif_params);
	if (err) {
		printk("[%s %d] Subscribe failed (err %d)\n", __func__, __LINE__, err);
		atomic_clear_bit(&nus_c->state, NUS_C_TX_NOTIF_ENABLED);
	} else {
		printk("[%s %d] [SUBSCRIBED]\n", __func__, __LINE__);
	}

	return err;
}

/*
********************************discovery********************************
*/


static void discovery_complete(struct bt_gatt_dm *dm,
			       void *context)
{
	struct bt_central_client *nus = context;
	printk("[]%s %d Service discovery completed\n", __func__, __LINE__);

	bt_gatt_dm_data_print(dm);

	bt_nus_central_handles_assign(dm, nus);
	bt_nus_central_subscribe_receive(nus);

	bt_gatt_dm_data_release(dm);

	bt_centralscan_start();
}

static void discovery_service_not_found(struct bt_conn *conn,
					void *context)
{
	printk("[%s %d] Service not found\n", __func__, __LINE__);
}

static void discovery_error(struct bt_conn *conn,
			    int err,
			    void *context)
{
	printk("[%s %d] Error while discovering GATT database: (%d)\n", __func__, __LINE__, err);
}
struct bt_gatt_dm_cb discovery_cb = {
	.completed         = discovery_complete,
	.service_not_found = discovery_service_not_found,
	.error_found       = discovery_error,
};


int nus1_central_init()
{
	printk("[%s %d] central init\n", __func__, __LINE__);
	for (int i = 0; i < 20; i++) 
	{
		CentralClient_list[i].conn = NULL;
	}

	return 0;
}

static void exchange_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	if (!err) {
		printk("[%s %d] MTU exchange done\n", __func__, __LINE__);
	} else {
		printk("[%s %d] MTU exchange failed (err %" PRIu8 ")\n", __func__, __LINE__, err);
	}
}

static int iCentral_num = 0;

static void gatt_discover(struct bt_central_client *nus)
{
	int err;

	err = bt_gatt_dm_start(nus->conn,
			       BT_UUID_NUS_PERIPHERAL_SERVICE,
			       &discovery_cb,
			       nus);
	if (err) {
		printk("[%s %d] could not start the discovery procedure, error code: %d\n", __func__, __LINE__, err);
	}
	else
	{
		printk("[%s %d] gatt_discover\n", __func__, __LINE__);
	}
}


void nus_central_connected(struct bt_conn *conn)
{
	int err, i, iNum = 0;
    iCentral_num = iCentral_num + 1;

    for (i = 0; i < 20; i++) {
        if (CentralClient_list[i].conn == NULL) {
            // 引用 +1（Zephyr 必须这么做）
            CentralClient_list[i].conn = bt_conn_ref(conn);
			iNum = i;
            break;
        }
    }

    printk("[%s %d] iCentral_num is %d\n", __func__, __LINE__, iCentral_num);

	static struct bt_gatt_exchange_params exchange_params;

	exchange_params.func = exchange_func;
	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) 
	{
		printk("[%s %d] MTU exchange failed (err %d)\n", __func__, __LINE__, err);
	}

	err = bt_conn_set_security(conn, BT_SECURITY_L1);
	if (err) {
		printk("[%s %d] Failed to set security: %d\n", __func__, __LINE__, err);
	}
	else
	{
		gatt_discover(CentralClient_list + iNum);
	}

	err = bt_central_scan_stop();
	if (err) {
		printk("[%s %d] Stop LE scan failed (err %d)\n", __func__, __LINE__, err);
	}
}
void nus_central_disconnected(struct bt_conn *conn)
{
	if(iCentral_num)
	{
		iCentral_num = iCentral_num - 1;

		for (int i = 0; i < 20; i++) 
		{
			if (CentralClient_list[i].conn == conn) {
				bt_conn_unref(CentralClient_list[i].conn);
				CentralClient_list[i].conn = NULL;
				break;
			}
		}
	}

    
    printk("[%s %d] disconnect; Central_num is %d\n", __func__, __LINE__, iCentral_num);

	if(iCentral_num < 20)
	{
		bt_centralscan_start();
	}
}

int getCentralNumber()
{
	return iCentral_num;
}

/*
广播期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, 8);
连接期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, 8);
*/
