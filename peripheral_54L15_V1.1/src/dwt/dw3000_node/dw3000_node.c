#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#include "deca_probe_interface.h"
#include "deca_device_api.h"

#include <stdlib.h>
#include "dw3000_hw.h"
#include "dw3000_spi.h"
#include "shared_defines.h"
#include "shared_functions.h"
#include "config_options.h"
#include "RS485_deal.h"

#define ALL_MSG_SN_IDX 2
#define ALL_MSG_TAG_MAC 3
#define ALL_MSG_AUCHOR_NODEID 9
#define FINAL_MSG_POLL_TX_TS_IDX 12
#define FINAL_MSG_RESP_RX_TS_IDX 20
#define FINAL_MSG_FINAL_TX_TS_IDX 28
#define LOCALNODEID 3

#define MAXTAGNUMBER 10

typedef struct tag_dw1000_state_t
{
    uint8_t uicharacter; //1:Tag 2:Anchor
    uint8_t uiStep;      //0:init 1:tag send To Anchor epool  2:anchor receive epool 3: anchor send To tag resp  4:tag receive resp 5: tag send To anchor final 6: anchor receive final
    //tag: 0 => 1 => 4 => 5
    //anchor 0 => 2 => 3 => 6
    uint8_t uiTagCurrentID[15];    //0-254
    uint8_t uiAnchorCurrentID;
    uint8_t uiTagNodeID;
    uint8_t uiTimeoutN;    //   5000 * 1.0256us * n
    uint8_t uiLocalNodeID;
    uint8_t uiMacAddr[6];
}dw1000_state_t;

dw1000_state_t Dw1000_State = {0};
static uint8_t uiDiffLocalMacAddr(uint8_t *uiLocalMacAddr, uint8_t *uiCurrentMacAddr);

#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

extern dwt_txconfig_t txconfig_options;
extern dwt_txconfig_t txconfig_options_ch9;

static void rx_ok_cb(const dwt_cb_data_t *cb_data);
static void rx_to_cb(const dwt_cb_data_t *cb_data);
static void rx_err_cb(const dwt_cb_data_t *cb_data);
static void tx_done_cb(const dwt_cb_data_t *cb_data);

static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21,    0, 0};
static uint8_t tx_resp_msg[] = {0x41, 0x89, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0};
static uint8_t tx_final_msg[] = {0x41, 0x90, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


static dwt_callbacks_s dw3000node_callbacks;

void get_LocalMacAddr(uint8_t *uiLocalMacAddr)
{
    memcpy(uiLocalMacAddr, Dw1000_State.uiMacAddr, 6 * sizeof(uint8_t));
}

static uint8_t uiDiffLocalMacAddr(uint8_t *uiLocalMacAddr, uint8_t *uiCurrentMacAddr)
{
    uint8_t uiRet = 1;

    for(int i = 0; i < 6; i++)
    {
        if(uiLocalMacAddr[i] != uiCurrentMacAddr[i]/*Dw1000_State.uiMacAddr[i]*/)
        {
            uiRet = 0;
            break;
        }
    }
    return uiRet;
}

void dw3000_hw_node_init()
{
    dw3000_hw_init();
    dw3000_hw_init_interrupt();
    
    uint32_t low  = NRF_FICR->DEVICEADDR[0];
	uint32_t high = NRF_FICR->DEVICEADDR[1];

    Dw1000_State.uiMacAddr[5] = (low >> 0)  & 0xFF;
    Dw1000_State.uiMacAddr[4] = (low >> 8)  & 0xFF;
    Dw1000_State.uiMacAddr[3] = (low >> 16) & 0xFF;
    Dw1000_State.uiMacAddr[2] = (low >> 24) & 0xFF;
    Dw1000_State.uiMacAddr[1] = (high >> 0) & 0xFF;
    Dw1000_State.uiMacAddr[0] = (high >> 8) & 0xFF;
    printk("[%s %d] addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, Dw1000_State.uiMacAddr[0], Dw1000_State.uiMacAddr[1], Dw1000_State.uiMacAddr[2], Dw1000_State.uiMacAddr[3], Dw1000_State.uiMacAddr[4], Dw1000_State.uiMacAddr[5]);

    dw3000node_callbacks.cbCiaDone = NULL;
    dw3000node_callbacks.cbDualSPIEv = NULL;
    dw3000node_callbacks.cbFrmRdy = NULL;
    dw3000node_callbacks.cbRxErr = rx_err_cb;
    dw3000node_callbacks.cbRxOk = rx_ok_cb;
    dw3000node_callbacks.cbRxTo = rx_to_cb;
    dw3000node_callbacks.cbSPIErr = NULL;
    dw3000node_callbacks.cbSPIRDErr = NULL;
    dw3000node_callbacks.cbSPIRdy = NULL;
    dw3000node_callbacks.cbSysEvent = NULL;
    dw3000node_callbacks.cbTxDone = tx_done_cb;
    dw3000node_callbacks.devErr = NULL;

    Dw1000_State.uicharacter = 2;
    Dw1000_State.uiTagNodeID = 1;
    Dw1000_State.uiStep = 0;
    Dw1000_State.uiTimeoutN = 0;
    Dw1000_State.uiLocalNodeID = LOCALNODEID;

    if(Dw1000_State.uicharacter == 1)
    {
        get_LocalMacAddr(tx_poll_msg + ALL_MSG_TAG_MAC);
        get_LocalMacAddr(tx_final_msg + ALL_MSG_TAG_MAC);
        tx_poll_msg[ALL_MSG_AUCHOR_NODEID] = Dw1000_State.uiTagNodeID;
        tx_final_msg[ALL_MSG_AUCHOR_NODEID] = Dw1000_State.uiTagNodeID;
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        tx_resp_msg[ALL_MSG_AUCHOR_NODEID] = Dw1000_State.uiLocalNodeID;
    }
}

void dw3000_node_run()
{
    dwt_config_t config = {
        .chan = 5,                /* Channel number. */
        .txPreambLength = DWT_PLEN_1024,     /* Preamble length. Used in TX only. */
        .rxPAC = DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
        .txCode = 4,                /* TX preamble code. Used in TX only. */
        .rxCode = 4,                /* RX preamble code. Used in RX only. */
        .sfdType = DWT_SFD_DW_8,    /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
        .dataRate = DWT_BR_6M8,       /* Data rate. */
        .phrMode = DWT_PHRMODE_STD,  /* PHY header mode. */
        .phrRate = DWT_PHRRATE_STD,  /* PHY header rate. */
        .sfdTO = (1024 + 1 + 8 -8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
        .stsMode = DWT_STS_MODE_OFF, /* STS disabled */
        .stsLength = DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
        .pdoaMode = DWT_PDOA_M0       /* PDOA mode off */
    };

    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dw3000_spi_speed_fast();
    
    dw3000_hw_reset();

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ 
    {
        printk("[%s %d]-----fail to dwt_checkidlerc\r\n",  __func__, __LINE__);
        k_msleep(1000 * 3);
    };
    
    if (dwt_initialise(DWT_DW_IDLE) == DWT_ERROR)
    {
        //test_run_info((unsigned char *)"INIT FAILED     ");
        printk("fail to dwt init\r\n");
        while (1) {printk("[%s %d] fail to dwt init\r\n", __func__, __LINE__);  };
    }

    /* This is put here for testing, so that we can see the receiver ON/OFF pattern using an oscilloscope. */
    // dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    if (dwt_configure(&config))
    {
        printk("[%s %d] CONFIG FAILED", __func__, __LINE__);
        while (1) { };
    }

    if (config.chan == 5)
    {
        dwt_configuretxrf(&txconfig_options);
    }
    else
    {
        dwt_configuretxrf(&txconfig_options_ch9);
    }

    printk("[%s %d] success to dw3000 init\r\n", __func__, __LINE__);
    /* Configure SNIFF mode. */
    // dwt_setsniffmode(1, SNIFF_ON_TIME , SNIFF_OFF_TIME);

    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
    
    dwt_setcallbacks(&dw3000node_callbacks);

    dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXPHE_BIT_MASK
                         | DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK, 0, DWT_ENABLE_INT);
    // dw3000_hw_interrupt_enable();
    dwt_setrxtimeout(7000);
    if(Dw1000_State.uicharacter == 2)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}
/****************************************************************************
    START MESSAGE
*****************************************************************************/

void dw1000_sendMsg1(uint8_t *sendbuf, uint16_t sendlen, uint8_t txmode)
{
    int iRet = 0;
    if(sendlen == sizeof(tx_poll_msg) && Dw1000_State.uicharacter == 1)
    {
        Dw1000_State.uiTagCurrentID[Dw1000_State.uiTagNodeID] = (Dw1000_State.uiTagCurrentID[Dw1000_State.uiTagNodeID] + 1) % 255;
        sendbuf[2] = Dw1000_State.uiTagCurrentID[Dw1000_State.uiTagNodeID];
    }

    if(sendlen == sizeof(tx_poll_msg) || sendlen == sizeof(tx_resp_msg))
    {
        // dwt_writetxfctrl(0, 0, 0);
        dwt_writetxdata(sendlen, sendbuf, 0);
        dwt_writetxfctrl(sendlen, 0, 1);
        // printf("current time (0x%llx)\r\n", get_dwt_systime_u64());
        iRet = dwt_starttx(txmode);
    }
    else if(sendlen == sizeof(tx_final_msg))
    {
        // dwt_writetxfctrl(0, 0, 0);
        dwt_writetxdata(sendlen, sendbuf, 0);
        dwt_writetxfctrl(sendlen, 0, 1);
        iRet = dwt_starttx(txmode);
    }
    else
    {
        dwt_writetxdata(sendlen, sendbuf, 0);
        dwt_writetxfctrl(sendlen, 0, 1);
        iRet = dwt_starttx(DWT_START_TX_IMMEDIATE);
    }

    if(iRet == DWT_SUCCESS)
    {
        // printf("[%s %d]ID(%d)\r", __func__, __LINE__, sendbuf[2]);
    }
    else
    {
        printf("fail to send msg current time %d\r", sendlen);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

#define MAXTAGTIMEOUTN 4
void dw1000_sendpoll()
{
    if(Dw1000_State.uiTimeoutN < MAXTAGTIMEOUTN)
    {
        Dw1000_State.uiTimeoutN++;
    }
    
    if(Dw1000_State.uicharacter == 1 && (Dw1000_State.uiTimeoutN >= MAXTAGTIMEOUTN || Dw1000_State.uiStep == 0))
    {
        // dw3000_hw_sync();
        Dw1000_State.uiStep = 0;
        Dw1000_State.uiTimeoutN = 0;

        if(Dw1000_State.uiTagNodeID >= 1 && Dw1000_State.uiTagNodeID < 10)
        {
            Dw1000_State.uiTagNodeID = Dw1000_State.uiTagNodeID % 8 + 1;
        }
        else
        {
            Dw1000_State.uiTagNodeID = 1;
        }
        
        tx_poll_msg[ALL_MSG_AUCHOR_NODEID] = Dw1000_State.uiTagNodeID;
        tx_final_msg[ALL_MSG_AUCHOR_NODEID] = Dw1000_State.uiTagNodeID;
        // printk("***********************************************\n");
        dw1000_sendMsg1(tx_poll_msg, sizeof(tx_poll_msg), DWT_START_TX_IMMEDIATE);
    }
}

// #define RESP_RX_TO_FINAL_TX_DLY_UUS 3100
#define RESP_RX_TO_FINAL_TX_DLY_UUS (300 + CPU_PROCESSING_TIME)

static uint64_t auchor_poll_rx_ts;


void dw1000_parsedata(uint8_t *rx_buffer, uint16_t frame_len)
{
    uint8_t uiCurrentID = 0;
    uint32_t final_tx_time;
    uiCurrentID = rx_buffer[2];
    
    if(frame_len == sizeof(tx_poll_msg) && rx_buffer[ALL_MSG_AUCHOR_NODEID] == Dw1000_State.uiLocalNodeID)//auchor receive poll
    {
        tx_resp_msg[2] = uiCurrentID;
        memcpy(tx_resp_msg + ALL_MSG_TAG_MAC, rx_buffer + ALL_MSG_TAG_MAC, 6 * sizeof(uint8_t));
        // memcpy(uiCurrentMacAddr1, rx_buffer + ALL_MSG_TAG_MAC, 6 * sizeof(uint8_t));
        auchor_poll_rx_ts = get_rx_timestamp_u64();
        dw1000_sendMsg1(tx_resp_msg, sizeof(tx_resp_msg), DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    }
    else if(frame_len == sizeof(tx_resp_msg) &&  rx_buffer[ALL_MSG_AUCHOR_NODEID] == Dw1000_State.uiTagNodeID && uiDiffLocalMacAddr(rx_buffer + ALL_MSG_TAG_MAC, Dw1000_State.uiMacAddr) == 1)//tag receive resp
    {
        uint64_t tag_poll_tx_ts = get_tx_timestamp_u64();
        uint64_t tag_resp_rx_ts = get_rx_timestamp_u64();

        final_tx_time = ((tag_resp_rx_ts + (RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8) + 0x4FFFF;
        dwt_setdelayedtrxtime(final_tx_time);

        /* Final TX timestamp is the transmission time we programmed plus the TX antenna delay. */
        uint64_t Tmpfinal_tx_ts = (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        /* Write all timestamps in the final message. See NOTE 11 below. */
        final_msg_set_ts_u64(&tx_final_msg[FINAL_MSG_POLL_TX_TS_IDX], tag_poll_tx_ts);
        final_msg_set_ts_u64(&tx_final_msg[FINAL_MSG_RESP_RX_TS_IDX], tag_resp_rx_ts);
        final_msg_set_ts_u64(&tx_final_msg[FINAL_MSG_FINAL_TX_TS_IDX], Tmpfinal_tx_ts);

        tx_final_msg[ALL_MSG_SN_IDX] = uiCurrentID;
        dw1000_sendMsg1(tx_final_msg, sizeof(tx_final_msg), DWT_START_TX_DELAYED);
    }
    else if(frame_len == sizeof(tx_final_msg) && rx_buffer[ALL_MSG_AUCHOR_NODEID] == Dw1000_State.uiLocalNodeID/* && uiDiffLocalMacAddr(rx_buffer + ALL_MSG_TAG_MAC, uiCurrentMacAddr1) == 1*/)//auchor receive final
    {
        uint64_t poll_tx_ts, resp_rx_ts, final_tx_ts;

        double Ra, Rb, Da, Db;
        int64_t tof_dtu;

        uint64_t auchor_resp_tx_ts = get_tx_timestamp_u64();
        uint64_t auchor_final_rx_ts = get_rx_timestamp_u64();

        /* Get timestamps embedded in the final message. */
        final_msg_get_ts_u64(&rx_buffer[FINAL_MSG_POLL_TX_TS_IDX], &poll_tx_ts);
        final_msg_get_ts_u64(&rx_buffer[FINAL_MSG_RESP_RX_TS_IDX], &resp_rx_ts);
        final_msg_get_ts_u64(&rx_buffer[FINAL_MSG_FINAL_TX_TS_IDX], &final_tx_ts);

        Ra = (double)(resp_rx_ts - poll_tx_ts);
        Rb = (double)(auchor_final_rx_ts - auchor_resp_tx_ts);
        Da = (double)(final_tx_ts - resp_rx_ts);
        Db = (double)(auchor_resp_tx_ts - auchor_poll_rx_ts);
        tof_dtu = (int64_t)(llabs(Ra * Rb - Da * Db) / (Ra + Rb + Da + Db));

        double tof = tof_dtu * DWT_TIME_UNITS;
        double distance = tof * SPEED_OF_LIGHT;
        uint16_t uiDistance = (uint16_t)round(distance * 100);
        uint8_t uiRS485Buffer[16] = {0};
        uiRS485Buffer[0] = 0x11;
        uiRS485Buffer[1] = 0x33;
        memcpy(uiRS485Buffer + 2, rx_buffer + ALL_MSG_TAG_MAC, 6 * sizeof(uint8_t));

        uiRS485Buffer[8] = Dw1000_State.uiLocalNodeID;
        uiRS485Buffer[9] = uiDistance & 0xff;
        uiRS485Buffer[10] = (uiDistance & 0xff00) >> 8;
        uiRS485Buffer[11] = 0xFA;

        sendMsgdata2RS485uart(uiRS485Buffer, 12);

        printf("[%s %d] ID(%d)  mac(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x) distance %f \r\n", __func__, __LINE__, uiCurrentID, rx_buffer[ALL_MSG_TAG_MAC], rx_buffer[ALL_MSG_TAG_MAC + 1], \
                rx_buffer[ALL_MSG_TAG_MAC + 2], rx_buffer[ALL_MSG_TAG_MAC + 3], rx_buffer[ALL_MSG_TAG_MAC + 4], rx_buffer[ALL_MSG_TAG_MAC + 5], distance);
        // printf("pool tx(0x%llx) pool rx(0x%llx) rest tx(0x%llx) rest rx(0x%llx) finnal tx(0x%llx) finnal rx(0x%llx)\r\n", poll_tx_ts, auchor_poll_rx_ts,  auchor_resp_tx_ts, resp_rx_ts, final_tx_ts, auchor_final_rx_ts);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    else
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}
/*
    END MESSAGE
*/
#define DW3000_RX_BUF_MAX 128
static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    // printk("[%s %d] rx ok cb length(%d)\n", __func__, __LINE__, cb_data->datalength);
    uint8_t  rx_buffer[DW3000_RX_BUF_MAX] = {0};

    uint16_t rx_len = cb_data->datalength;

    if (rx_len > DW3000_RX_BUF_MAX)
    {
        rx_len = DW3000_RX_BUF_MAX;
    }
    dwt_readrxdata(rx_buffer, rx_len, 0U);

    dw1000_parsedata(rx_buffer, rx_len);
    // printk("[%s %d] %x %x %x %x %x %x %x %x %x %x %x %x\n", __func__, __LINE__, rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3], rx_buffer[4], rx_buffer[5], rx_buffer[6], rx_buffer[7], rx_buffer[8], rx_buffer[9], rx_buffer[10], rx_buffer[11]);

    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiTagCurrentID[Dw1000_State.uiTagNodeID] == rx_buffer[2] && Dw1000_State.uiStep == 1 && cb_data->datalength == sizeof(tx_resp_msg))
        {
            Dw1000_State.uiStep = 4;
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        
    }
}

static void rx_to_cb(const dwt_cb_data_t *cb_data)
{
    // printk("[%s %d] rx to cb \n", __func__, __LINE__);
    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiTimeoutN < MAXTAGTIMEOUTN)
        {
            Dw1000_State.uiTimeoutN++;
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        else
        {
            printk("[%s %d] current is idle\n", __func__, __LINE__);
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    printk("[%s %d] rx err cb \n", __func__, __LINE__);
    
    if(Dw1000_State.uicharacter == 2)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

static void tx_done_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    // printk("[%s %d] tx done cb\n", __func__, __LINE__);
    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiStep == 0)
        {
            Dw1000_State.uiStep = 1;
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        else 
        {
            if(Dw1000_State.uiStep == 4)
            {
                Dw1000_State.uiStep = 0;
                Dw1000_State.uiTimeoutN = MAXTAGTIMEOUTN;
            }
            printk("[%s %d] current is idle\n", __func__, __LINE__);
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {

    }
}
