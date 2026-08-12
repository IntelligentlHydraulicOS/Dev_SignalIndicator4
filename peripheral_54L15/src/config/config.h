#ifndef CONFIG_H
#define CONFIG_H

extern uint8_t uiGetMaxCentralNumber();
extern void uiSetMaxCentralNumber(uint8_t uiCentralNumber);

extern uint8_t uiGetLocalID();
extern void uiSetLocalID(uint8_t uiLocalID);

extern uint8_t uiGetBLE_Txpower();
extern void uiSetBLE_Txpower(uint8_t uiTxpower);
extern uint8_t uiGetSensorConnectType();
extern void uiSetSensorConnectType(uint8_t uiConnectType);

extern void saveConfig();
extern void nrfConfigInit();
#endif