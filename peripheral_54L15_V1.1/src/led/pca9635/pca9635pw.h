#ifndef PCA9635PW_H
#define PCA9635PW_H

#include "ledcolorrgb.h"

void pca9635_init();

void pca9635_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight);

#endif