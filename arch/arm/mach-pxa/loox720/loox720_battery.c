/* Battery Charging support for FSC Loox 720
 *
 * Authors: Piotr Czechowicz, Tomasz Figa
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/gpio.h>

#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>

#include <mach/loox720-gpio.h>
#include <mach/loox720-cpld.h>
#include <mach/loox720.h>
#include <mach/irqs.h>

static unsigned int ac_irq = 0xffffffff;
static unsigned int battery_irq = 0xffffffff;
static unsigned int usb_irq = 0xffffffff;

static void update_battery_charging(void)
{
	int usb_conn = (gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N) == 0);
	int ac_conn = (gpio_get_value(GPIO_NR_LOOX720_AC_IN_N) == 0);

	if (ac_conn || usb_conn) {
		int battery_full = gpio_get_value(GPIO_NR_LOOX720_BATTERY_FULL_N) == 0;
		printk(KERN_INFO "battery: ac: %d; usb: %d; battery_full: %d\n", ac_conn, usb_conn, battery_full);
		if (ac_conn)
			gpio_set_value(GPIO_NR_LOOX720_USB_CHARGE_N, 1);
		else
			gpio_set_value(GPIO_NR_LOOX720_USB_CHARGE_N, 0);
		if (!battery_full) {
			gpio_set_value(GPIO_NR_LOOX720_CHARGE_EN_N, 0);
			loox720_enable_led(LOOX720_LED_RIGHT, LOOX720_LED_COLOR_C | LOOX720_LED_BLINK);
		} else {
			gpio_set_value(GPIO_NR_LOOX720_CHARGE_EN_N, 1);
			loox720_enable_led(LOOX720_LED_RIGHT, LOOX720_LED_COLOR_C);
		}
	} else {
		printk(KERN_INFO "battery: external power is disconnected.\n");
		gpio_set_value(GPIO_NR_LOOX720_CHARGE_EN_N, 1);
		loox720_disable_led(LOOX720_LED_RIGHT, LOOX720_LED_COLOR_C);
	}
}

static void update_battery_chip(void)
{
	int connected = (gpio_get_value(GPIO_NR_LOOX720_AC_IN_N) == 0) || (gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N) == 0);
	printk( KERN_INFO "battery: charging controller enabled=%d\n", connected );
	loox720_egpio_set_bit(LOOX720_CPLD_BATTERY_BIT, connected);
}

static int
battery_isr(int irq, void *data)
{
	int full = gpio_get_value(GPIO_NR_LOOX720_BATTERY_FULL_N) == 0;
	printk( KERN_INFO "battery_isr: battery_full=%d\n", full);

	update_battery_charging();

	return IRQ_HANDLED;
}

static int
ac_isr(int irq, void *data)
{
	int connected;

	if (irq != ac_irq)
		return IRQ_NONE;

	connected = gpio_get_value(GPIO_NR_LOOX720_AC_IN_N) == 0;
	printk( KERN_INFO "ac_isr: connected=%d\n", connected );

	update_battery_charging();
	update_battery_chip();

	return IRQ_HANDLED;
}

static int
usb_isr(int irq, void *data)
{
	int connected;

	if (irq != usb_irq)
		return IRQ_NONE;

	connected = gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N) == 0;
	printk( KERN_INFO "usb_isr: connected=%d\n", connected );

	update_battery_charging();
	update_battery_chip();

	return IRQ_HANDLED;
}

static int
loox720_battery_probe( struct platform_device *pdev )
{
	int battery_full, usb_connected, ac_connected;

	printk( KERN_NOTICE "Loox 720 Core Hardware Driver\n" );

	ac_irq = IRQ_GPIO(GPIO_NR_LOOX720_AC_IN_N);
	battery_irq = IRQ_GPIO(GPIO_NR_LOOX720_BATTERY_FULL_N);
	usb_irq = IRQ_GPIO(GPIO_NR_LOOX720_USB_DETECT_N);

	ac_connected = gpio_get_value(GPIO_NR_LOOX720_AC_IN_N) == 0;
	usb_connected = gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N) == 0;
	battery_full = gpio_get_value(GPIO_NR_LOOX720_BATTERY_FULL_N) == 0;

	printk( KERN_INFO "AC: connected=%d\n", ac_connected );
	if (request_irq( ac_irq, ac_isr, IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	                 "Loox 720 AC Detect", NULL ) != 0) {
		printk( KERN_ERR "Unable to configure AC detect interrupt.\n" );
		return -ENODEV;
	}

	printk( KERN_INFO "USB: connected=%d\n", usb_connected );
	if (request_irq( usb_irq, usb_isr, IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "Loox 720 USB Detect", NULL) != 0) {
		printk( KERN_ERR "Unable to configure USB detect interrupt.\n" );
		free_irq( ac_irq, NULL );
		return -ENODEV;
	}

	if (request_irq( battery_irq, battery_isr, IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "Loox 720 Battery Full", NULL) != 0) {
		printk( KERN_ERR "Unable to configure battery-full detect interrupt.\n" );
		free_irq( ac_irq, NULL );
		free_irq( usb_irq, NULL );
		return -ENODEV;
	}

	update_battery_chip();
	update_battery_charging();

	return 0;
}

static int
loox720_battery_remove( struct platform_device *dev )
{
	if (ac_irq != 0xffffffff)
		free_irq( ac_irq, NULL );
	if (battery_irq != 0xffffffff)
		free_irq( battery_irq, NULL );
	if (usb_irq != 0xffffffff)
		free_irq( usb_irq, NULL );
	return 0;
}

struct platform_driver loox720_battery_driver = {
	.driver = {
		.name     = "loox720-battery",
	},
	.probe    = loox720_battery_probe,
	.remove   = loox720_battery_remove,
};

static int __init
loox720_battery_init( void )
{
	return platform_driver_register( &loox720_battery_driver );
}


static void __exit
loox720_battery_exit( void )
{
	platform_driver_unregister( &loox720_battery_driver );
}

module_init( loox720_battery_init );
module_exit( loox720_battery_exit );

MODULE_AUTHOR("Piotr Czechowicz, Tomasz Figa");
MODULE_DESCRIPTION("Loox 720 Battery Charging Support");
MODULE_LICENSE("GPL");

