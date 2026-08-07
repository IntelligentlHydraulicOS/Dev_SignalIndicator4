
#include "deca_device_api.h"
#include "port.h"




decaIrqStatus_t decamutexon(void)
{
    // DW1000IRQ_enable(0);
    return 1;
}


void decamutexoff(decaIrqStatus_t s)
{
    // DW1000IRQ_enable(1);
}