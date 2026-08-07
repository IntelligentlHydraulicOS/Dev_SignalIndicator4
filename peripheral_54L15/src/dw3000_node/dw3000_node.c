#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>


#include "deca_probe_interface.h"
#include "deca_device_api.h"


#include "dw3000_hw.h"
#include "dw3000_spi.h"

#define SNIFF_ON_TIME  2
#define SNIFF_OFF_TIME 16
void dw3000_hw_node_init()
{
    dw3000_hw_init();
    dw3000_hw_init_interrupt();
    dw3000_hw_interrupt_disable();
/*
    dw3000_hw_reset();
    dw3000_spi_speed_slow();
*/
}

void dw3000_node_run()
{
    dwt_config_t config = {
        .chan = 5,                /* Channel number. */
        .txPreambLength = DWT_PLEN_128,     /* Preamble length. Used in TX only. */
        .rxPAC = DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
        .txCode = 9,                /* TX preamble code. Used in TX only. */
        .rxCode = 9,                /* RX preamble code. Used in RX only. */
        .sfdType = 1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
        .dataRate = DWT_BR_850K,       /* Data rate. */
        .phrMode = DWT_PHRMODE_STD,  /* PHY header mode. */
        .phrRate = DWT_PHRRATE_STD,  /* PHY header rate. */
        .sfdTO = (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
        .stsMode = DWT_STS_MODE_OFF, /* STS disabled */
        .stsLength = DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
        .pdoaMode = DWT_PDOA_M0       /* PDOA mode off */
    };

    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dw3000_spi_speed_fast();
    
    dw3000_hw_reset();

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ 
    {
        printk("-----fail to dwt_checkidlerc\r\n");
        k_msleep(1000 * 3);
    };
    printk("files name(%s) function(%s), number(%d)", __FILE__, __FUNCTION__, __LINE__);
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        //test_run_info((unsigned char *)"INIT FAILED     ");
        printk("fail to dwt init\r\n");
        while (1) {printk("fail to dwt init\r\n"); };
    }

    printk("files name(%s) function(%s), number(%d)", __FILE__, __FUNCTION__, __LINE__);
    /* This is put here for testing, so that we can see the receiver ON/OFF pattern using an oscilloscope. */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    printk("files name(%s) function(%s), number(%d)", __FILE__, __FUNCTION__, __LINE__);
    if (dwt_configure(&config))
    {
        printk("CONFIG FAILED     ");
        while (1) { };
    }

    printk("files name(%s) function(%s), number(%d)", __FILE__, __FUNCTION__, __LINE__);
    /* Configure SNIFF mode. */
    dwt_setsniffmode(1, SNIFF_ON_TIME , SNIFF_OFF_TIME);

}