#ifndef ZIGBEE_DEAL_H
#define ZIGBEE_DEAL_H

#include <stdint.h>
int Zigbee_deal_init(void);

extern void sendMsgdata2Zigbeeuart(uint8_t *data, uint16_t len);
extern void Zigbee_dealsetuartReceive();
extern void sendzigbee();
extern int get_zigbee_gpio();

#endif