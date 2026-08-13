#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/printk.h>

#include "nus_peripheral.h"
#include "nus_manage.h"
#include "peripheral_manage.h"
#include "uartdataparse.h"
#include "config.h"


#define PERIPHERAL_MAX 1
static int iPeripheral_num = 0;
struct bt_conn *peripheralconn_list[20] = {NULL};

/*
    advertising
*/

#define DEVICE_NAME "Sany-MasterR"
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
static struct k_work adv_work;
static void adv_work_handler(struct k_work *work)
{
    my_custom_data[0] = 0x05;
    my_custom_data[2] = uiGetLocalID();
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
#else

#define APP_BT_LE_EXT_ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN, \
                          BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL)

static void adv_sent_cb(struct bt_le_ext_adv *adv,
                        struct bt_le_ext_adv_sent_info *info)
{
    printk("[%s %d] send ad\n", __func__, __LINE__);
}

const static struct bt_le_ext_adv_cb adv_callbacks = {
    .sent = adv_sent_cb,
};

static struct bt_le_ext_adv *adv_set;
#if 0
static void adv_work_handler(struct k_work *work)
{
    /* 创建广播集 */
    int err = 0;
    err = bt_le_ext_adv_create(APP_BT_LE_EXT_ADV_PARAM, &adv_callbacks, &adv_set);
    if (err) {
        printk("Failed to create advertising set (err %d)\n", err);
        return 0;
    }

    /* 设置广播数据 */
    err = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Failed to set advertising data (err %d)\n", err);
        return 0;
    }
    
    /* 启动广播 */
    err = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        printk("Failed to start extended advertising (err %d)\n", err);
        return 0;
    }
}
#endif

static void adv_start(void)
{
    int err;
    struct bt_le_ext_adv_start_param ext_adv_start_param = {0};

    if (adv_set) {
        err = bt_le_ext_adv_stop(adv_set);
        if (err) {
            printk("Failed to stop extended advertising (err %d)\n", err);
            return;
        }
        err = bt_le_ext_adv_delete(adv_set);
        if (err) {
            printk("Failed to delete advertising set (err %d)\n", err);
            return;
        }
    }

    err = bt_le_ext_adv_create(APP_BT_LE_EXT_ADV_PARAM, &adv_callbacks, &adv_set);
    if (err) {
        printk("Failed to create advertising set (err %d)\n", err);
        return;
    }

    err = bt_le_ext_adv_set_data(adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Failed to set advertising data (err %d)\n", err);
        return;
    }

    err = bt_le_ext_adv_start(adv_set, &ext_adv_start_param);
    if (err) {
        printk("Failed to start extended advertising (err %d)\n", err);
        return;
    }
}
#endif
/*  advertising end */

static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	//int err;
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	printk("[%s %d] Received data from: %s", __func__, __LINE__, addr);
    RS485_uart_dataparse(data, len, 1);
}

static struct bt_nus_peripheral_cb nus_cb = {
	.received = bt_receive_cb,
};

int nus_peripheral_init()
{
    int err = 0;

    printk("[%s %d] peripheral init\n", __func__, __LINE__);
    err = bt_nus_peripheral_init(&nus_cb);
	if (err) {
		printk("[%s %d] Failed to initialize UART service (err: %d)\n", __func__, __LINE__, err);
		return 0;
	}

    CurrentSet_Tx_Power();
    //k_work_init(&adv_work, adv_work_handler);
	// advertising_start();
    adv_start();
    return err;
}

void peripheral_advertising_start()
{
    // advertising_start();
    adv_start();
}

void peripheral_advertising_stop()
{
    // bt_le_adv_stop();
    bt_le_ext_adv_stop(adv_set);
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

void peripheral_send_message(uint8_t *Data, uint16_t uiLen)
{
    if(iPeripheral_num <= 0)
    {
        return;
    }

    if (bt_nus_peripheral_send(NULL, Data, uiLen)) 
    {
        printk("[%s %d] Failed to send data over BLE connection\n", __func__, __LINE__);             
	}
}

/*
广播期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, 8);
连接期间： set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, 8);
*/