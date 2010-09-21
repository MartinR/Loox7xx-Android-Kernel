/*
 * include/asm/arm/arch-pxa/loox720-cpld.h
 *
 * Authors: 	Piotr Czechowicz 	<p.czechowicz@admitech.pl>
 *		Tomasz Figa		<tomasz.figa@gmail.com>
 *		Jan Rinze Peterzon	<janrinze@gmail.com>
 *
 * Loox 720 CPLD structure:
 * 16 registers (16-bit) located at physical address (0x10000000):
 * reg 1 	- interrupt status
 * reg 2 	- interrupt mask
 * reg 3 	- input pins
 * regs 4-7 	- unknown
 * regs 8-16 	- output pins
 *
 */

#ifndef _LOOX720_CPLD_H_
#define _LOOX720_CPLD_H_

#include <mach/pxa2xx-regs.h>

#define LOOX720_CPLD_PHYS	PXA_CS4_PHYS
#define LOOX720_CPLD_SIZE	32

/*
    adapted these methods for 16 bit..
    Hopefully noone else uses these..
    a carefull grep showed noone yet does..
    the names are too generic..
*/

#define BIT_POS(x)	(x>>4)
#define BIT_SHIFT(x)	(x&15)
#define BIT_MSK(x)	(u32)(1<<BIT_SHIFT(x))

/*
Bitmasks of LED modes in LEDs' cache.
*/

#define LOOX720_LED1_BLUE		1
#define LOOX720_LED1_GREEN		2
#define LOOX720_LED1_BLINK		4
#define LOOX720_LED1_BLINK2		8

#define LOOX720_LED2_GREEN		16
#define LOOX720_LED2_RED		32
#define LOOX720_LED2_ORANGE		48
#define LOOX720_LED2_BLINK	 	64
#define LOOX720_LED2_BLINK2	  	128

/*
Bitmasks of bits in reg #8:
*/

#define LOOX720_CPLD_LED1_BIT_A 256
#define LOOX720_CPLD_LED1_BIT_B 512
#define LOOX720_CPLD_LED1_BIT_C 1024
#define LOOX720_CPLD_LED1_BIT_D 2048
#define LOOX720_CPLD_LED1_BLINK 32768

#define LOOX720_CPLD_LED2_BIT_A 16
#define LOOX720_CPLD_LED2_BIT_B 32
#define LOOX720_CPLD_LED2_BIT_C 64
#define LOOX720_CPLD_LED2_BIT_D 128
#define LOOX720_CPLD_LED2_BLINK 16384

/*
CPLD output bits numbers
*/

#define LOOX720_CPLD_LED_BIT_1			132
#define LOOX720_CPLD_LED_BIT_2			133
#define LOOX720_CPLD_LED_BIT_3			134
#define LOOX720_CPLD_LED_BIT_4			135
#define LOOX720_CPLD_LED_BIT_5			136
#define LOOX720_CPLD_LED_BIT_6			137
#define LOOX720_CPLD_LED_BIT_7			138
#define LOOX720_CPLD_LED_BIT_8			139
#define LOOX720_CPLD_CF_3V3_BIT			140
#define LOOX720_CPLD_LED_BIT_9			142
#define LOOX720_CPLD_LED_BIT_10			143
#define LOOX720_CPLD_BATTERY_BIT		144
#define LOOX720_CPLD_USB_PULLUP_BIT		145
#define LOOX720_CPLD_LCD_CONSOLE_BIT		146 // Blanking: 4rd Unblanking: 2nd
#define LOOX720_CPLD_LCD_BIT			147 // Blanking: 3rd Unblanking: 3rd
/*

These looks like a piece of garbage, because wince doesn't use them...

#define LOOX720_CPLD_LED2_EN_1			148
#define LOOX720_CPLD_LED2_EN_2			149
#define LOOX720_CPLD_LED2_EN_3			150
#define LOOX720_CPLD_LED1_EN_1			152
#define LOOX720_CPLD_LED1_EN_2			153
*/
#define LOOX720_CPLD_LCD_COLOR_BIT		160 // Blanking: 2nd Unblanking: 4th
#define LOOX720_CPLD_LCD_BIT2			161 // Blanking: 5th Unblanking: 1st
#define LOOX720_CPLD_CF_RESET			162
#define LOOX720_CPLD_SERIAL_BIT			163
#define LOOX720_CPLD_VAUX_CTL1			176
#define LOOX720_CPLD_VAUX_CTL2			177
#define LOOX720_CPLD_VAUX_CTL3			178	
#define LOOX720_CPLD_VAUX_CTL4			179
#define LOOX720_CPLD_BLUETOOTH_POWER		192
#define LOOX720_CPLD_CF_RESET_N			193 // False...
#define LOOX720_CPLD_CAMERA_FLASH_BIT		195
#define LOOX720_CPLD_SOUND_BIT			208
#define LOOX720_CPLD_SND_AMPLIFIER_BIT		209
#define LOOX720_CPLD_BLUETOOTH_RADIO    	210
#define LOOX720_CPLD_WIFI_POWER			211
#define LOOX720_CPLD_CAMERA_POWER		224
#define LOOX720_CPLD_SD_BIT			225
#define LOOX720_CPLD_CAMERA_RESET		226
#define LOOX720_CPLD_CF_5V_BIT			241
#define LOOX720_CPLD_BACKLIGHT_BIT		242 // Blanking: 1st Unblanking: 5th

/*
CPLD input bitmasks definitions

These should be bit positions!!

*/

#define LOOX720_CPLD_CF_DETECT_N		0x04
#define LOOX720_CPLD_CF_READY			0x10
#define LOOX720_CPLD_WIFI_READY			0x20
#define LOOX720_CPLD_WIFI_ENABLED		0x40

extern int loox720_cpld_irq_base;

/*
CPLD interrupt multiplexer definitions
*/

#define LOOX720_IRQ_BASE IRQ_BOARD_START

#define LOOX720_CPLD_IRQ(irq) \
    (LOOX720_CPLD_IRQ_ ## irq) + LOOX720_IRQ_BASE

#define LOOX720_CPLD_IRQ_COUNT 16

#define LOOX720_CPLD_IRQ_WIFI 		5
#define LOOX720_CPLD_IRQ_CF 		4
#define LOOX720_CPLD_IRQ_CARD_DETECT 	2

/*
Function definitions
*/

extern void loox720_set_leds(int mode);
extern void loox720_egpio_set_bit(int bit, int val);
extern u32  loox720_cpld_reg_read(int regno);
extern void loox720_cpld_reg_write(int regno, u32 value);
extern void loox720_cpld_write_masked(int regno,u32 mask ,u32 value);
extern u32 loox720_cpld_reg_test(int regno,u32 mask);
extern void loox720_cpld_resume(void);

#endif
