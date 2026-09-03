
#include "node.h"


#include "deca_device_api.h"
#include "deca_regs.h"
#include "deca_spi.h"
#include "port.h"
#include "bq27441_driver.h"

#include <zephyr/logging/log.h>

static uint64_t get_dwt_systime_u64()
{
    uint8 ts_tab[5];
    uint64_t ts = 0;
    int i;
    dwt_readsystime(ts_tab);
    for (i = 4; i >= 0; i--)
    {
        ts <<= 8;
        ts |= ts_tab[i];
    }
    return ts;
}

static uint64_t get_rx_timestamp_u64(void)
{
    uint8 ts_tab[5];
    uint64_t ts = 0;
    int i;
    dwt_readrxtimestamp(ts_tab);
    for (i = 4; i >= 0; i--)
    {
        ts <<= 8;
        ts |= ts_tab[i];
    }
    return ts;
}

static uint64_t get_tx_timestamp_u64(void)
{
    uint8 ts_tab[5];
    uint64_t ts = 0;
    int i;
    dwt_readtxtimestamp(ts_tab);
    for (i = 4; i >= 0; i--)
    {
        ts <<= 8;
        ts |= ts_tab[i];
    }
    return ts;
}

// static void final_msg_get_ts(const uint8 *ts_field, uint32 *ts)
// {
//     int i;
//     *ts = 0;
//     for (i = 0; i < 4; i++)
//     {
//         *ts += ts_field[i] << (i * 8);
//     }
// }

// static void final_msg_set_ts(uint8 *ts_field, uint64_t ts)
// {
//     int i;
//     for (i = 0; i < 4; i++)
//     {
//         ts_field[i] = (uint8) ts;
//         ts >>= 8;
//     }
// }

static void final_msg_get_ts(const uint8 *ts_field, uint64_t *ts)
{
    int i;
    *ts = 0;
    for (i = 0; i < sizeof(uint64_t); i++)
    {
        *ts += ((uint64_t)ts_field[i]) << (i * 8);
    }
}

static void final_msg_set_ts(uint8 *ts_field, uint64_t ts)
{
    int i;
    for (i = 0; i < sizeof(uint64_t); i++)
    {
        ts_field[i] = (uint8) ts;
        ts >>= 8;
    }
}

static K_SEM_DEFINE(uwb_receive_ok, 0, 1);
LOG_MODULE_REGISTER(dw1000_node);

void dw1000_sendpoll();

#define UUS_TO_DWT_TIME 65536

#define POLL_TX_TO_RESP_RX_DLY_UUS 300
#define RESP_RX_TO_FINAL_TX_DLY_UUS 3100
#define RESP_RX_TIMEOUT_UUS 2700
#define PRE_TIMEOUT 8


#define POLL_RX_TO_RESP_TX_DLY_UUS 2750
#define RESP_TX_TO_FINAL_RX_DLY_UUS 500
#define FINAL_RX_TIMEOUT_UUS 3300

#define ALL_MSG_TAG_MAC         3
#define ALL_MSG_SN_IDX 2
#define FINAL_MSG_POLL_TX_TS_IDX 12
#define FINAL_MSG_RESP_RX_TS_IDX 20
#define FINAL_MSG_FINAL_TX_TS_IDX 28
#define FINAL_MSG_BAT_INFO_IDX 38
#define FINAL_MSG_TS_LEN 4

#define RESP_MSG_POLL_RX_TS_IDX 12
#define RESP_MSG_RESP_TX_TS_IDX 20

#define SPEED_OF_LIGHT 299702547

// static uint8 rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21, 0, 0};
// static uint8 tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21, 0, 0};

// static uint8 tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// static uint8 rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// static uint8 rx_final_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// static uint8 tx_final_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8 rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21, 0, 0};
static uint8 tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21, 0, 0};

static uint8_t tx_resp_msg[] = {0x41, 0x89, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0};
// static uint8 tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8 rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8 rx_final_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8 tx_final_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define TX_ANT_DLY 32950
#define RX_ANT_DLY 0//16505

#define FRAME_LEN_MAX 127
static uint8 rx_buffer[FRAME_LEN_MAX];

/* Declaration of static functions. */
static void rx_ok_cb(const dwt_cb_data_t *cb_data);
static void rx_to_cb(const dwt_cb_data_t *cb_data);
static void rx_err_cb(const dwt_cb_data_t *cb_data);
static void tx_conf_cb(const dwt_cb_data_t *cb_data);

typedef struct tag_dw1000_state_t
{
    uint8_t uicharacter; //1:Tag 2:Anchor
    uint8_t uiStep;      //0:init 1:tag send To Anchor epool  2:anchor receive epool 3: anchor send To tag resp  4:tag receive resp 5: tag send To anchor final 6: anchor receive final
    //tag: 0 => 1 => 4 => 5
    //anchor 0 => 2 => 3 => 6
    uint8_t uiCurrentID;    //0-254
    uint8_t uiTimeoutN;    //   5000 * 1.0256us * n
}dw1000_state_t;

dw1000_state_t Dw1000_State;

// 组装单字节功率值
uint8 dwt_calc_txpow_byte(uint8 coarse, uint8 fine)
{
    return ((coarse & 0x7) << 5) | (fine & 0x1F);
}
// 生成完整32bit寄存器值，4字节统一功率
uint32 dwt_gen_txpower_reg(uint8 powByte)
{
    return (powByte << 24) | (powByte << 16) | (powByte << 8) | powByte;
}

void node_process_init()
{
    spi_lowrate();
    // Reset and initialise DW1000 
	reset_DW1000();

    if (dwt_initialise(DWT_LOADUCODE) == DWT_ERROR)
	{
			LOG_INF("INIT FAILED");
			while (1)
			{
				LOG_INF("==========================");
                k_msleep(3000);
			}
	}

    dwt_config_t config = {
		.chan = 5,               /* Channel number. */
		.prf = DWT_PRF_16M,     /* Pulse repetition frequency. */
		.txPreambLength = DWT_PLEN_1024,   /* Preamble length. */
		.rxPAC = DWT_PAC8,       /* Preamble acquisition chunk size. Used in RX only. */
		.txCode = 4,               /* TX preamble code. Used in TX only. */
		.rxCode = 4,               /* RX preamble code. Used in RX only. */
		.nsSFD = 1,               /* Use non-standard SFD (Boolean) */
		.dataRate = DWT_BR_6M8,     /* Data rate. */
		.phrMode = DWT_PHRMODE_STD, /* PHY header mode. */
		.sfdTO = 1081//DWT_SFDTOC_DEF//(1025 + 64 - 8) /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
	};

    Dw1000_State.uicharacter = 2;
    Dw1000_State.uiCurrentID = 0;
    Dw1000_State.uiStep = 0;
    Dw1000_State.uiTimeoutN = 0;
    spi_fastrate();

	dwt_configure(&config);

    // dwt_setsmarttxpower(0);
    // dwt_write32bitreg(TX_POWER_ID, 0x25456585);
    // uint8 maxPow = dwt_calc_txpow_byte(0, 31);
    // uint32 txReg = dwt_gen_txpower_reg(maxPow);
    // dwt_write32bitreg(TX_POWER_ID, txReg);

    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Register RX call-back. */
    dwt_setcallbacks(&tx_conf_cb, &rx_ok_cb, &rx_to_cb, &rx_err_cb);

    // dwt_setinterrupt(DWT_INT_TFRS | DWT_INT_RFCG | DWT_INT_RFTO | DWT_INT_RXPTO | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL | DWT_INT_SFDT, 1);
    dwt_setinterrupt(DWT_INT_TFRS | DWT_INT_RFCG | DWT_INT_RFTO | DWT_INT_RXPTO | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL | DWT_INT_SFDT, 1);

    dwt_setrxaftertxdelay(50);

    /* Set response frame timeout. */
    dwt_setrxtimeout(7000);

    DW1000IRQ_enable(1);
    // uint32_t mask = SYS_STATUS_RXDFR | SYS_STATUS_TXFRS | SYS_STATUS_AAT;
    // dwt_write32bitreg(SYS_MASK_ID, mask);
    if(Dw1000_State.uicharacter != 1)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

static uint64_t auchor_poll_rx_ts;
static uint64_t auchor_resp_tx_ts;
static uint64_t auchor_final_rx_ts;

static uint64_t tag_poll_tx_ts;
static uint64_t tag_resp_rx_ts;
// static uint64_t tag_final_tx_ts;

void dw1000_sendMsg1(uint8 *sendbuf, uint16 sendlen, uint8 txmode)
{
    int iRet = 0;
    if(sendlen == 12 && Dw1000_State.uicharacter == 1)
    {
        Dw1000_State.uiCurrentID = (Dw1000_State.uiCurrentID + 1) % 255;
        sendbuf[2] = Dw1000_State.uiCurrentID;
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
        iRet = dwt_starttx(DWT_START_TX_IMMEDIATE);
    }

    if(iRet == DWT_SUCCESS)
    {
        // printf("ID(%d) len(%d)\r", sendbuf[2], sendlen);
    }
    else
    {
        printf("fail to send msg current time(0x%llx) %d\r", get_dwt_systime_u64(), sendlen);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

void dw1000_sendpoll()
{
    if(Dw1000_State.uiTimeoutN < 10)
    {
        Dw1000_State.uiTimeoutN++;
    }
    
    if(Dw1000_State.uicharacter == 1 && (Dw1000_State.uiTimeoutN >= 10 || Dw1000_State.uiStep == 0))
    {
        Dw1000_State.uiStep = 0;
        Dw1000_State.uiTimeoutN = 0;

        dw1000_sendMsg1(tx_poll_msg, sizeof(tx_poll_msg), DWT_START_TX_IMMEDIATE);
    }
}

void dw1000_parsedata(uint8 *rx_buffer, uint16 frame_len)
{
    uint8 uiCurrentID = 0;
    uint32 final_tx_time;
    uiCurrentID = rx_buffer[2];
    
    if(frame_len == sizeof(rx_poll_msg))//auchor receive poll
    {
        
        tx_resp_msg[2] = uiCurrentID;
        tx_resp_msg[9] = rx_buffer[9];
        memcpy(tx_resp_msg + ALL_MSG_TAG_MAC, rx_buffer + ALL_MSG_TAG_MAC, 6 * sizeof(uint8_t));
        auchor_poll_rx_ts = get_rx_timestamp_u64();
#if 0
        uint32 resp_tx_time;
        resp_tx_time = ((auchor_poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8) + /*0x2661AC*/ 0x2F0000;
        uint64_t Tmpresp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        final_msg_set_ts(tx_resp_msg + RESP_MSG_POLL_RX_TS_IDX, auchor_poll_rx_ts);
        final_msg_set_ts(tx_resp_msg + RESP_MSG_RESP_TX_TS_IDX, Tmpresp_tx_ts);
        
        dwt_setdelayedtrxtime(resp_tx_time);
        dw1000_sendMsg1(tx_resp_msg, sizeof(tx_resp_msg), DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
        // printf("p_rx:%llx, r_tx:0x%llx, resp_tx(0x%lx)\r\n", auchor_poll_rx_ts, Tmpresp_tx_ts, resp_tx_time);
        
#else
    dw1000_sendMsg1(tx_resp_msg, sizeof(tx_resp_msg), DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
#endif   

    }
    else if(frame_len == sizeof(rx_resp_msg))//tag receive resp
    {
        tag_poll_tx_ts = get_tx_timestamp_u64();
        tag_resp_rx_ts = get_rx_timestamp_u64();

        final_tx_time = ((tag_resp_rx_ts + (RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8) + 0x2F0000;
        dwt_setdelayedtrxtime(final_tx_time);

        /* Final TX timestamp is the transmission time we programmed plus the TX antenna delay. */
        uint64_t Tmpfinal_tx_ts = (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        /* Write all timestamps in the final message. See NOTE 11 below. */
        final_msg_set_ts(&tx_final_msg[FINAL_MSG_POLL_TX_TS_IDX], tag_poll_tx_ts);
        final_msg_set_ts(&tx_final_msg[FINAL_MSG_RESP_RX_TS_IDX], tag_resp_rx_ts);
        final_msg_set_ts(&tx_final_msg[FINAL_MSG_FINAL_TX_TS_IDX], Tmpfinal_tx_ts);

        tx_final_msg[ALL_MSG_SN_IDX] = uiCurrentID;
        tx_final_msg[FINAL_MSG_BAT_INFO_IDX] = bat_info.soc;
        dw1000_sendMsg1(tx_final_msg, sizeof(tx_final_msg), DWT_START_TX_DELAYED);

        // printf("p_tx:%llx, r_rx:0x%llx, final_tx(0x%llx)\r\n", tag_poll_tx_ts, tag_resp_rx_ts, Tmpfinal_tx_ts);
    }
    else if(frame_len == sizeof(rx_final_msg))//auchor receive final
    {
        uint64_t poll_tx_ts, resp_rx_ts, final_tx_ts;
        // uint64_t poll_rx_ts_32, resp_tx_ts_32, final_rx_ts_32;
        double Ra, Rb, Da, Db;
        int64_t tof_dtu;
        auchor_resp_tx_ts = get_tx_timestamp_u64();
        auchor_final_rx_ts = get_rx_timestamp_u64();

        /* Get timestamps embedded in the final message. */
        final_msg_get_ts(&rx_buffer[FINAL_MSG_POLL_TX_TS_IDX], &poll_tx_ts);
        final_msg_get_ts(&rx_buffer[FINAL_MSG_RESP_RX_TS_IDX], &resp_rx_ts);
        final_msg_get_ts(&rx_buffer[FINAL_MSG_FINAL_TX_TS_IDX], &final_tx_ts);

        Ra = (double)(resp_rx_ts - poll_tx_ts);
        Rb = (double)(auchor_final_rx_ts - auchor_resp_tx_ts);
        Da = (double)(final_tx_ts - resp_rx_ts);
        Db = (double)(auchor_resp_tx_ts - auchor_poll_rx_ts);
        tof_dtu = (int64_t)((Ra * Rb - Da * Db) / (Ra + Rb + Da + Db));

        double tof = tof_dtu * DWT_TIME_UNITS;
        double distance = tof * SPEED_OF_LIGHT;

        printf("distance %f bat_lvl %d\r\n", distance, rx_buffer[FINAL_MSG_BAT_INFO_IDX]);
        // printf("pool tx(0x%llx) pool rx(0x%llx) rest tx(0x%llx) rest rx(0x%llx) finnal tx(0x%llx) finnal rx(0x%llx)\r\n", poll_tx_ts, auchor_poll_rx_ts,
        //     auchor_resp_tx_ts, resp_rx_ts, final_tx_ts, auchor_final_rx_ts);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    else
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }

    // printf("************ID (%d)******nodeID(%d)****buflen(%d)*****\r\n", uiCurrentID, rx_buffer[9], frame_len);
}

void dw1000_isr_ok_receive()
{
    k_sem_give(&uwb_receive_ok);
}


void dw1000_isr_receivemsg(void)
{
    // uint8 rx_buffer[127] = {0};
    // uint32 status_reg = 0;
    // uint16 frame_len = 0;

    // uint8  fctrl[2];
    
    while(1)
    {
        k_sem_take(&uwb_receive_ok, K_FOREVER);
        // printf("***********enter****************\r");
#if 1
        dwt_isr();
#else

        // DW1000IRQ_enable(0);
        while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_TXFRS | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
        { 
            // printf("*********0x%lx**************", status_reg);
            k_usleep(10);
        }

        if (status_reg & SYS_STATUS_RXFCG)
        {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_GOOD);
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;

            dwt_readfromdevice(RX_BUFFER_ID, 0, 2, fctrl);

            if((status_reg & SYS_STATUS_AAT) && ((fctrl[0] & 0x20) == 0))
            {
                dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_AAT); // Clear AAT status bit in register

                printf("aat*****\r\n");
            }
            if (frame_len <= 127)
            {
                dwt_readrxdata(rx_buffer, frame_len, 0);
                // LOG_INF("*****len(%d)***%x %x %x %x************", frame_len, rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
                // printf("%s\r\n", rx_buffer);

                dw1000_parsedata(rx_buffer, frame_len);
                continue;
            }
        }
        if(status_reg & SYS_STATUS_ALL_RX_ERR)
        {
            if(status_reg & SYS_STATUS_RXPHE)
            {
                // printf("***************1*****************\r");
            }

            if(status_reg & SYS_STATUS_RXFCE)
            {
                printf("***************2*****************\r");
            }

            if(status_reg & SYS_STATUS_RXRFSL)
            {
                printf("***************3*****************\r");
            }

            if(status_reg & SYS_STATUS_RXSFDTO)
            {
                // printf("***************4*****************\r");
            }

            if(status_reg & SYS_STATUS_AFFREJ)
            {
                printf("***************5*****************\r");
            }

            if(status_reg & SYS_STATUS_LDEERR)
            {
                printf("***************6*****************\r");
            }

            uint32_t rx_all_err = SYS_STATUS_RXOVRR | SYS_STATUS_RXPTO |
                      SYS_STATUS_RXFCE | SYS_STATUS_RXSFDTO | SYS_STATUS_ALL_RX_ERR;
            uint32_t mask = SYS_STATUS_RXDFR | rx_all_err | SYS_STATUS_TXFRS | SYS_STATUS_AAT;
            dwt_write32bitreg(SYS_MASK_ID, mask);
            // printf("***************2*****0x%x****", status_reg);
            dwt_forcetrxoff();
            dwt_rxreset();
        }

        if(status_reg & SYS_STATUS_TXFRS)// Handle TX confirmation event
        {
            printf("*SYS_STATUS_TXFRS*\r");

            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_TX);
            // my_dwt_idle();
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            continue;
        }
        if(status_reg & SYS_STATUS_ALL_RX_TO)// Handle frame reception/preamble detect timeout events
        {
            printf("***************SYS_STATUS_ALL_RX_TO****************\r");
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXRFTO); // Clear RX timeout event bits

            dwt_forcetrxoff();
            dwt_rxreset();
        }
#if 1
        // 2. 关键：清除所有接收标志（正常帧+全部错误位）
        // uint32_t rx_clear_mask = SYS_STATUS_RXDFR | SYS_STATUS_ALL_RX_ERR;
        // dwt_write32bitreg(SYS_STATUS_ID, status_reg & rx_clear_mask);

        // if (status_reg & SYS_STATUS_RXOVRR)
        // {
        //     dwt_rxreset(); // 清空接收FIFO，复位接收状态机
        // }

        // uint32_t mask = SYS_STATUS_RXDFR | SYS_STATUS_TXFRS | SYS_STATUS_AAT;
        // dwt_write32bitreg(SYS_MASK_ID, mask);
        // uint32_t st = dwt_read32bitreg(SYS_STATUS_ID);
        // dwt_write32bitreg(SYS_STATUS_ID, st & (SYS_STATUS_TXFRS | SYS_STATUS_AAT));
        // dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS | SYS_STATUS_AAT);
        // dwt_setinterrupt(DWT_INT_RFCG/* | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL | DWT_INT_SFDT*/, 1);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
#endif
        // DW1000IRQ_enable(1);
        // printf("exit\r");
#endif

    }
}

K_THREAD_DEFINE(dw1000_isr_thread_id, 1024, dw1000_isr_receivemsg, NULL, NULL, NULL, 4, 0, 0);

//tag: 0 => 1 => 4 => 5
//anchor 0 => 2 => 3 => 6
static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    int i;

    /* Clear local RX buffer to avoid having leftovers from previous receptions. This is not necessary but is included here to aid reading the RX
     * buffer. */
    for (i = 0 ; i < FRAME_LEN_MAX; i++ )
    {
        rx_buffer[i] = 0;
    }

    /* A frame has been received, copy it to our local buffer. */
    if (cb_data->datalength <= FRAME_LEN_MAX)
    {
        dwt_readrxdata(rx_buffer, cb_data->datalength, 0);
        dw1000_parsedata(rx_buffer, cb_data->datalength);
    }

    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiCurrentID == rx_buffer[2] && Dw1000_State.uiStep == 1 && cb_data->datalength == 30)
        {
            Dw1000_State.uiStep = 4;
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        if(cb_data->datalength == 12)
        {
            Dw1000_State.uiCurrentID = rx_buffer[2];
            Dw1000_State.uiStep = 2;
        }
        else if(Dw1000_State.uiCurrentID == rx_buffer[2] && cb_data->datalength == 38 && Dw1000_State.uiStep == 3)
        {
            Dw1000_State.uiStep = 0;
        }
    }

    // printf("current ID(%d) ID(%d) length(%d) step(%d)\r\n", Dw1000_State.uiCurrentID, rx_buffer[2], cb_data->datalength, Dw1000_State.uiStep);
}

static void rx_to_cb(const dwt_cb_data_t *cb_data)
{
    // printf("***************rx_to_cb****************\r\n");

    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiTimeoutN < 10)
        {
            Dw1000_State.uiTimeoutN++;
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    printf("***************rx_err_cb****************\r\n");
    if(Dw1000_State.uicharacter == 2)
    {
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    
}

static void tx_conf_cb(const dwt_cb_data_t *cb_data)
{
    if(Dw1000_State.uicharacter == 1)
    {
        if(Dw1000_State.uiStep == 0)
        {
            Dw1000_State.uiStep = 1;
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        else if(Dw1000_State.uiStep == 4)
        {
            Dw1000_State.uiStep = 0;
        }
    }
    else if(Dw1000_State.uicharacter == 2)
    {
        if(Dw1000_State.uiStep == 2)
        {
            Dw1000_State.uiStep = 3;
        }
        // dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    // printf("***************tx_conf_cb****************step(%d)\r\n", Dw1000_State.uiStep);
}