#ifndef DW3000_NODE_H
#define DW3000_NODE_H


extern void dw3000_hw_node_init();
extern void dw3000_node_run();
extern void dw1000_sendpoll();
extern void get_LocalMacAddr(uint8_t *uiLocalMacAddr);
extern uint8_t get_uwbNodeID();
extern void flash_uwbNodeID();
extern int get_uwb_gpio();

#endif