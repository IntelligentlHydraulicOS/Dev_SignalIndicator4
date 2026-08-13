#ifndef SENSOR_DEAL_H
#define SENSOR_DEAL_H
#include <zephyr/bluetooth/addr.h>
#include <stdint.h>

extern void Sensor_dataParse(uint8_t *uiData, uint16_t uiLen, const bt_addr_le_t *addr);
extern void Sensor_Addr_Add(const bt_addr_le_t *addr, uint8_t uiSensorType, uint8_t uiDevID, uint8_t uiPositionID);
extern void Sensor_Addr_Del(const bt_addr_le_t *addr);
extern void Sensor_Info_Respone_Interval();
extern void Sensor_Info_Respone_All(uint8_t uiFromPort);
extern void cmd_Get_Sensor_Respose(uint8_t uiFromPort);
extern void Sensor_Addr_Set_Status(const bt_addr_le_t *addr, uint8_t uiStatus);
extern void Sensor_DisconnectCentral(uint8_t *uiMacAddr);
#endif