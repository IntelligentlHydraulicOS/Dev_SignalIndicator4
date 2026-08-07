#ifndef BT_CENTRAL_MANAGE_H_
#define BT_CENTRAL_MANAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/gatt_dm.h>



struct bt_nus_central_handles {

        /** Handle of the NUS RX characteristic, as provided by
	 *  a discovery.
         */
	uint16_t rx;

        /** Handle of the NUS TX characteristic, as provided by
	 *  a discovery.
         */
	uint16_t tx;

        /** Handle of the CCC descriptor of the NUS TX characteristic,
	 *  as provided by a discovery.
         */
	uint16_t tx_ccc;
};

struct bt_central_client;
struct bt_nus_central_client_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a notification of the NUS TX
	 * Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] data Received data.
	 * @param[in] len Length of received data.
	 *
	 * @retval BT_GATT_ITER_CONTINUE To keep notifications enabled.
	 * @retval BT_GATT_ITER_STOP To disable notifications.
	 */
	uint8_t (*received)(struct bt_central_client *nus, const uint8_t *data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent and written to the NUS RX Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] err ATT error code.
	 * @param[in] data Transmitted data.
	 * @param[in] len Length of transmitted data.
	 */
	void (*sent)(struct bt_central_client *nus, uint8_t err, const uint8_t *data, uint16_t len);

	/** @brief TX notifications disabled callback.
	 *
	 * TX notifications have been disabled.
	 *
	 * @param[in] nus  NUS Client instance.
	 */
	void (*unsubscribed)(struct bt_central_client *nus);
};

struct bt_central_client {

        /** Connection object. */
	struct bt_conn *conn;

        /** Internal state. */
	atomic_t state;

        /** Handles on the connected peer device that are needed
         * to interact with the device.
         */
	struct bt_nus_central_handles handles;

        /** GATT subscribe parameters for NUS TX Characteristic. */
	struct bt_gatt_subscribe_params tx_notif_params;

        /** GATT write parameters for NUS RX Characteristic. */
	struct bt_gatt_write_params rx_write_params;

        /** Application callbacks. */
	struct bt_nus_central_client_cb cb;
};

int nus1_central_init();
void nus_central_connected(struct bt_conn *conn);
void nus_central_disconnected(struct bt_conn *conn);
int getCentralNumber();





#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */