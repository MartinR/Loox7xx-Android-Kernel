/*
 * LCD driver for Fujitsu-Siemens Loox 720
 *
 * Authors: ...
 *
 * based on previous work, see below:
 *
 * Machine initialization for Dell Axim X3
 *
 * Authors: Andrew Zabolotny <zap@homelink.ru>
 *
 * For now this is mostly a placeholder file; since I don't own an Axim X3
 * it is supposed the project to be overtaken by somebody else. The code
 * in here is *supposed* to work so that you can at least boot to the command
 * line, but there is no guarantee.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/lcd.h>
#include <linux/backlight.h>
#include <linux/err.h>

#include <asm/arch/loox720-gpio.h>
#include <asm/arch/loox720-cpld.h>
#include <linux/fb.h>
#include <asm/mach-types.h>
#include "asm/arch/pxa-regs.h"
#include "asm/arch/pxafb.h"
#include <linux/platform_device.h>


static struct pxafb_mode_info loox720_lcd_mode_info = {
	.pixclock		= 80000, // Since we now use double pixel clock
	.bpp			= 16,
	.xres			= 480,
	.yres			= 640,
	.hsync_len		= 4,
	.left_margin		= 20,
	.right_margin		= 8,
	.vsync_len		= 1,
	.upper_margin		= 6,
	.lower_margin		= 9,
	.sync			= 0,
};

static struct pxafb_mach_info loox720_fb_info =
{
	.modes			= &loox720_lcd_mode_info,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP,
	//.lccr0			= LCCR0_Act | LCCR0_Sngl | LCCR0_Color ,
	//.lccr3			= LCCR3_OutEnH | LCCR3_PixRsEdg, // | LCCR3_DPC,
};

// backlight on/off now is handled by the backlight device
static struct platform_device loox720_bl_device = {
	.name		= "loox720-bl",
	.id		= -1,
};

// lcd on/off is now handled by the lcd device
static struct platform_device loox720_lcd_device = {
	.name		= "loox720-lcd",
	.id		= -1,
};

static int __init
loox720_lcd_init (void)
{
	if (! machine_is_loox720 ())
		return -ENODEV;

	set_pxa_fb_info(&loox720_fb_info);
	platform_device_register(&loox720_bl_device);
	platform_device_register(&loox720_lcd_device);
	return 0;
}

static void __exit
loox720_lcd_exit (void)
{
	platform_device_unregister(&loox720_bl_device);
	platform_device_unregister(&loox720_lcd_device);
}

module_init (loox720_lcd_init);
module_exit (loox720_lcd_exit);

MODULE_AUTHOR("...");
MODULE_DESCRIPTION("Loox 720 Core frambuffer driver");
MODULE_LICENSE("GPL");

