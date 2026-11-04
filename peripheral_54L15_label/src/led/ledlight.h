#ifndef LEDLIGHT_H
#define LEDLIGHT_H

extern void Function_of_indicator_lights();
extern void Indicator_lights_off(void);
extern uint8_t Led_ParseData(uint8_t *uiBuff, uint16_t Size, uint8_t uiFromPort);
extern void LedLightInit();

#endif