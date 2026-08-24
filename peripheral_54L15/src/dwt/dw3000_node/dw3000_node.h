#ifndef DW3000_NODE_H
#define DW3000_NODE_H


extern void dw3000_hw_node_init();
extern void dw3000_node_run();
extern void dw1000_sendpoll();
extern void get_LocalMacAddr(uint8_t *uiLocalMacAddr);

#endif