#ifndef LEDCOLORRGB_H
#define LEDCOLORRGB_H
#include <stdint.h>
struct LED_RGB_T {
	/** Red channel */
	uint8_t r;
	/** Green channel */
	uint8_t g;
	/** Blue channel */
	uint8_t b;
};

#endif