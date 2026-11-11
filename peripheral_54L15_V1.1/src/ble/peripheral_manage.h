#ifndef BT_PERIPHERAL_MANAGE_H_
#define BT_PERIPHERAL_MANAGE_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/bluetooth/conn.h>

extern int nus_peripheral_init();
extern void peripheral_advertising_start();
extern void peripheral_connected(struct bt_conn *conn);
extern void peripheral_disconnected(struct bt_conn *conn);
extern void peripheral_send_message(uint8_t *Data, uint16_t uiLen);
extern void peripheral_advertising_restart();

#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */