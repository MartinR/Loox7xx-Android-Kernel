/*
 *
 * Flash support for Loox 7xx PDAs
 *
 * Based on magician.c
 * Initial version by Dmitriy Geels
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <asm/gpio.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

/* StrataFlash */
/*
static void loox7xx_set_vpp(struct map_info *map, int vpp)
{
	static int old_vpp = 0;
	if (vpp == old_vpp)
		return;
	if (vpp)
		gpio_set_value(EGPIO_MAGICIAN_FLASH_VPP, 1);
	else
		gpio_set_value(EGPIO_MAGICIAN_FLASH_VPP, 0);
	old_vpp = vpp;
}
*/
#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition wince_partitions[] = {
	{
		name:		"bootloader",
		offset:		MTDPART_OFS_NXTBLK,
		size:		SZ_256K,
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	},
	{
		name:		"winmobile",
		offset:		MTDPART_OFS_NXTBLK,
		size:		135 * SZ_256K,
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	},
	{
		name:		"looxstore",
		offset:		MTDPART_OFS_NXTBLK,
		size:		MTDPART_SIZ_FULL,
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	},
};
#endif

static struct resource loox7xx_flash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + SZ_64M - 1,
	.flags = IORESOURCE_MEM,
};

static struct flash_platform_data loox7xx_flash_data = {
	.name		= "loox720-flash",
	.map_name	= "cfi_probe",
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(wince_partitions),
	.parts		= wince_partitions,
};

struct platform_device loox7xx_flash = {
	.name          = "pxa2xx-flash",
	.id            = -1,
	.num_resources = 1,
	.resource      = &loox7xx_flash_resource,
	.dev = {
		.platform_data = &loox7xx_flash_data,
	},
};

MODULE_AUTHOR("Dmitriy Geels <dmitriy.geels@gmail.com>");
MODULE_DESCRIPTION("Internal flash driver for HTC Bali");
MODULE_LICENSE("GPL");
