/*
 * Buttons driver for Axim X50/X51(v).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Copyright (C) 2007 Pierre Gaufillet
 *
 */


#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>

#include <linux/gpio_keys.h>
#include <mach/pxa27x_keypad.h>
#include <mach/loox720-gpio.h>

/****************************************************************
 * Keyboard
 ****************************************************************/


/*	shamelessly copied from the hh.org include/linux/input_pda.h

       Instead of using any values in include/linux/input.h, we have to use
       use values < 128 due to some munging that kdrive does to get keystrokes.
       When kdrive gets its key events from evdev instead of the console,
       we should be able to switch to using input.h values and get rid of
       xmodmap. */

#define _KEY_APP1	KEY_F9		// xmodmap sees 67 + 8 = 75  
#define _KEY_APP2	KEY_F10		// xmodmap 76                
#define _KEY_APP3	KEY_F11		// xmodmap 95                
#define _KEY_APP4	KEY_F12		// xmodmap 96                

#define _KEY_RECORD	KEY_RO

/* It is highly recommended to use exactly 4 codes above for
   4 buttons the device has. This will ensure that console and 
   framebuffer applications (e.g. games) will work ok on all 
   devices. If you'd like more distinguishable names, following
   convenience defines are provided, suiting many devices. */

#define _KEY_CALENDAR	_KEY_APP1
#define _KEY_CONTACTS	_KEY_APP2
#define _KEY_MAIL	_KEY_APP3
#define _KEY_HOMEPAGE	_KEY_APP4

static unsigned int loox720_key_matrix[] = {
	// KEY( row , column , KEY_CODE )

	// row 0
	KEY( 0,0,KEY_CAMERA),KEY( 0,1,KEY_UP), KEY( 0,2,KEY_OK),
	// row 1
	KEY( 1,0,_KEY_RECORD),KEY( 1,1,KEY_DOWN), KEY( 1,2,KEY_SCROLLDOWN),
	// row 2
	KEY( 2,0,_KEY_CALENDAR),KEY( 2,1,KEY_RIGHT), KEY( 2,2,KEY_SCROLLUP),
	// row 3
	KEY( 3,0,_KEY_APP3),KEY( 3,1,KEY_LEFT),
	// row 4
	KEY( 4,0,_KEY_CONTACTS),KEY( 4,1,KEY_ENTER), 
	// row 5
	KEY( 5,0,_KEY_HOMEPAGE),
};

struct pxa27x_keypad_platform_data loox720_keypad_info = {
	.matrix_key_rows	= 6,
	.matrix_key_cols	= 3,
	.matrix_key_map		= loox720_key_matrix,
	.matrix_key_map_size	= ARRAY_SIZE(loox720_key_matrix),

	// debounce interval shamelessly copied from mainstone.c
	.debounce_interval	= 30,
};
	

static int __devinit loox720_buttons_probe(struct platform_device *dev)
{
        pxa_set_keypad_info(&loox720_keypad_info);
        return 0;
}

static struct platform_driver loox720_buttons_driver = {
        .driver                = {
            .name       = "loox720-buttons",
        },
        .probe          = loox720_buttons_probe,
};

static int __init loox720_buttons_init(void)
{
        if (!machine_is_loox720())
                return -ENODEV;

        return platform_driver_register(&loox720_buttons_driver);
}

static void __exit loox720_buttons_exit(void)
{
        platform_driver_unregister(&loox720_buttons_driver);
}

module_init(loox720_buttons_init);
module_exit(loox720_buttons_exit);

MODULE_AUTHOR ("Pierre Gaufillet");
MODULE_DESCRIPTION ("Buttons support for FSC Loox 720");
MODULE_LICENSE ("GPL");
