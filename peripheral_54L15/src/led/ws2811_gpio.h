#ifndef WS2811_GPIO_H
#define WS2811_GPIO_H


#define LED_COLOR_ID_RED        0
#define LED_COLOR_ID_GREEN      1
#define LED_COLOR_ID_BLUE       2
#define LED_COLOR_ID_YELLOW     3
#define LED_COLOR_ID_PURPLE     4
#define LED_COLOR_ID_CYANl      5
#define LED_COLOR_ID_WHITE      6
#define LED_COLOR_ID_ORANGE     7
#define LED_COLOR_ID_PINK       8
#define LED_COLOR_ID_BLACK      9
#define LED_COLOR_ID_MAX        10

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

void led_updatergb();













#endif