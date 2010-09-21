/*
 * backlight driver for Fujitsu-Siemens Loox 720
 * based on the corgi_bl.c backlight driver by Richard Purdie
 *
 * Authors: Harald Radke, Tomasz Figa
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/backlight.h>
#include <linux/err.h>

#include <mach/loox720-gpio.h>
#include <mach/loox720-cpld.h>
#include <linux/fb.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>



#define LOOX720_MAX_INTENSITY 0xc8
#define LOOX720_DEFAULT_INTENSITY (LOOX720_MAX_INTENSITY / 4)

static uint reg_pwduty0,reg_perval0;

/*--------------------------------------------------------------------------------*/

static void loox720_set_bl_intensity(int intensity)
{
	if (intensity < 7) intensity = 0;

	PWM_CTRL0 = 1;
	PWM_PWDUTY0 = intensity;
	PWM_PERVAL0 = LOOX720_MAX_INTENSITY;

	if (intensity > 0) {
		loox720_egpio_set_bit(LOOX720_CPLD_BACKLIGHT_BIT, 1);
		PWM_CTRL0 = 1;
	} else {
		loox720_egpio_set_bit(LOOX720_CPLD_BACKLIGHT_BIT, 0);
		PWM_CTRL0 = 0;
	}
}

/*---------------------------------------------------------------------------------*/

static int loox720_bl_update_status(struct backlight_device *bd) 
{
  int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;
	loox720_set_bl_intensity(intensity);
	return 0;
}

/*--------------------------------------------------------------------------------*/

static	int loox720_bl_get_brightness(struct backlight_device *bd){
  return PWM_PWDUTY0 & 0xff;
}

/*--------------------------------------------------------------------------------*/

static struct backlight_ops loox720_bl_ops = {
	.update_status = loox720_bl_update_status,
	.get_brightness = loox720_bl_get_brightness,
};

#ifdef CONFIG_PM
/*---------------------------------------------------------------------------------*/
static int loox720_bl_suspend (struct platform_device *pdev, pm_message_t message)
{
	reg_pwduty0 = PWM_PWDUTY0;
	reg_perval0 = PWM_PERVAL0;
	PWM_CTRL0 = 0;
	loox720_egpio_set_bit(LOOX720_CPLD_BACKLIGHT_BIT, 0);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static int loox720_bl_resume (struct platform_device *pdev)
{
	PWM_CTRL0 = 1;
	PWM_PWDUTY0 = reg_pwduty0;
	PWM_PERVAL0 = reg_perval0;
	loox720_egpio_set_bit(LOOX720_CPLD_BACKLIGHT_BIT, 1);
	return 0;
}

/*---------------------------------------------------------------------------------*/
#else
#define loox720_bl_suspend NULL
#define loox720_bl_resume NULL
#endif

static int loox720_bl_probe (struct platform_device *pdev)
{
	struct backlight_device *bl_device;
	
	if (! machine_is_loox720 ())
		return -ENODEV;
	bl_device = backlight_device_register ("loox720-bl", &pdev->dev, NULL, &loox720_bl_ops);
	if (IS_ERR (bl_device))
		return -1;
	platform_set_drvdata(pdev, bl_device);
	bl_device->props.max_brightness = LOOX720_MAX_INTENSITY;
	bl_device->props.power = FB_BLANK_UNBLANK;
	bl_device->props.brightness = LOOX720_DEFAULT_INTENSITY;
	backlight_update_status(bl_device);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static int loox720_bl_remove (struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);
	backlight_update_status(bd);
	backlight_device_unregister(bd);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static struct platform_driver loox720_bl_driver = {
	.probe		= loox720_bl_probe,
	.remove		= loox720_bl_remove,
	.suspend	= loox720_bl_suspend,
	.resume		= loox720_bl_resume,
	.driver		= {
		.name	= "loox720-bl",
	},
};

static int __init loox720_bl_init(void)
{
	return platform_driver_register(&loox720_bl_driver);
}


static void __exit loox720_bl_exit (void)
{
	platform_driver_unregister(&loox720_bl_driver);
}

module_init (loox720_bl_init);
module_exit (loox720_bl_exit);

MODULE_AUTHOR("Harald Radke, Tomasz Figa");
MODULE_DESCRIPTION("Loox 718/720 LCD backlight driver");
MODULE_LICENSE("GPL");

