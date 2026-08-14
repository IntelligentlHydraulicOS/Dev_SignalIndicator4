#ifndef BT_NUS_PERIPHERAL_H_
#define BT_NUS_PERIPHERAL_H_

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_UUID_NUS_VAL_PERIPHERAL \
	BT_UUID_128_ENCODE(0x0000FE60, 0x0, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_NUS_TX_PERIPHERAL \
	BT_UUID_128_ENCODE(0x0000FE62, 0x0, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_NUS_RX_PERIPHERAL \
	BT_UUID_128_ENCODE(0x0000FE61, 0x0, 0x1000, 0x8000, 0x00805F9B34FB)

#define BT_UUID_NUS_PERIPHERAL_SERVICE   BT_UUID_DECLARE_128(BT_UUID_NUS_VAL_PERIPHERAL)
#define BT_UUID_NUS_PERIPHERAL_RX        BT_UUID_DECLARE_128(BT_UUID_NUS_RX_PERIPHERAL)
#define BT_UUID_NUS_PERIPHERAL_TX        BT_UUID_DECLARE_128(BT_UUID_NUS_TX_PERIPHERAL)

enum bt_nus_peripheral_send_status {
	/** Send notification enabled. */
	BT_NUS_PERIPHERAL_SEND_STATUS_ENABLED,
	/** Send notification disabled. */
	BT_NUS_PERIPHERAL_SEND_STATUS_DISABLED,
};

struct bt_nus_peripheral_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a write request on the NUS RX
	 * Characteristic.
	 *
	 * @param[in] conn  Pointer to connection object that has received data.
	 * @param[in] data  Received data.
	 * @param[in] len   Length of received data.
	 */
	void (*received)(struct bt_conn *conn,
			 const uint8_t *const data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent as a notification and written on the NUS TX
	 * Characteristic.
	 *
	 * @param[in] conn Pointer to connection object, or NULL if sent to all
	 *                 connected peers.
	 */
	void (*sent)(struct bt_conn *conn);

	/** @brief Send state callback.
	 *
	 * Indicate the CCCD descriptor status of the NUS TX characteristic.
	 *
	 * @param[in] status Send notification status.
	 */
	void (*send_enabled)(enum bt_nus_peripheral_send_status status);

};

int bt_nus_peripheral_init(struct bt_nus_peripheral_cb *callbacks);
int bt_nus_peripheral_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);
static inline uint32_t bt_nus_peripheral_get_mtu(struct bt_conn *conn)
{
	/* According to 3.4.7.1 Handle Value Notification off the ATT protocol.
	 * Maximum supported notification is ATT_MTU - 3 */
	return bt_gatt_get_mtu(conn) - 3;
}

#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */