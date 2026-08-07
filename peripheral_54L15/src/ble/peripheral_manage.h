#ifndef BT_PERIPHERAL_MANAGE_H_
#define BT_PERIPHERAL_MANAGE_H_

#ifdef __cplusplus
extern "C" {
#endif


int nus_peripheral_init();
void peripheral_advertising_start();
void peripheral_connected(struct bt_conn *conn);
void peripheral_disconnected(struct bt_conn *conn);






#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */