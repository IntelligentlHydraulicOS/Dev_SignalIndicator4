#ifndef DW3000_NODE_H
#define DW3000_NODE_H

#include <stdbool.h>


extern void dw3000_hw_node_init();
extern void dw3000_node_run();
extern void dw1000_sendpoll(uint64_t curr_ts);
extern bool dw1000_take_poll_response(void);
extern void get_LocalMacAddr(uint8_t *uiLocalMacAddr);

#endif