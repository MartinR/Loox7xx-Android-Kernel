/*
 * arch/arm/mach-pxa/loox720/loox720_bt.c
 * Copyright (c) Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	    Loox 720 Bluetooth driver based on
 *			S3C2410 bluetooth "driver" by Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/loox720.h>
#include <mach/loox720-gpio.h>
#include <mach/loox720-cpld.h>
#include <mach/regs-uart.h>

#define DRV_NAME              "loox720-bt"

enum loox_bt_state {
BT_OFF,
BT_ON,
BT_PRE_SUSPEND_ON,
};

static enum loox_bt_state state;

/* Bluetooth control */
static void loox720_bt_enable(int on)
{
	int tries;
	if (on) {
		/* pre-serial-up hardware configuration */
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_POWER, 1 );
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_RADIO, 1 );
		loox720_enable_led( LOOX720_LED_LEFT, LOOX720_LED_COLOR_A | LOOX720_LED_BLINK );
		gpio_set_value( GPIO_NR_LOOX720_CPU_BT_RESET_N, 0 );
		mdelay(1);
		gpio_set_value( GPIO_NR_LOOX720_CPU_BT_RESET_N, 1 );

		/*
		 * BRF6150's RTS goes low when firmware is ready
		 * so check for CTS=1 (nCTS=0 -> CTS=1). Typical 150ms
		 */
		printk( KERN_NOTICE "loox720_bt.c: Waiting for firmware...\n");
		tries = 0;
		
		do {
			mdelay(10);
		} while ((BTMSR & MSR_CTS) == 0 && tries++ < 50);
		
		if(tries>49)
		{
			printk( KERN_NOTICE "loox720_bt.c: Firmware timeout!\n");
		}

		state = BT_ON;
	}
	else {
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_RADIO, 0 );
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_POWER, 0 );
		loox720_disable_led( LOOX720_LED_LEFT, LOOX720_LED_COLOR_A );
		mdelay(1);
		if (state != BT_PRE_SUSPEND_ON)
		  state = BT_OFF;
	}
}

static ssize_t loox720_bt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", !(state == BT_OFF));
}

static ssize_t loox720_bt_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int new_state;
	char *endp;

	new_state = simple_strtoul(buf, &endp, 0);
	if (*endp && !isspace(*endp))
		return -EINVAL;

	loox720_bt_enable(new_state);

	return count;
}
static DEVICE_ATTR(enable, 0644,
		loox720_bt_show,
		loox720_bt_store);

static int __init loox720_bt_probe(struct platform_device *pdev)
{
	if(gpio_request(GPIO_NR_LOOX720_CPU_BT_RESET_N, "BT_RESET_N") != 0) {
		printk(KERN_ERR "Failed to request BT_RESET_N GPIO\n");
		return -ENODEV;
	}
	/* disable BT by default */
	loox720_bt_enable(0);

	return device_create_file(&pdev->dev, &dev_attr_enable);
}

static int loox720_bt_remove(struct platform_device *pdev)
{
	gpio_free(GPIO_NR_LOOX720_CPU_BT_RESET_N);
	return 0;
}

#ifdef CONFIG_PM
static int loox720_bt_suspend(struct platform_device *pdev, pm_message_t mstate)
{
	if (state == BT_ON) {
	  state = BT_PRE_SUSPEND_ON;
	  loox720_bt_enable(0);
	}
	return 0;
}

static int loox720_bt_resume(struct platform_device *pdev)
{
	if (state == BT_PRE_SUSPEND_ON)
	  loox720_bt_enable(1);
	return 0;
}
#else
#define loox720_bt_suspend	NULL
#define loox720_bt_resume	NULL
#endif


static struct platform_driver loox720_bt_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},
	.probe		= loox720_bt_probe,
	.remove		= loox720_bt_remove,
	.suspend	= loox720_bt_suspend,
	.resume		= loox720_bt_resume,
};


static int __init loox720_bt_init(void)
{
	return platform_driver_register(&loox720_bt_driver);
}

static void __exit loox720_bt_exit(void)
{
	platform_driver_unregister(&loox720_bt_driver);
}

module_init(loox720_bt_init);
module_exit(loox720_bt_exit);

MODULE_AUTHOR("Tomasz Figa <tomasz.figa@gmail.com>");
MODULE_DESCRIPTION("Driver for the PocketLOOX 720 bluetooth chip");
MODULE_LICENSE("GPL");

#if 0

/* Bluetooth interface driver for TI BRF6150 on Loox 720
 * (Based on TI BRF6150 driver for HX4700)
 *
 * Copyright (c) 2005 SDG Systems, LLC
 *
 * 2005-04-21   Todd Blumer             Created.
 * 2007-07-25	Tomasz Figa		Modified for Loox 720.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/leds.h>

#include <asm/hardware.h>
#include <asm/arch/serial.h>
#include <asm/arch/loox720.h>
#include <asm/arch/loox720-gpio.h>
#include <asm/arch/loox720-cpld.h>

static void
loox720_bt_configure( int state )
{
	int tries;

	printk( KERN_NOTICE "loox720 configure bluetooth: %d\n", state );
	switch (state) {
	
	case PXA_UART_CFG_PRE_STARTUP:
		break;

	case PXA_UART_CFG_POST_STARTUP:
		/* pre-serial-up hardware configuration */
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_POWER, 1 );
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_RADIO, 1 );
		loox720_enable_led( LOOX720_LED_LEFT, LOOX720_LED_COLOR_A | LOOX720_LED_BLINK );
		SET_LOOX720_GPIO( CPU_BT_RESET_N, 0 );
		mdelay(1);
		SET_LOOX720_GPIO( CPU_BT_RESET_N, 1 );

		/*
		 * BRF6150's RTS goes low when firmware is ready
		 * so check for CTS=1 (nCTS=0 -> CTS=1). Typical 150ms
		 */
		printk( KERN_NOTICE "loox720_bt.c: Waiting for firmware...\n");
		tries = 0;
		do {
			mdelay(10);
		} while ((BTMSR & MSR_CTS) == 0 && tries++ < 50);
		if(tries>49)
		{
			printk( KERN_NOTICE "loox720_bt.c: Firmware timeout!\n");
		}
		break;

	case PXA_UART_CFG_PRE_SHUTDOWN:

		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_RADIO, 0 );
		loox720_egpio_set_bit( LOOX720_CPLD_BLUETOOTH_POWER, 0 );
		loox720_disable_led( LOOX720_LED_LEFT, LOOX720_LED_COLOR_A );
		mdelay(1);
		break;

	default:
		break;
	}
}


static int
loox720_bt_probe( struct platform_device *pdev )
{
	struct loox720_bt_funcs *funcs = pdev->dev.platform_data;

	printk( KERN_NOTICE "loox720_bt.c: Probing device...\n");

/* Already configured by mfp_pxa2xx */
#if 0
	/* configure bluetooth UART */
	pxa_gpio_mode( GPIO_NR_LOOX720_BT_RXD_MD );
	pxa_gpio_mode( GPIO_NR_LOOX720_BT_TXD_MD );
	pxa_gpio_mode( GPIO_NR_LOOX720_BT_UART_CTS_MD );
	pxa_gpio_mode( GPIO_NR_LOOX720_BT_UART_RTS_MD );
#endif

	funcs->configure = loox720_bt_configure;

	return 0;
}

static int
loox720_bt_remove( struct platform_device *pdev )
{
	struct loox720_bt_funcs *funcs = pdev->dev.platform_data;

	funcs->configure = NULL;

	return 0;
}

static struct platform_driver bt_driver = {
	.driver = {
		.name     = "loox720-bt",
	},
	.probe    = loox720_bt_probe,
	.remove   = loox720_bt_remove,
};

static int __init
loox720_bt_init( void )
{
	printk(KERN_NOTICE "loox720 Bluetooth Driver\n");
	return platform_driver_register( &bt_driver );
}

static void __exit
loox720_bt_exit( void )
{
	platform_driver_unregister( &bt_driver );
}

module_init( loox720_bt_init );
module_exit( loox720_bt_exit );

MODULE_AUTHOR("Todd Blumer, SDG Systems, LLC");
MODULE_DESCRIPTION("loox720 Bluetooth Support Driver");
MODULE_LICENSE("GPL");

/* vim600: set noexpandtab sw=8 ts=8 :*/

#endif
