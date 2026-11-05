#include "sensordeal.h"
#include "RS485_deal.h"
#include "central_manage.h"
#include "peripheral_manage.h"
#include "bma253_example.h"
#include <stdlib.h> 
#pragma pack(push, 1)
typedef struct tag_sensor_info_t
{
    uint8_t ui_addr[6];
    uint8_t ui_addrType;
    uint8_t uiType;
    uint8_t uiDevID;
    uint8_t uiRSSI;
    uint8_t uiV;
    uint8_t uiSoc;
    uint8_t uiTxPower;
    uint16_t uiInterval;
    uint8_t uiValueX;
    uint8_t uiValueX1;
    uint8_t uiValueY;
    uint8_t uiValueY1;
    uint8_t uiSensorStatus;
    uint8_t uiPositionID;
}sensor_info_t;
#pragma pack(pop)

typedef struct tag_Current_Sensor_Info_t
{
    uint8_t uiValueX;
    uint8_t uiValueX1;
    uint8_t uiValueY;
    uint8_t uiValueY1;
}Current_Sensor_Info_t;

void cmd_Get_Sensor_Respose(uint8_t uiFromPort);
#define MAX_SENSORNUM 0xA
static sensor_info_t sensor_info[MAX_SENSORNUM] = {0};
static Current_Sensor_Info_t current_sensor_info[MAX_SENSORNUM] = {0};
static uint16_t uiCurpitch = 0, uiCurroll = 0;

static uint8_t FindSensorInfo(const bt_addr_le_t *addr)
{
    uint8_t uiRet = 0xFE;
    for(int i = 0; i < MAX_SENSORNUM; i++)
    {
        if(addr->a.val[0] == sensor_info[i].ui_addr[5] && addr->a.val[1] == sensor_info[i].ui_addr[4] && addr->a.val[2] == sensor_info[i].ui_addr[3] && \
            addr->a.val[3] == sensor_info[i].ui_addr[2] && addr->a.val[4] == sensor_info[i].ui_addr[1] && addr->a.val[5] == sensor_info[i].ui_addr[0])
        {
            uiRet = i;
            break;
        }
    }
    return uiRet;
}

static void sensor_data_parse_interval_add(uint8_t *uiData, uint16_t uiLen, uint8_t uiNum)
{
    //sensor_info[uiNum].uiDevID = uiData[4];
    if(sensor_info[uiNum].uiType == 0x1 || sensor_info[uiNum].uiType == 0x2 || sensor_info[uiNum].uiType == 0x4)//pressure
    {
        sensor_info[uiNum].uiRSSI = uiData[6];
        sensor_info[uiNum].uiV = uiData[7];
        sensor_info[uiNum].uiSoc = uiData[8];
        sensor_info[uiNum].uiValueX = uiData[9];
        sensor_info[uiNum].uiValueX1 = uiData[10];
        sensor_info[uiNum].uiTxPower = uiData[11];
        sensor_info[uiNum].uiInterval = uiData[12] + (uiData[13] << 8);
    }
    else if(sensor_info[uiNum].uiType == 0x3)
    {
        sensor_info[uiNum].uiRSSI = uiData[6];
        sensor_info[uiNum].uiV = uiData[7];
        sensor_info[uiNum].uiSoc = uiData[8];
        sensor_info[uiNum].uiValueX = uiData[9];
        sensor_info[uiNum].uiValueX1 = uiData[10];
        sensor_info[uiNum].uiValueY = uiData[11];
        sensor_info[uiNum].uiValueY1 = uiData[12];
        sensor_info[uiNum].uiTxPower = uiData[13];
        sensor_info[uiNum].uiInterval = uiData[14] + (uiData[15] << 8);
    }
}

static void sensor_data_parse_interval(uint8_t *uiData, uint16_t uiLen, const bt_addr_le_t *addr)
{
    uint8_t uiNum = FindSensorInfo(addr);

    if(uiNum == 0xFE)
    {
        printk("[%s %d] fail to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
        return;
    }

    if(sensor_info[uiNum].uiDevID == uiData[4] && sensor_info[uiNum].uiType == uiData[5])
    {
        // sensor_info[uiNum].uiType = uiData[5];
        sensor_data_parse_interval_add(uiData, uiLen, uiNum);
    }
    else
    {
        printk("[%s %d] devid or devtype is diff\n", __func__, __LINE__);
    }
}

void Sensor_dataParse(uint8_t *uiData, uint16_t uiLen, const bt_addr_le_t *addr)
{

    if(uiData[0] == 0xAA && uiData[uiLen - 1] == 0xFE)
    {
        uint8_t uiCMD = uiData[3];
        switch (uiCMD)
        {
            case 0x1:
                /* code */
                break;
            case 0x2:
                sensor_data_parse_interval(uiData, uiLen, addr);
                break;
            case 0x3:
                /* code */
                break;
            case 0x4:
                /* code */
                break;
            case 0x5:
                /* code */
                break;
            default:
                break;
        }
    }
}

void Sensor_Addr_Add(const bt_addr_le_t *addr, uint8_t uiSensorType, uint8_t uiDevID, uint8_t uiPositionID)
{
    int iNoneAddr = MAX_SENSORNUM;

    uint8_t uiNum = FindSensorInfo(addr);

    if(uiNum != 0xFE)
    {
        printk("[%s %d] success to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
        return;
    }

    for(int i = 0; i < MAX_SENSORNUM; i++)
    {
        if(sensor_info[i].ui_addr[0] == 0x0 && sensor_info[i].ui_addr[1] == 0x0 && sensor_info[i].ui_addr[2] == 0x0 &&  \
            sensor_info[i].ui_addr[3] == 0x0 && sensor_info[i].ui_addr[4] == 0x0 && sensor_info[i].ui_addr[5] == 0x0 && iNoneAddr == 0XA)
        {
            iNoneAddr = i;
            break;
        }
    }

    if(iNoneAddr == MAX_SENSORNUM)
    {
        printk("[%s %d] fail to find none addr\n", __func__, __LINE__);
        return;
    }

    for(int i = 5; i >= 0; i--)
	{
		sensor_info[iNoneAddr].ui_addr[5 - i] = addr->a.val[i];
	}
    sensor_info[iNoneAddr].ui_addrType = addr->type;
    sensor_info[iNoneAddr].uiSensorStatus = 0;
    sensor_info[iNoneAddr].uiPositionID = uiPositionID;
    sensor_info[iNoneAddr].uiDevID = uiDevID;
    sensor_info[iNoneAddr].uiType = uiSensorType;
    cmd_Get_Sensor_Respose(2);
    printk("[%s %d] success to add this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
}

void Sensor_Addr_Del(const bt_addr_le_t *addr)
{
    uint8_t uiNum = FindSensorInfo(addr);

    if(uiNum == 0xFE)
    {
        printk("[%s %d] fail to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
        return;
    }
    else
    {
        memset(sensor_info + uiNum, 0, sizeof(sensor_info_t));

        printk("[%s %d] success to del this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
    }
    cmd_Get_Sensor_Respose(2);
}

void Sensor_Addr_Set_Status(const bt_addr_le_t *addr, uint8_t uiStatus)
{
    uint8_t uiNum = FindSensorInfo(addr);

    if(uiNum == 0xFE)
    {
        printk("[%s %d] fail to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0]);
        return;
    }
    else
    {
        sensor_info[uiNum].uiSensorStatus |= uiStatus;
        printk("[%s %d] success to set this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x) status(0x%x)\n", __func__, __LINE__, addr->a.val[5], addr->a.val[4], addr->a.val[3], addr->a.val[2], \
            addr->a.val[1], addr->a.val[0], sensor_info[uiNum].uiSensorStatus);
    }
}

static uint8_t getSensorID(sensor_info_t *psensorinfo)
{
    uint8_t uiRet = 0;
    if(psensorinfo->uiType == 0x1)//pressure
    {
        uiRet = 0x10 + psensorinfo->uiDevID - 1;
    }
    else if(psensorinfo->uiType == 0x2)//travel
    {
        uiRet = 0x30 + psensorinfo->uiDevID - 1;
    }
    else if(psensorinfo->uiType == 0x3)//angle
    {
        uiRet = 0x20 + psensorinfo->uiDevID - 1;
    }
    else if(psensorinfo->uiType == 0x4)
    {
        uiRet = 0x40;
    }

    return uiRet;
}

static int iSensorTimeout = 0;
void Sensor_Info_Respone_Interval()
{
    uint32_t iCurrentTimeout = k_ticks_to_ms_near32(k_uptime_ticks());
	if((iCurrentTimeout - iSensorTimeout) < 1000)
	{
		return;
	}

	iSensorTimeout = iCurrentTimeout;
    uint8_t uiBuffer[128] = {0};
    uint8_t uiNum = 0;

    uiBuffer[0] = 0x11;
	uiBuffer[1] = 0x22;
	uiBuffer[2] = 0x1;
	uiBuffer[4] = 0xFC;

    uint8_t *puiData = uiBuffer + 6, uinterval = 9;
    uint16_t uipitch, uiroll;

    for(int i = 0; i < MAX_SENSORNUM; i++)
    {
        if(sensor_info[i].uiType != 0 && (sensor_info[i].uiValueX != current_sensor_info[i].uiValueX || \
            sensor_info[i].uiValueX1 != current_sensor_info[i].uiValueX1 || \
            sensor_info[i].uiValueY != current_sensor_info[i].uiValueY ||   \
            sensor_info[i].uiValueY1 != current_sensor_info[i].uiValueY1))
        {
            *(puiData + uiNum * uinterval) = getSensorID(sensor_info + i);
            *(puiData + uiNum * uinterval + 1) = sensor_info[i].uiValueX;
            *(puiData + uiNum * uinterval + 2) = sensor_info[i].uiValueX1;
            *(puiData + uiNum * uinterval + 3) = sensor_info[i].uiValueY;
            *(puiData + uiNum * uinterval + 4) = sensor_info[i].uiValueY1;
            *(puiData + uiNum * uinterval + 5) = sensor_info[i].uiRSSI;//RSSI
            *(puiData + uiNum * uinterval + 6) = sensor_info[i].uiTxPower;
            *(puiData + uiNum * uinterval + 7) = sensor_info[i].uiV;
            *(puiData + uiNum * uinterval + 8) = sensor_info[i].uiSoc;

            current_sensor_info[i].uiValueX = sensor_info[i].uiValueX;
            current_sensor_info[i].uiValueX1 = sensor_info[i].uiValueX1;
            current_sensor_info[i].uiValueY = sensor_info[i].uiValueY;
            current_sensor_info[i].uiValueY1 = sensor_info[i].uiValueY1;
            uiNum++;
        }
    }

    bma253_example_get_angle(&uipitch, &uiroll);
    if(abs(uiCurpitch - uipitch) > 5 || abs(uiCurroll - uiroll) > 5)
    {
        *(puiData + uiNum * uinterval) = 0x27;
        *(puiData + uiNum * uinterval + 1) = uipitch & 0xff;
        *(puiData + uiNum * uinterval + 2) = (uipitch >> 8) & 0xff;
        *(puiData + uiNum * uinterval + 3) = uiroll & 0xff;
        *(puiData + uiNum * uinterval + 4) = (uiroll >> 8) & 0xff;
        uiCurpitch = uipitch;
        uiCurroll = uiroll;
        uiNum++;
    }

    uiBuffer[3] = uiNum * uinterval + 2;
    uiBuffer[5] = uiNum;

    if(uiNum)
    {
        sendMsgdata2RS485uart(uiBuffer, uiBuffer[3] + 4);
        peripheral_send_message(uiBuffer, uiBuffer[3] + 4);
    }
}

void Sensor_Info_Respone_All(uint8_t uiFromPort)
{
    uint8_t uiBuffer[128] = {0};
    uint8_t uiNum = 0;

    uiBuffer[0] = 0x11;
	uiBuffer[1] = 0x22;
	uiBuffer[2] = 0x1;
	uiBuffer[4] = 0xFC;

    uint8_t *puiData = uiBuffer + 6, uinterval = 9;

    for(int i = 0; i < MAX_SENSORNUM; i++)
    {
        if(sensor_info[i].uiType != 0 && sensor_info[i].uiSensorStatus == 0x7)
        {
            *(puiData + uiNum * uinterval) = getSensorID(sensor_info + i);
            *(puiData + uiNum * uinterval + 1) = sensor_info[i].uiValueX;
            *(puiData + uiNum * uinterval + 2) = sensor_info[i].uiValueX1;
            *(puiData + uiNum * uinterval + 3) = sensor_info[i].uiValueY;
            *(puiData + uiNum * uinterval + 4) = sensor_info[i].uiValueY1;
            *(puiData + uiNum * uinterval + 5) = sensor_info[i].uiRSSI;//RSSI
            *(puiData + uiNum * uinterval + 6) = sensor_info[i].uiTxPower;
            *(puiData + uiNum * uinterval + 7) = sensor_info[i].uiV;
            *(puiData + uiNum * uinterval + 8) = sensor_info[i].uiSoc;
            uiNum++;
        }
    }

    {
        *(puiData + uiNum * uinterval) = 0x27;
        *(puiData + uiNum * uinterval + 1) = uiCurpitch & 0xff;
        *(puiData + uiNum * uinterval + 2) = (uiCurpitch >> 8) & 0xff;
        *(puiData + uiNum * uinterval + 3) = uiCurroll & 0xff;
        *(puiData + uiNum * uinterval + 4) = (uiCurroll >> 8) & 0xff;
        uiNum++;
    }

    uiBuffer[3] = uiNum * uinterval + 2;
    uiBuffer[5] = uiNum;

    // if(uiNum)
    {
        if(uiFromPort == 1)
        {
            peripheral_send_message(uiBuffer, uiBuffer[3] + 4);
        }
        else if(uiFromPort == 2)
        {
            sendMsgdata2RS485uart(uiBuffer, uiBuffer[3] + 4);
        }
    }
}

void cmd_Get_Sensor_Respose(uint8_t uiFromPort)
{
    uint8_t uiBuffer[128] = {0};
    uint8_t uiNum = 0;
    uint8_t *puiData = uiBuffer + 6, uinterval = 10;

    uiBuffer[0] = 0x11;
	uiBuffer[1] = 0x22;
	uiBuffer[2] = 0x1;
	uiBuffer[4] = 0xFB;

    for(int i = 0; i < MAX_SENSORNUM; i++)
    {
        if(sensor_info[i].ui_addr[0] != 0 || sensor_info[i].ui_addr[1] != 0 || sensor_info[i].ui_addr[2] != 0 ||    \
            sensor_info[i].ui_addr[3] != 0 || sensor_info[i].ui_addr[4] != 0 || sensor_info[i].ui_addr[5] != 0)
        {
            memcpy(puiData + uiNum * uinterval, sensor_info[i].ui_addr, 6 * sizeof(uint8_t));
            *(puiData + uiNum * uinterval + 6) = sensor_info[i].uiSensorStatus;

            *(puiData + uiNum * uinterval + 7) = sensor_info[i].uiDevID;
			*(puiData + uiNum * uinterval + 8) = sensor_info[i].uiPositionID;
			*(puiData + uiNum * uinterval + 9) = sensor_info[i].uiType;

            uiNum++;
        }
    }

    uiBuffer[3] = uiNum * uinterval + 2;
    uiBuffer[5] = uiNum;

    // if(uiNum)
    {
        if(uiFromPort == 1)
        {
            peripheral_send_message(uiBuffer, uiBuffer[3] + 4);
        }
        else if(uiFromPort == 2)
        {
            sendMsgdata2RS485uart(uiBuffer, uiBuffer[3] + 4);
        }
    }
}

void Sensor_DisconnectCentral(uint8_t *uiMacAddr)
{
    bt_addr_le_t target_addr = {0};
    
    for(int i = 5; i >= 0; i--)
	{
	    target_addr.a.val[i] = uiMacAddr[5 - i];
	}

    uint8_t uiNum = FindSensorInfo(&target_addr);

    if(uiNum == 0xFE)
    {
        printk("[%s %d] fail to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, uiMacAddr[0], uiMacAddr[1], uiMacAddr[2], uiMacAddr[3], uiMacAddr[4], uiMacAddr[5]);
        return;
    }
    else
    {
        printk("[%s %d] success to find this addr(0x%x:0x%x:0x%x:0x%x:0x%x:0x%x)\n", __func__, __LINE__, uiMacAddr[5], uiMacAddr[4], uiMacAddr[3], uiMacAddr[2], uiMacAddr[1], uiMacAddr[0]);
        target_addr.type = sensor_info[uiNum].ui_addrType;
        if(sensor_info[uiNum].uiSensorStatus & 0x1)
        {
            central_disconnect_by_addr(&target_addr);
        }
        else
        {
            printk("[%s %d] this mac sensor is disconnect\n", __func__, __LINE__);
        }
    }
}






