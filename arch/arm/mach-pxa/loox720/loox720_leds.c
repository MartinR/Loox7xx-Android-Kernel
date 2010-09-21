#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <mach/loox720-cpld.h>
#include <mach/loox720.h>

//extern void loox720_cpld_write_masked(int regno,u32 mask, u32 value);
//extern void loox720_cpld_reg_write(int regno, u32 value);
//extern u32 loox720_egpio_cache_get(int regno);
//extern void loox720_egpio_cache_set(int regno, u32 value);

static u8 loox720_leds_cache = 0x00;

/*
	Loox 720 leds are driven by CPLD. For each LED, there are 4+1 bits on CPLD.
	
	Bit combination table for LED 1 (left):
	|---|---|---|---|---|----------------------------------------
	| A | B | C | D | X | LED status
	|---|---|---|---|---|----------------------------------------
	| 0 | 0 | 0 | 1 | 1 | Blinking green and blue
	|---|---|---|---|---|----------------------------------------
	| 0 | 1 | 0 | 1 | 1 | Blinking blue
	|---|---|---|---|---|----------------------------------------
	| 0 | 1 | 1 | 1 | 0 | Solid blue
	|---|---|---|---|---|----------------------------------------
	| 0 | 1 | 1 | 1 | 1 | Inverted blinking blue
	|---|---|---|---|---|----------------------------------------
	| 1 | 0 | 0 | 1 | 1 | Blinking green
	|---|---|---|---|---|----------------------------------------
	| 1 | 1 | 1 | 1 | 0 | Solid green
	|---|---|---|---|---|----------------------------------------
	| 1 | 1 | 1 | 1 | 1 | Inverted blinking green
	|---|---|---|---|---|----------------------------------------

	Bit combination table for LED 2 (right):
	|---|---|---|---|---|----------------------------------------
	| A | B | C | D | X | LED status
	|---|---|---|---|---|----------------------------------------
	| 1 | 1 | 0 | 0 | 1 | Blinking green and orange
	|---|---|---|---|---|----------------------------------------
	| 1 | 1 | 1 | 0 | 1 | Blinking orange
	|---|---|---|---|---|----------------------------------------
	| 1 | 0 | 0 | 0 | 0 | Solid orange
	|---|---|---|---|---|----------------------------------------
	| 1 | 0 | 0 | 0 | 1 | Inverted blinking orange
	|---|---|---|---|---|----------------------------------------
	| 0 | 1 | 0 | 0 | 1 | Blinking green
	|---|---|---|---|---|----------------------------------------
	| 0 | 0 | 1 | 0 | 0 | Solid green
	|---|---|---|---|---|----------------------------------------
	| 0 | 0 | 1 | 0 | 1 | Inverted blinking green
	|---|---|---|---|---|----------------------------------------
	| 1 | 0 | 1 | 0 | 0 | Solid red (?!)
	|---|---|---|---|---|----------------------------------------
	| 1 | 0 | 1 | 0 | 1 | Inverted blinking red (?! #2)
	|---|---|---|---|---|----------------------------------------
	
loox720_leds_cache structure:
	bit 0 - led 1 blue
	bit 1 - led 1 green
	bit 2 - led 1 blink
	bit 3 - led 1 inverted blink mode (working with blink bit set)
	bit 4 - led 2 green
	bit 5 - led 2 red
	bit 6 - led 2 blink
	bit 7 - led 2 inverted blink mode (working with blink bit set)
*/

static void	loox720_update_leds( void )
{
	u32 v = 0;
	u32 mask = 0;
	/*
	    originally	0x03500000;
	    but since cpld has become 16 bit we will do 0x0350 separately
	*/
	
	// LED1 (left one)
	switch (loox720_leds_cache & 0x0F)
	{
	    // blue
	    case 0x1: mask |= 0xE00; break;
	    case 0x5: mask |= 0x8A04; break;
	    case 0xD: mask |= 0x8E00; break;
	    
	    // green
	    case 0x2: mask |= 0xF00; break;
	    case 0x6: mask |= 0xA908; break;
	    case 0xE: mask |= 0x8F00; break;
	    
	    // both
	    case 0x7: mask |= 0xA80C; break;
	    
	    default: mask |= 0x300; break;
	}
	
	// LED2 (right one)
	switch ((loox720_leds_cache >> 4) & 0x0F)
	{
	    // green
	    case 0x1: mask |= 0x40; break;
	    case 0x5: mask |= 0x4020; break;
	    case 0xD: mask |= 0x4040; break;
	    
	    // red
	    case 0x2: mask |= 0x50; break;
	    case 0xE: mask |= 0x4050; break;
	    
	    // orange
	    case 0x3: mask |= 0x10; break;
	    case 0x7: mask |= 0x4070; break;
	    case 0xF: mask |= 0x4010; break;
	    
	    default: mask |= 0x30; break;
	}
	
	/*
	    Due to conversion of the cpld to 16bit this code
	    is marked for deletion.
	    
	    v = (loox720_egpio_cache_get(4) & ~(0x0370CFF0)) | mask;
	    loox720_egpio_cache_set(4, v);
	    loox720_cpld_reg_write(4, v);
	    
	    below a method suitable for updating bits in bulk on the cpld
	    
	    32 bit -> reg 4
	    16 bit -> reg 8 and reg 9
	    
	    loox720_cpld_write_masked( <reg> , <bit mask> , <new value>);
	    
	    this will change only the corresponding bits in the bitmask
	    other bits of <new value> will be ignored. 
	    
	    
	*/
	
	loox720_cpld_write_masked(8,0xCFF0,mask);
	loox720_cpld_write_masked(9,0x0370,0x0350);
}

void loox720_set_leds_cache(u8 value)
{
    loox720_leds_cache = value;
    loox720_update_leds();
}
EXPORT_SYMBOL(loox720_set_leds_cache);

void	loox720_enable_led(int led, int mode)
{
	led &= 0x01;
	mode &= 0x0F;
	if(mode & LOOX720_LED_BLINK_ALT)
	{
		mode |= LOOX720_LED_BLINK;
		loox720_leds_cache &= (0xF0 >> (4*led));
	}
	if(!(mode & LOOX720_LED_BLINK))
		loox720_leds_cache &= (0xF0 >> (4*led));
	if(((loox720_leds_cache >> (4*led)) & (LOOX720_LED_BLINK | LOOX720_LED_BLINK_ALT)) != (mode & (LOOX720_LED_BLINK | LOOX720_LED_BLINK_ALT)))
		loox720_leds_cache &= (0xF0 >> (4*led));
	loox720_leds_cache |= (mode << (4*led));
	loox720_update_leds();
}
EXPORT_SYMBOL(loox720_enable_led);

void    loox720_disable_led(int led, int mode)
{
    led &= 0x01;
    mode &= 0x03;
	mode = mode << (4*led);
    loox720_leds_cache &= ~mode;
	if(!(loox720_leds_cache & ((LOOX720_LED_COLOR_A | LOOX720_LED_COLOR_B) << (4*led))))
		loox720_leds_cache &= (0xF0 >> (4*led));
    loox720_update_leds();
}
EXPORT_SYMBOL(loox720_disable_led);

static int __init
loox720_leds_init( void )
{
	printk(KERN_INFO "Loox720 LED Driver\n");
	loox720_update_leds();
	return 0;
}

static void __exit
loox720_leds_exit( void )
{
	loox720_leds_cache = 0x00;
	loox720_update_leds();
}

module_init( loox720_leds_init );
module_exit( loox720_leds_exit );

MODULE_AUTHOR("Tomasz Figa");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSC Loox 720 LED driver 2.0");
