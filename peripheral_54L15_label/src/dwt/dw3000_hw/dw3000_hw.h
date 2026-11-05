#ifndef DW3000_HW_H
#define DW3000_HW_H

#include <stdbool.h>

extern int dw3000_hw_init(void);
extern int dw3000_hw_init_interrupt(void);
extern void dw3000_hw_fini(void);
extern void dw3000_hw_enter_low_power(void);
extern void dw3000_hw_exit_low_power(void);
extern void dw3000_hw_reset(void);
extern void dw3000_hw_wakeup(void);
extern void dw3000_hw_wakeup_pin_low(void);
extern void dw3000_hw_interrupt_enable(void);
extern void dw3000_hw_interrupt_disable(void);
extern bool dw3000_hw_interrupt_is_enabled(void);
extern void dw3000_hw_sync();

#endif
