#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

typedef struct {
    uint8_t local_id;
    uint8_t tx_power;
    uint8_t connect_type;
    uint8_t max_connections;
    uint8_t device_number;
} app_config_t;

typedef struct {
    uint8_t start_node_id;
    uint8_t end_node_id;
    uint16_t min_interval_ms;
    uint16_t max_interval_ms;
    uint16_t active_window_s;
    uint16_t deep_sleep_s;
} uwb_settings_t;

#define UWB_MAX_TAG_NODE_ID 255U
#define UWB_MIN_INTERVAL_MS 50U
#define UWB_MAX_INTERVAL_MS 24000U
#define UWB_DEFAULT_START_NODE_ID 1U
#define UWB_DEFAULT_END_NODE_ID 10
#define UWB_DEFAULT_MIN_INTERVAL_MS 100U
#define UWB_DEFAULT_MAX_INTERVAL_MS 20000U
#define UWB_MIN_ACTIVE_WINDOW_S 10U
#define UWB_MAX_ACTIVE_WINDOW_S 60U
#define UWB_DEFAULT_ACTIVE_WINDOW_S 30U
#define UWB_MIN_DEEP_SLEEP_S 60U
#define UWB_MAX_DEEP_SLEEP_S 7200U
#define UWB_DEFAULT_DEEP_SLEEP_S 1800U

uint8_t uiGetMaxCentralNumber(void);
void uiSetMaxCentralNumber(uint8_t uiCentralNumber);

uint8_t uiGetLocalID(void);
void uiSetLocalID(uint8_t uiLocalID);

uint8_t uiGetBLE_Txpower(void);
void uiSetBLE_Txpower(uint8_t uiTxpower);
uint8_t uiGetSensorConnectType(void);
void uiSetSensorConnectType(uint8_t uiConnectType);

void saveConfig(void);
void nrfConfigInit(void);
int nrfConfigGet(app_config_t *config);
int nrfConfigSet(const app_config_t *config);
int uwb_settings_get(uwb_settings_t *settings);
int uwb_settings_set(const uwb_settings_t *settings);
#endif