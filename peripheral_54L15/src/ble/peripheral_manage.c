#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/printk.h>

#include "nus_peripheral.h"
#include "nus_manage.h"
#include "peripheral_manage.h"
#include "config.h"

#define PERIPHERAL_MAX 1

static struct k_work adv_work;
struct bt_conn *peripheralconn_list[20] = {NULL};
static int iPeripheral_num = 0;

#define DEVICE_NAME "Sany-Master"
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

uint8_t my_custom_data[] = {
    0x11, 0x22, 0x33, 0x44, 0x55,     // 你想发的任意数据
    0xAA, 0xBB, 0xCC, 0xDD, 0x11,
    0x22            // 可放 ID/版本/传感器/距离等
};

#define MANUFACTURER_ID 0xFFFF        // 厂商ID（测试用）
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, my_custom_data, sizeof(my_custom_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
#if 0
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, my_custom_data, sizeof(my_custom_data)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL_PERIPHERAL),
};
#endif
static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	//int err;
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	printk("[%s %d] Received data from: %s", __func__, __LINE__, addr);

	//sendMsgdata2uart(data, len, 0);
	// sendMsgdata2uart(data, len, 1);
}

static struct bt_nus_peripheral_cb nus_cb = {
	.received = bt_receive_cb,
};


static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		printk("[%s %d] Advertising failed to start (err %d)\n", __func__, __LINE__, err);
		return;
	}

	printk("[%s %d] Advertising successfully started\r", __func__, __LINE__);
}


static void advertising_start(void)
{

    k_work_submit(&adv_work);
	
}

int nus_peripheral_init()
{
    int err = 0;

    printk("[%s %d] peripheral init\n", __func__, __LINE__);
    err = bt_nus_peripheral_init(&nus_cb);
	if (err) {
		printk("[%s %d] Failed to initialize UART service (err: %d)\n", __func__, __LINE__, err);
		return 0;
	}

    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, uiGetBLE_Txpower());
    k_work_init(&adv_work, adv_work_handler);
	advertising_start();
    return err;
}

void peripheral_advertising_start()
{
    advertising_start();
}

void peripheral_advertising_stop()
{
    bt_le_adv_stop();
}

void peripheral_connected(struct bt_conn *conn)
{
    iPeripheral_num++;
    for (int i = 0; i < 20; i++) {
        if (peripheralconn_list[i] == NULL) {
            // 引用 +1（Zephyr 必须这么做）
            peripheralconn_list[i] = bt_conn_ref(conn);
            break;
        }
    }

    printk("[%s %d] iPeripheral_num is %d\n", __func__, __LINE__, iPeripheral_num);
    if(iPeripheral_num < PERIPHERAL_MAX)
    {
        peripheral_advertising_start();
    }
}

void peripheral_disconnected(struct bt_conn *conn)
{
    iPeripheral_num--;
    for (int i = 0; i < 20; i++) {
        if (peripheralconn_list[i] == conn) {
            bt_conn_unref(peripheralconn_list[i]);
            peripheralconn_list[i] = NULL;
            break;
        }
    }
    printk("[%s %d] iPeripheral_num is %d\n", __func__, __LINE__, iPeripheral_num);

    if(iPeripheral_num < PERIPHERAL_MAX)
    {
        peripheral_advertising_start();
    }
}

/*
广播期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, 8);
连接期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, 8);
*/