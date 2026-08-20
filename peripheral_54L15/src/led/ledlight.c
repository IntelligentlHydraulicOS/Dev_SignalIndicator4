#include <stdint.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "ws2811_gpio.h"
#include "ledlight.h"
#include "RS485_deal.h"
#include "peripheral_manage.h"

static const struct gpio_dt_spec heart1_led = GPIO_DT_SPEC_GET(DT_NODELABEL(heart_led), gpios);

#if 0
{0x00, 0xFF, 0x00}, /* green */
{0xFF, 0x00, 0x00}, /* red */
{0x00, 0x00, 0xFF}, /* blue */
{0xFF, 0xFF, 0x00}, /* yellow */
{0x00, 0xFF, 0xFF}, /* purple */
{0xFF, 0x00, 0xFF}, /* Cyan1 */
{0xFF, 0xFF, 0xFF}, /* white */
{0x45, 0xFF, 0x00}, /* orange */
{0x00, 0xFF, 0x40}, /* pink */
{0x00, 0x00, 0x00}, /* black */
#endif
struct LED_RGB_T rgbcolors[] = {
	[0] = {0x00, 0xFF, 0x00}, /* green */
    [1] = {0xFF, 0xFF, 0x00}, /* yellow */
	[2] = {0x00, 0x00, 0xFF}, /* blue */
	[3] = {0xFF, 0x00, 0xFF}, /* purple */
	[4] = {0xFF, 0x00, 0x00}, /* red */
	[5] = {0x00, 0xFF, 0xFF}, /* Cyan1 */
	[6] = {0xFF, 0xFF, 0xFF}, /* white */
	[7] = {0xFF, 0x00, 0x40}, /* orange */
	[8] = {0xFF, 0x45, 0x00}, /* pink */
	[9] = {0x00, 0x00, 0x00}, /* black */
};

typedef enum {
    STATE_NORMAL = 0x0,        
    STATE_CONTROL = 0x1,        
    STATE_FAULT = 0x2,        
    STATE_FOLLOW = 0x3,     
    STATE_STOP_LOCK = 0x4,
	STATE_UPGRADE = 0x5,
	STATE_MASTER = 0x6,
	STATE_CONTROLLED = 0x7,//BT debug
	STATE_REMOTECONTROL = 0x8,
	STATE_PINK = 0x9,
    STATE_CLOSE
} LEDsState_t;

LEDsState_t g_LED_pastState = STATE_CLOSE;
LEDsState_t g_LED_currentState = STATE_NORMAL;
uint8_t uiLedsLight = 1;//0:常亮  2：闪亮
uint8_t uiFlashingFlag = 0;


void LedsSetState(LEDsState_t currentstate)
{
    if(uiLedsLight == 0)
	{
		uiFlashingFlag = 0;
	}
	else
	{
		uiFlashingFlag = (uiFlashingFlag + 1) % 2;
	}

    switch (currentstate)
	{
		case STATE_NORMAL:
            WS2811_SetAllLEDs(rgbcolors + STATE_NORMAL, uiFlashingFlag);
        break;
		case STATE_CONTROL:
			WS2811_SetAllLEDs(rgbcolors + STATE_CONTROL, uiFlashingFlag);
		break;
		case STATE_STOP_LOCK:
			WS2811_SetAllLEDs(rgbcolors + STATE_STOP_LOCK, uiFlashingFlag);
		break;
		case STATE_FAULT:
			WS2811_SetAllLEDs(rgbcolors + STATE_FAULT, uiFlashingFlag);
		break;
		case STATE_FOLLOW:
			WS2811_SetAllLEDs(rgbcolors + STATE_FOLLOW, uiFlashingFlag);
		break;
		case STATE_UPGRADE:
			WS2811_SetAllLEDs(rgbcolors + STATE_UPGRADE, uiFlashingFlag);
		break;
		case STATE_MASTER:
			WS2811_SetAllLEDs(rgbcolors + STATE_MASTER, uiFlashingFlag);
		break;
		case STATE_CONTROLLED:
			WS2811_SetAllLEDs(rgbcolors + STATE_CONTROLLED, uiFlashingFlag);
		break;
		case STATE_REMOTECONTROL:
			WS2811_SetAllLEDs(rgbcolors + STATE_REMOTECONTROL, uiFlashingFlag);
		break;
		case STATE_CLOSE:
			WS2811_SetAllLEDs(rgbcolors + STATE_CLOSE, uiFlashingFlag);
		break;
		default:
			WS2811_SetAllLEDs(rgbcolors + STATE_CLOSE, uiFlashingFlag);
        break;
	}
}

static uint8_t uiheartLight = 0;
static void heartledlight()
{
	int err = gpio_pin_set_dt(&heart1_led, uiheartLight);
	if (err) {
		printk("[%s %d] Cannot write LED gpio", __func__, __LINE__);
	}

	uiheartLight = (uiheartLight + 1) % 2;
}

void LedLightInit()
{
	int err = gpio_pin_configure_dt(&heart1_led, GPIO_OUTPUT);
    if (err) {
        printk("[%s %d]Cannot configure LED gpio", __func__, __LINE__);
    }

	ws2811_init();
}
static int iLedTimeout = 0;
void Function_of_indicator_lights(void)
{
	uint32_t iCurrentTimeout = k_ticks_to_ms_near32(k_uptime_ticks());
	if((iCurrentTimeout - iLedTimeout) < 1000)
	{
		return;
	}

	iLedTimeout = iCurrentTimeout;

    if(g_LED_pastState != g_LED_currentState)
	{
		g_LED_pastState = g_LED_currentState;
	}

	heartledlight();
    LedsSetState(g_LED_pastState);
}

/*
	uiFromPort:2-485 1-BT
*/
uint8_t Led_ParseData(uint8_t *uiBuff, uint16_t Size, uint8_t uiFromPort)
{
	uint8_t uiRet = 1;
	uint8_t uiSendBuff[8] = {0};
	if(Size == 5 && uiBuff[0] == 0x0 && uiBuff[1] == 0x33 && uiBuff[2] == 0x02)
	{
		uiLedsLight = uiBuff[3];

		if(uiBuff[4] == 0x1)
		{
			g_LED_currentState = STATE_STOP_LOCK;
		}
		else if(uiBuff[4] == 0x2)
		{
			g_LED_currentState = STATE_CONTROL;
		}
		else if(uiBuff[4] == 0x3)
		{
			g_LED_currentState = STATE_FAULT;
		}
		else if(uiBuff[4] == 0x4)
		{
			g_LED_currentState = STATE_NORMAL;
		}
		else if(uiBuff[4] == 0x5)
		{
			g_LED_currentState = STATE_FOLLOW;
		}
		else if(uiBuff[4] == 0x6)
		{
			g_LED_currentState = STATE_UPGRADE;
		}
		else if(uiBuff[4] == 0x7)
		{
			g_LED_currentState = STATE_MASTER;
		}
		else if(uiBuff[4] == 0x8)
		{
			g_LED_currentState = STATE_CONTROLLED;
		}
		else if(uiBuff[4] == 0x9)
		{
			g_LED_currentState = STATE_REMOTECONTROL;
		}
		else if(uiBuff[4] == 0xa)
		{
			g_LED_currentState = STATE_CLOSE;
		}
		else
		{
			uiRet = 0;
		}

        printk("[%s %d] current state %d", __func__, __LINE__, g_LED_currentState);
	}
	else
	{
		uiRet = 0;
	}
	
	if(uiRet)
	{
		memcpy(uiSendBuff, uiBuff, Size);
		uiSendBuff[2] = 0x1;

		if(uiFromPort == 2)
		{
			sendMsgdata2RS485uart(uiSendBuff, Size);
		}
		else if(uiFromPort == 1)
		{
			peripheral_send_message(uiSendBuff, Size);
		}
	}

	return uiRet;
}