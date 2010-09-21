/*
 * LCD device driver for Fujitsu-Siemens Loox 720
 *
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
#include <linux/lcd.h>
#include <linux/err.h>

#include <mach/loox720-gpio.h>
#include <mach/loox720-cpld.h>
#include <linux/fb.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>



#define LOOX720_MAX_CONTRAST 255
#define LOOX720_DEFAULT_CONTRAST 77

static uint reg_perval1,reg_pwduty1;
static int loox_720_lcd_power_status;
/*--------------------------------------------------------------------------------*/

static int loox720_lcd_set_contrast(struct lcd_device *lcd, int contrast)
{
	PWM_PWDUTY1 = contrast;
	return 0;
}

/*--------------------------------------------------------------------------------*/

static	int loox720_lcd_get_contrast(struct lcd_device *lcd){
	return PWM_PWDUTY1 & 0xff;
}

/*--------------------------------------------------------------------------------*/

static int loox720_lcd_set_power (struct lcd_device *lcd, int power)
{
      loox_720_lcd_power_status = power;
      if (power == FB_BLANK_UNBLANK) {
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_COLOR_BIT, 1);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_CONSOLE_BIT, 1);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_BIT2, 1);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_BIT, 1);
		PWM_CTRL1 = 1;
	} else {
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_BIT2, 0);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_CONSOLE_BIT, 0);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_BIT, 0);
		loox720_egpio_set_bit(LOOX720_CPLD_LCD_COLOR_BIT, 0);
		PWM_CTRL1 = 0;

	}
	return 0;
}
    
/*--------------------------------------------------------------------------------*/

static int loox720_lcd_get_power (struct lcd_device *lcd) {
	return loox_720_lcd_power_status;
}

/*--------------------------------------------------------------------------------*/

struct lcd_ops loox720_lcd_ops = {
	.get_power = loox720_lcd_get_power,
	.set_power = loox720_lcd_set_power,
	.get_contrast = loox720_lcd_get_contrast,
        .set_contrast = loox720_lcd_set_contrast,
};


#ifdef CONFIG_PM
/*---------------------------------------------------------------------------------*/
static int loox720_lcd_suspend (struct platform_device *pdev, pm_message_t message)
{
	struct lcd_device *lcd_device=platform_get_drvdata(pdev);
	reg_pwduty1 = PWM_PWDUTY1;
	reg_perval1 = PWM_PERVAL1;
	lcd_set_power(lcd_device,FB_BLANK_POWERDOWN);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static int loox720_lcd_resume (struct platform_device *pdev)
{
	struct lcd_device *lcd_device=platform_get_drvdata(pdev);
	PWM_PWDUTY1 = reg_pwduty1;
	PWM_PERVAL1 = reg_perval1;
	lcd_set_power(lcd_device,FB_BLANK_UNBLANK);
	return 0;
}

/*---------------------------------------------------------------------------------*/
#else
#define loox720_lcd_suspend NULL
#define loox720_lcd_resume NULL
#endif

static int loox720_lcd_probe (struct platform_device *pdev)
{
	struct lcd_device *lcd_device;
	
	if (! machine_is_loox720 ())
		return -ENODEV;
	lcd_device = lcd_device_register ("loox720-lcd", &pdev->dev, NULL, &loox720_lcd_ops);
	if (IS_ERR (lcd_device))
		return -1;
	platform_set_drvdata(pdev, lcd_device);
	lcd_device->props.max_contrast = LOOX720_MAX_CONTRAST;
	lcd_set_power(lcd_device,FB_BLANK_UNBLANK);
	loox720_lcd_set_contrast(lcd_device,LOOX720_DEFAULT_CONTRAST);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static int loox720_lcd_remove (struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);
	backlight_update_status(bd);
	backlight_device_unregister(bd);
	return 0;
}

/*---------------------------------------------------------------------------------*/

static struct platform_driver loox720_lcd_driver = {
	.probe		= loox720_lcd_probe,
	.remove		= loox720_lcd_remove,
	.suspend	= loox720_lcd_suspend,
	.resume		= loox720_lcd_resume,
	.driver		= {
		.name	= "loox720-lcd",
	},
};

static int __init loox720_lcd_init(void)
{
	return platform_driver_register(&loox720_lcd_driver);
}


static void __exit loox720_lcd_exit (void)
{
	platform_driver_unregister(&loox720_lcd_driver);
}

module_init (loox720_lcd_init);
module_exit (loox720_lcd_exit);

MODULE_AUTHOR("Harald Radke, Tomasz Figa");
MODULE_DESCRIPTION("Loox 718/720 LCD device driver");
MODULE_LICENSE("GPL");

