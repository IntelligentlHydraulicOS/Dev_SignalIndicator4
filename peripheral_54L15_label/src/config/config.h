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
#endif