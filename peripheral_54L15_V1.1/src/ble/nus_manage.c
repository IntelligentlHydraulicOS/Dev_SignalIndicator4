#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include <zephyr/net_buf.h>            /* net_buf, net_buf_add, net_buf_unref */
#include <zephyr/sys/byteorder.h>      /* sys_cpu_to_le16 */
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/printk.h>

#include "nus_manage.h"
#include "peripheral_manage.h"
#include "central_manage.h"
#include "config.h"


static struct bt_conn *auth_conn;

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	struct bt_conn_info info;

	if (conn_err) {
#if 0
		printk("Failed to connect to %s, 0x%02x %s\n", addr, conn_err,
		       bt_hci_err_to_str(conn_err));

		if (conn == central_conn) {
			bt_conn_unref(central_conn);
			central_conn = NULL;

			scan_start();
		}
		return;
#endif
	}
	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("[%s %d] Failed to get connection info (err %d)\n", __func__, __LINE__, err);
		return;
	}

	if (info.role == BT_CONN_ROLE_CENTRAL)
	{
		printk("[%s %d] conn central\n", __func__, __LINE__);
		if (conn_err)
		{
			printk("[%s %d] conn central error\n", __func__, __LINE__);
			//central_disconnected();
			nus_central_disconnected(conn);
			
		}
		else
		{
			printk("[%s %d] conn central ed\n", __func__, __LINE__);
			nus_central_connected(conn);
		}
		
	}
	else if (info.role == BT_CONN_ROLE_PERIPHERAL)
	{
		printk("[%s %d] conn peripheal\n", __func__, __LINE__);
		peripheral_connected(conn);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;
	struct bt_conn_info info;

	err = bt_conn_get_info(conn, &info);
	if (info.role == BT_CONN_ROLE_CENTRAL)
	{
		printk("[%s %d] dis conn central\n", __func__, __LINE__);
		nus_central_disconnected(conn);
	}
	else if (info.role == BT_CONN_ROLE_PERIPHERAL)
	{
		printk("[%s %d] dis conn peripheal\n", __func__, __LINE__);
		peripheral_disconnected(conn);
	}
}

static void recycled_cb(void)
{
	printk("[%s %d] Connection object available from previous conn. Disconnect is complete!\n", __func__, __LINE__);
	//peripheral_advertising_start();
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	// int err = 0;
	struct bt_conn_info info = {0};

	bt_conn_get_info(conn, &info);

	if (info.role == BT_CONN_ROLE_PERIPHERAL)
	{
		return;
	}
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("[%s %d] Security changed: %s level %u\n", __func__, __LINE__, addr, level);
	} else {
		printk("[%s %d] Security failed: %s level %u err %d %s\n", __func__, __LINE__, addr, level, err,
			bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[%s %d] Passkey for %s: %06u\n", __func__, __LINE__, addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[%s %d] Passkey for %s: %06u\n", __func__, __LINE__, addr, passkey);
}


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[%s %d] Pairing cancelled: %s\n", __func__, __LINE__, addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[%s %d] Pairing completed: %s, bonded: %d\n", __func__, __LINE__, addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[%s %d] Pairing failed conn: %s, reason %d %s\n", __func__, __LINE__, addr, reason,
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif


void nus_security_init()
{
	int err = 0;
#if 1
	if (IS_ENABLED(CONFIG_BT_NUS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("[%s %d] Failed to register authorization callbacks. (err: %d)\n", __func__, __LINE__, err);
			return;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("[%s %d] Failed to register authorization info callbacks. (err: %d)\n", __func__, __LINE__, err);
		}
	}
#endif
}

static void set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl)
{
	struct bt_hci_cp_vs_write_tx_power_level *cp;
    struct bt_hci_rp_vs_write_tx_power_level *rp;
    struct net_buf *buf, *rsp = NULL;
    int err;

    /* 新 API：使用 bt_hci_cmd_alloc 替代 bt_hci_cmd_create */
    buf = bt_hci_cmd_alloc(K_FOREVER);
    if (!buf) {
        printk("[%s %d] Unable to allocate command buffer\n", __func__, __LINE__);
        return;
    }

    cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);
    cp->handle_type = handle_type;
    cp->tx_power_level = tx_pwr_lvl;

    /* opcode 在 send_sync 中传入 */
    err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, buf, &rsp);
    if (err) {
        printk("[%s %d] Set Tx power err: %d\n", __func__, __LINE__, err);
        return;
    }

    rp = (void *)rsp->data;
    printk("[%s %d] Actual Tx Power: %d\n", __func__, __LINE__, rp->selected_tx_power);
    net_buf_unref(rsp);
}

void CurrentSet_Tx_Power()
{
	set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, uiGetBLE_Txpower());
}
