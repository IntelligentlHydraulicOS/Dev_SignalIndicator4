#ifndef ZIGBEE_DEAL_H
#define ZIGBEE_DEAL_H

#include <stdint.h>
int Zigbee_deal_init(void);

void sendMsgdata2Zigbeeuart(const uint8_t *const data, uint16_t len);
void Zigbee_dealsetuartReceive();
void sendzigbee();
#endif