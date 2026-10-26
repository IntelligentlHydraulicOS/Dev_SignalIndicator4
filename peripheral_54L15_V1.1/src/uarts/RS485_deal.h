#ifndef RS485_DEAL_H
#define RS485_DEAL_H

#include <stdint.h>
int RS485_deal_init(void);

void sendMsgdata2RS485uart(const uint8_t *const data, uint16_t len);
void RS485_dealsetuartReceive();

#endif