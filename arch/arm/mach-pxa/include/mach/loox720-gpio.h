/*
 * include/asm-arm/arch-pxa/loox720-gpio.h
 * 
 * Loox 720 GPIO list
 * Reused the hx4700-gpio.h file as a template.
 *
 * History:
 * 2008/08/17 - Tomasz Figa - Big cleanup - removed most of unneeded definitions, archaic macros and confirmed some GPIOs
 */

/*
 * !!! ATTENTION !!!
 *
 * Following GPIOs definitions are taken from another PDA header (hx4700).
 * We should check them all, to make sure that they aren't misused.
 * My suggestion is to mark all GPIOs that we are sure or unsure with some comment
 * so if you are sure of some GPIO, please add some comment after its definition!
 *
 * THANKS!
 * Tomasz Figa <tom3q@staszic.waw.pl>
 *
 * Note (18.08.2008):	tom3q:	I have marked all the GPIOs we are sure with their directions and descriptions
 *				if you find the meaning of some GPIO, please mark it following the scheme:
 				// DIR - sure - description
 				All the GPIOs marked as UNKNOWN are just unknown... (Don't look at their name)
 */

#ifndef _LOOX720_GPIO_H_
#define _LOOX720_GPIO_H_

#include <asm/arch/pxa-regs.h>

/*
 * GPIOs operating as Application-Specific GPIO
 */

#define GPIO_NR_LOOX720_KEY_ON			0 // IN - sure - the power button
#define GPIO_NR_LOOX720_GP_RST_N		1 // IN - sure - the reset button
#define GPIO_NR_LOOX720_UNK_2			2 // UNKNOWN IN
#define GPIO_NR_LOOX720_PWR_SCL			3 // UNKNOWN OUT
#define GPIO_NR_LOOX720_PWR_SDA			4 // UNKNOWN OUT
#define GPIO_NR_LOOX720_PWR_CAP0		5 // UNKNOWN IN
#define GPIO_NR_LOOX720_PWR_CAP1		6 // UNKNOWN IN
#define GPIO_NR_LOOX720_PWR_CAP2		7 // UNKNOWN IN
#define GPIO_NR_LOOX720_PWR_CAP3		8 // UNKNOWN IN
#define GPIO_NR_LOOX720_AC_IN_N			9 // IN - sure - low when AC Adapter is connected

#define GPIO_NR_LOOX720_BATTERY_FULL_N		11 // IN - sure - low when battery is full
#define GPIO_NR_LOOX720_CPLD_INT		12 // IN - sure - CPLD external interrupt pin used by IRQ demuxer
#define GPIO_NR_LOOX720_USB_DETECT_N		13 // IN - sure - detects connection to USB client controller (VBus)
#define GPIO_NR_LOOX720_WIFI_PWR		14 // OUT - sure - CF WIFI power pin - high to enable ACX100 (must be used in pair with CPLD bit)

#define GPIO_NR_LOOX720_WIFI_RST	 	19 // OUT - sure - CF WIFI reset pin - high to enable ACX100 reset state, low to disable

#define GPIO_NR_LOOX720_CPU_BT_RESET_N		22 // OUT - sure - Bluetooth chip reset pin - low to enable BRF6150 reset state, high to disable

#define GPIO_NR_LOOX720_USB_CHARGE_N		36 // OUT - sure - low -> charge from USB; high -> charge from the AC adapter

#define GPIO_NR_LOOX720_UNK_38			38 // UNKNOWN IN

#define GPIO_NR_LOOX720_MMC_DETECT_N		52 // IN - sure - low when an SD card is inserted

#define GPIO_NR_LOOX720_MMC_RO			74 // IN - sure - high when a write-protected SD card is inserted
#define GPIO_NR_LOOX720_EARPHONE_DET_N		75 // UNKNOWN OUT - driving low fades the screen to white (LCD reset?)

#define GPIO_NR_LOOX720_IR_ON_N			81 // UNKNOWN OUT

#define GPIO_NR_LOOX720_UNK_90			90 // UNKNOWN OUT
#define GPIO_NR_LOOX720_FLASH_VPEN		91 // UNKNOWN OUT

#define GPIO_NR_LOOX720_TOUCHPANEL_IRQ_N	94 // IN - sure - low when the touchscreen is being touched
#define GPIO_NR_LOOX720_BATT_OFF		95 // UNKNOWN OUT
#define GPIO_NR_LOOX720_USB_CHARGE_RATE		96 // UNKNOWN IN - seems like related to the sound system

#define GPIO_NR_LOOX720_CPLD_RESET_N		106 // OUT - sure - low to reset the CPLD
#define GPIO_NR_LOOX720_CHARGE_EN_N		107 // OUT - sure - low to enable battery charging

#define GPIO_NR_LOOX720_UNK_119			119 // UNKNOWN IN
#define GPIO_NR_LOOX720_HEADPHONE_DET		120 // IN - sure - headphone detect (high when headphones are connected)

#endif /* _LOOX720_GPIO_H */
