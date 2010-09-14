/*
 * include/asm/arm/arch-pxa/loox720.h
 */

#ifndef _LOOX720_H_
#define _LOOX720_H_

/* Loox 720 general header
 */

struct loox720_bt_funcs {
        void (*configure) ( int state );
};

#endif /* _LOOX720_H_ */

/*
 * Loox 720 Leds
 */

#define LOOX720_LED_LEFT 0
#define LOOX720_LED_RIGHT 1
#define LOOX720_LED_COLOR_A 1
#define LOOX720_LED_COLOR_B 2
#define LOOX720_LED_COLOR_C 3
#define LOOX720_LED_BLINK 4
#define LOOX720_LED_BLINK_ALT 12

extern void loox720_enable_led(int led, int mode);
extern void loox720_disable_led(int led, int mode);
