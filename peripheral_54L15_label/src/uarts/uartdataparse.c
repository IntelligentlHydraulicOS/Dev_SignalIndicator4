#include <zephyr/sys/reboot.h>

#include "uartdataparse.h"
#include "ledlight.h"
// #include "zigbee_deal.h"
#include "config.h"
#include "sensordeal.h"
// #include "RS485_deal.h"
#include "nus_manage.h"
#include "peripheral_manage.h"
#include "dw3000_node.h"

static void cmd_Set_data_parse(uint8_t *puiData, uint16_t uiLen, uint8_t uiFromPort)
{
    uint8_t uiCMD_ID = puiData[0];

    switch (uiCMD_ID)
    {
        case 0x1://set txpower
            if(uiLen == 2)
            {
                if(puiData[1] < 0x9 && puiData[1] != uiGetBLE_Txpower())
                {
                    uiSetBLE_Txpower(puiData[1]);
                    // CurrentSet_Tx_Power();
                }
            }
		break;
        case 0x2://set connectNumber
            if(uiLen == 2)
            {
                uiSetMaxCentralNumber(puiData[1]);
            }
		break;
        case 3: //Set LocalID
            if(uiLen == 2)
            {
                uiSetLocalID(puiData[1]);
            }
		break;
        case 5: //BT ota 
            if(uiLen == 1)
            {
                
            }
		break;
        case 7: //disconn MAC 
            if(uiLen == 7)
            {
                Sensor_DisconnectCentral(puiData + 1);
            }
		break;
        case 8: //set conn sensor type 
            if(uiLen == 2)
            {
                uiSetSensorConnectType(puiData[1]);
            }
		break;
        case 9: //save config 
            if(uiLen == 1)
            {
                saveConfig();
            }
		break;
        case 0xF8: //MCU reset
        {
            sys_reboot(SYS_REBOOT_WARM); 
        }
        break;
        default:
		    break;
    }
}

static void cmd_Get_data_parse(uint8_t *puiData, uint16_t uiLen, uint8_t uiFromPort)
{
    uint8_t uiBuffer[128] = {0}, uiRet = 1;
	uint8_t uiCMD_ID = puiData[0];
	char *myversion = "V 1.0.1_202608111";

    uiBuffer[0] = 0x11;
	uiBuffer[1] = 0x22;
	uiBuffer[2] = 0x1;
	uiBuffer[4] = uiCMD_ID;

    switch (uiCMD_ID)
    {
        case 0x1: //get txpower
            uiBuffer[3] = 0x2;
            uiBuffer[5] = uiGetBLE_Txpower();
            break;
        case 0x2: //get connectN
            uiBuffer[3] = 0x2;
            uiBuffer[5] = uiGetMaxCentralNumber();
            break;
        case 0x3: //get LocalID
            uiBuffer[3] = 0x2;
            uiBuffer[5] = uiGetLocalID();
            break;
        case 0x6: //get local mac addr
            uiBuffer[3] = 0x7;
            get_LocalMacAddr(uiBuffer + 5);
            uiRet = 1;
            break;
        case 0x8: //set conn sensor type 
            uiBuffer[3] = 0x2;
		    uiBuffer[5] = uiGetSensorConnectType();
            break;
        case 0xFA: //get version
            memcpy(uiBuffer + 5, myversion, strlen(myversion));
		    uiBuffer[3] = 0x2 + strlen(myversion);
            break;
        case 0xFB: //get connect info
            cmd_Get_Sensor_Respose(uiFromPort);
            uiRet = 0;
            break;
        case 0xFC: //get sensor info
            Sensor_Info_Respone_All(uiFromPort);
            uiRet = 0;
            break;
        default:
            uiRet = 0;
            break;
    }

    if(uiRet)
    {
        if(uiFromPort == 2)
        {
            // sendMsgdata2RS485uart(uiBuffer, uiBuffer[3] + 4);
        }
        else
        {
            peripheral_send_message(uiBuffer, uiBuffer[3] + 4);
        }
    }
}

static void cmd_data_parse(uint8_t *puiData, uint16_t uiLen, uint8_t uiFromPort)
{
    uint8_t uiCMD_ID = 0;
    if(uiLen > 4 && puiData[0] == 0x11 && puiData[1] == 0x22)
    {
        uiCMD_ID = puiData[2];
        switch (uiCMD_ID)
		{
			case 0x01:
			{
				cmd_Get_data_parse(puiData + 4, puiData[3], uiFromPort);
				break;
			}
			case 0x02:
			{
				cmd_Set_data_parse(puiData + 4, puiData[3], uiFromPort);
				break;
			}
			default:
				break;
		}
    }
}

void RS485_uart_dataparse(uint8_t *uiData, uint16_t uiLen, uint8_t uiFromPort)
{
    if(uiData[0] == 0x00 && uiData[1] == 0x33 && uiLen >=5)
    {
        Led_ParseData(uiData, uiLen, uiFromPort);
    }
    else if(uiData[0] == 0xA1 && uiData[1] == 0xA2)
    {
        // sendMsgdata2Zigbeeuart(uiData, uiLen);
    }
    else if(uiData[0] == 0x11 && uiData[1] == 0x22)
    {
        cmd_data_parse(uiData, uiLen, uiFromPort);
    }
}