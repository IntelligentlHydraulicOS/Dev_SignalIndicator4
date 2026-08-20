#ifndef UARTDATAPARSE_H
#define UARTDATAPARSE_H

#include <stdint.h>

extern void RS485_uart_dataparse(uint8_t *uiData, uint16_t uiLen, uint8_t uiFromPort);


#endif