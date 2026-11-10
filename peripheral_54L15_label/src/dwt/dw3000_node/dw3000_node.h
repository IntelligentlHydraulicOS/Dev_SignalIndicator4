#ifndef DW3000_NODE_H
#define DW3000_NODE_H

#include <stdbool.h>
#include <stdint.h>


extern void dw3000_hw_node_init();
extern void dw3000_node_run();
extern void dw1000_sendpoll(uint64_t curr_ts);
extern bool dw1000_take_poll_response(void);
extern void get_LocalMacAddr(uint8_t *uiLocalMacAddr);
extern int dw3000_node_set_tag_node_range(uint8_t start_node_id,
					  uint8_t end_node_id);
extern uint8_t dw3000_node_get_current_tag_node_id(void);

#endif