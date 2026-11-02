#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

typedef struct {
    uint8_t local_id;
    uint8_t tx_power;
    uint8_t connect_type;
    uint8_t max_connections;
    uint8_t device_number;
    uint8_t uiUWBInterval;
} app_config_t;
extern uint8_t uiGetMaxCentralNumber(void);
extern void uiSetMaxCentralNumber(uint8_t uiCentralNumber);

extern uint8_t uiGetLocalID(void);
extern void uiSetLocalID(uint8_t uiLocalID);

extern uint8_t uiGetBLE_Txpower(void);
extern void uiSetBLE_Txpower(uint8_t uiTxpower);
extern uint8_t uiGetSensorConnectType(void);
extern void uiSetSensorConnectType(uint8_t uiConnectType);

extern void saveConfig(void);
extern void nrfConfigInit(void);

extern void uiSetUWBInterval(uint8_t uiUWBInterval);
extern uint8_t uiGetUWBInterval();
extern int nrfConfigGet(app_config_t *config);
extern int nrfConfigSet(const app_config_t *config);
#endif