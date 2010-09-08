/* Driver for the Maxim 158xx PMIC, connected to PXA27x PWR_I2C.
 *
 * Copyright (c) 2006  Michal Sroczynski <msroczyn@elka.pw.edu.pl> 
 * Copyright (c) 2006  Anton Vorontsov <cbou@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * History:
 * 2006-02-04 : Michal Sroczynski <msroczyn@elka.pw.edu.pl> 
 * 	initial driver for Asus730
 * 2006-06-05 : Anton Vorontsov <cbou@mail.ru>
 *      hx4700 port, various changes
 * 2006-12-06 : Anton Vorontsov <cbou@mail.ru>
 *      Convert to the generic PXA driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <asm/arch/pxa27x_voltage.h>

#define MAX158XX_DEF_ADDR	0x14
static u_int8_t max158xx_address = MAX158XX_DEF_ADDR;
module_param(max158xx_address, byte, 0644);
MODULE_PARM_DESC(max158xx_address, "MAX158xx address on I2C bus (0x14/0x16)");

static inline u_int8_t mv2cmd (int mv)
{
	u_int8_t val = (mv - 700) / 25;
	if (val > 31) val = 31;
	return val;
}

static struct pxa27x_voltage_chip max158xx_chip = {
	.mv2cmd		= mv2cmd,
};

static struct platform_device max158xx_pdev = {
	.name 	= "pxa27x-voltage",
	.id	= -1,
	.dev = {
		.platform_data = &max158xx_chip,
	},
};

static int __init max158xx_init(void)
{
	max158xx_chip.address = max158xx_address;
	return platform_device_register(&max158xx_pdev);
}

static void __exit max158xx_exit(void)
{
	platform_device_unregister(&max158xx_pdev);
	return;
}

module_init(max158xx_init);
module_exit(max158xx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Sroczynski <msroczyn@elka.pw.edu.pl>, "
              "Anton Vorontsov <cbou@mail.ru>");
MODULE_DESCRIPTION("Driver for the Maxim 158xx PMIC");
