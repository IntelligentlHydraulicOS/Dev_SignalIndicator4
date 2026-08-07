#ifndef BT_NUS_MANAGE_H_
#define BT_NUS_MANAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

void nus_security_init();
void set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl);







#ifdef __cplusplus
}
#endif
#endif /* BT_NUS_H_ */