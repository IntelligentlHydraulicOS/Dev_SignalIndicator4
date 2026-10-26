#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include "ws2811_gpio.h"

#include <zephyr/drivers/pwm.h>
#include <zephyr/dt-bindings/pwm/pwm.h>
#include <nrfx_pwm.h>

LOG_MODULE_REGISTER(ws2811_gpio);

#define PWM_FALLING(val)  ((val) | 0x8000U)

// 硬件时序参数 WS2811 800kHz
#define LED_MAX_COUNT      2
#define PWM_PERIOD_NS      1250
#define T1H           PWM_FALLING(10)
#define T0H           PWM_FALLING(4)
#define TOTAL_BITS          (LED_MAX_COUNT * 24)
#define PWM_INST_IDX   20
/* P1.11: port=1, pin=11 */
#define PWM_OUT_PIN    (((1) << 5) | (11))

static nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(NRF_PWM_INST_GET(PWM_INST_IDX));
NRFX_INSTANCE_IRQ_HANDLER_DEFINE(pwm, PWM_INST_IDX, &pwm_instance);

static nrf_pwm_values_common_t pwm_seq_s[48] = {0};
static void pwm_handler(nrfx_pwm_event_type_t event_type, void *p_context)
{
    if (event_type == NRFX_PWM_EVENT_FINISHED) {
        // printk("PWM sequence finished\n");
    }
}

void ws2811_init()
{
	IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_PWM_INST_GET(PWM_INST_IDX)),
                IRQ_PRIO_LOWEST,
                nrfx_pwm_irq_handler, &pwm_instance, 0);
	
	nrfx_pwm_config_t config = {
        .output_pins = {
            PWM_OUT_PIN,
            NRF_PWM_PIN_NOT_CONNECTED,
            NRF_PWM_PIN_NOT_CONNECTED,
            NRF_PWM_PIN_NOT_CONNECTED,
        },
        .irq_priority = NRFX_PWM_DEFAULT_CONFIG_IRQ_PRIORITY,
        .base_clock   = NRF_PWM_CLK_16MHz,  /* 62.5ns/step */
        .count_mode   = NRF_PWM_MODE_UP,
        .top_value    = 20,           /* 周期 = 1250ns */
        .load_mode    = NRF_PWM_LOAD_COMMON,
        .step_mode    = NRF_PWM_STEP_AUTO,
    };

	int err = nrfx_pwm_init(&pwm_instance, &config, pwm_handler, NULL);
    if (err != 0) {
        printk("[%s %d] nrfx_pwm_init failed: %d\n", __func__, __LINE__, err);
        return;
    }
}

void WS2811_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight)
{
    int iNumber = 0;
    uint8_t uiTmpr = 0, uiTmpg = 0, uiTmpb = 0;
    uiTmpr = RgbLed->r;
    uiTmpg = RgbLed->g;
    uiTmpb = RgbLed->b;

    if(uiLight)
    {
        for(int i = 0; i < 48; i++)
        {
            pwm_seq_s[i] = T0H;
        }
        // memset(pwm_seq_s, T0H, 48 * sizeof(uint8_t));
    }
    else
    {
        for(int i = 7; i >= 0; i--)
        {
            if(uiTmpr & BIT(i))
            {
                pwm_seq_s[iNumber] = T1H;
                pwm_seq_s[iNumber + 24] = T1H;
            }
            else
            {
                pwm_seq_s[iNumber] = T0H;
                pwm_seq_s[iNumber + 24] = T0H;
            }

            if(uiTmpg & BIT(i))
            {
                pwm_seq_s[iNumber + 8] = T1H;
                pwm_seq_s[iNumber + 32] = T1H;
            }
            else
            {
                pwm_seq_s[iNumber + 8] = T0H;
                pwm_seq_s[iNumber + 32] = T0H;
            }

            if(uiTmpb & BIT(i))
            {
                pwm_seq_s[iNumber + 16] = T1H;
                pwm_seq_s[iNumber + 40] = T1H;
            }
            else
            {
                pwm_seq_s[iNumber + 16] = T0H;
                pwm_seq_s[iNumber + 40] = T0H;
            }
            iNumber++;
        }
    }
    

	nrf_pwm_sequence_t seq = {
        .values.p_common = pwm_seq_s/*pwm_seq[uiNum]*/,       /* RAM buffer */
        .length          = TOTAL_BITS,
        .repeats         = 0,             /* New value every PWM period */
        .end_delay       = 0
    };

    /* NRFX_PWM_FLAG_LOOP: DMA continuously replays the sequence */
    nrfx_pwm_simple_playback(&pwm_instance, &seq, 1, NRFX_PWM_FLAG_STOP);
}


