#ifndef WS2811_GPIO_H
#define WS2811_GPIO_H

#include <stdint.h>

struct LED_RGB_T {
	/** Red channel */
	uint8_t r;
	/** Green channel */
	uint8_t g;
	/** Blue channel */
	uint8_t b;
};

void ws2811_init();

// void ws2811_gpio_update_rgb(struct LED_RGB_T *pixels, uint8_t num_pixels);

void WS2811_SetAllLEDs(struct LED_RGB_T *RgbLed, uint8_t uiLight);













#endif