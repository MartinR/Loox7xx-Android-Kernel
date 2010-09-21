/* Battery (Charging & status) support for FSC Loox 720
 *
 * Authors: Harald Radke, Piotr Czechowicz, Tomasz Figa
 *
 * Note that the scaling of the ADS measurements like voltage,current and temperature
 * is not yet verified! These values are expected to change as soon as physical measurements
 * are taken, compared to corresponding Loox measurements...also note that scaling here
 * expects the ADS ref voltage to be 2500mV!
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

#include <linux/power_supply.h>

#ifdef CONFIG_LOOX720_ADS7846
extern int loox720_ads7846_battery_voltage(void);
extern int loox720_ads7846_battery_current(void);
extern int loox720_ads7846_battery_temp(void);
#endif

static unsigned int ac_irq = 0xffffffff;
static unsigned int battery_irq = 0xffffffff;
static unsigned int usb_irq = 0xffffffff;

#ifdef CONFIG_POWER_SUPPLY
/* one property function for battery, AC and USB power */
static int loox720_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	int usb_conn = (gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N) == 0);
	int ac_conn = (gpio_get_value(GPIO_NR_LOOX720_AC_IN_N) == 0);
	int battery_full = (gpio_get_value(GPIO_NR_LOOX720_BATTERY_FULL_N) == 0);
			
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE: /* either USB or AC */
	  if (psy->type == POWER_SUPPLY_TYPE_MAINS) /* AC  on/offline*/
	    val->intval = ac_conn; 
	  else /* USB on/offline */
	    val->intval = usb_conn;
	break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: /* highest measured value in uV */
	    val->intval = 4158000;
	break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN: /* lowest measured value in uV */
	    val->intval = 3635000;
	break;
	case POWER_SUPPLY_PROP_TECHNOLOGY: /* All our batteries are Li-ions (right?)*/
	    val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
	break;
	case POWER_SUPPLY_PROP_STATUS:
	  if (battery_full)
	      val->intval = POWER_SUPPLY_STATUS_FULL;
	  else
	    if (usb_conn || ac_conn)
	      val->intval = POWER_SUPPLY_STATUS_CHARGING;
	    else
	      val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	break;
#ifdef CONFIG_LOOX720_ADS7846 /* note that those scaling is more or less guessing! */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	  val->intval = loox720_ads7846_battery_voltage();
	  if (val->intval < 0)
	    return -1;
	  val->intval *= 1760; /* scaling with 1.76 (assumed ADS Vref 2500 mV!)
				  no division by 1000 since value is expected to be in uV*/
	break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
      	  val->intval = loox720_ads7846_battery_current(); /* we assume that ADS delivers correct mA value (Vref 2500mV!) */
	  if (val->intval < 0)
	    return -1;
	  val->intval *= 1000; /* value is expected to be in uA */
	break;
	case POWER_SUPPLY_PROP_TEMP:
	  val->intval = loox720_ads7846_battery_temp();
	  if (val->intval < 0)
	    return -1;
	  val->intval *= 325; /* scaling with 0.325 (assumed ADS Vref 2500mV!), */
	  val->intval /= 1000; /* multimeter val + ~10% (to HaRET values), value in 1/10 C */ 
	break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
} 

/* property for AC and USB */
static enum power_supply_property loox720_power_line_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

/* main battery properties */
static enum power_supply_property loox720_power_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
#ifdef CONFIG_LOOX720_ADS7846
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
#endif
};

static char *loox720_power_supplied_to[] = {
	"main-battery", 
/* backup-battery could be added if we knew how to access info about it */
};

static struct power_supply loox720_psy_ac = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = loox720_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(loox720_power_supplied_to),
	.properties = loox720_power_line_props,
	.num_properties = ARRAY_SIZE(loox720_power_line_props),
	.get_property = loox720_power_get_property,
};

static struct power_supply loox720_psy_usb = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.supplied_to = loox720_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(loox720_power_supplied_to),
	.properties = loox720_power_line_props,
	.num_properties = ARRAY_SIZE(loox720_power_line_props),
	.get_property = loox720_power_get_property,
};

static struct power_supply loox720_psy_battery = {
	.name = "main-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = loox720_power_battery_props,
	.num_properties = ARRAY_SIZE(loox720_power_battery_props),
	.get_property = loox720_power_get_property,
	.use_for_apm = 1,
};
#endif


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
loox720_battery_probe( struct platform_device *pdev)
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
#ifdef CONFIG_POWER_SUPPLY
	if (power_supply_register(&pdev->dev, &loox720_psy_ac) != 0)
	  dev_err(&pdev->dev, "failed to register %s power supply\n",loox720_psy_ac.name);
	if (power_supply_register(&pdev->dev, &loox720_psy_usb) != 0)
	  dev_err(&pdev->dev, "failed to register %s power supply\n",loox720_psy_usb.name);
	if (power_supply_register(&pdev->dev, &loox720_psy_battery) != 0)
	  dev_err(&pdev->dev, "failed to register %s power supply\n",loox720_psy_battery.name);
#endif
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
#ifdef CONFIG_POWER_SUPPLY
	power_supply_unregister(&loox720_psy_ac);
	power_supply_unregister(&loox720_psy_usb);
	power_supply_unregister(&loox720_psy_battery);
#endif
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

MODULE_AUTHOR("Piotr Czechowicz, Tomasz Figa, Harald Radke");
MODULE_DESCRIPTION("Loox 720 Battery Charging Support");
MODULE_LICENSE("GPL");

