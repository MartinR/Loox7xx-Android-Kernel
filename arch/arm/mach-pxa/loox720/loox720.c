/*
 *
 * Hardware definitions for Fujitsu-Siemens PocketLOOX 7xx series handhelds
 *
 * Copyright 2004 Fujitsu-Siemens Computers.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * based on hx4700.c
 */


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/serial.h>
#include <linux/gpio_keys.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
//#include <asm/arch/ssp.h>
#include <asm/arch/regs-ssp.h>
#include <asm/arch/pxa2xx-regs.h>
#include <asm/arch/pxa2xx_spi.h>
#include <asm/arch/udc.h>
#include <asm/arch/audio.h>
#include <asm/arch/ohci.h>
#include <asm/arch/irda.h>
#include <asm/arch/mmc.h>
#include <asm/arch/irqs.h>
#include <asm/arch/loox720.h>
#include <asm/arch/loox720-gpio.h>
#include <asm/arch/loox720-cpld.h>
#include <asm/arch/mfp-pxa27x.h>
#include <asm/arch/i2c.h>
#include <asm/arch/sharpsl.h>
#include <linux/spi/spi.h>

#include "../generic.h"
#include "loox720_core.h"
#include "../sharpsl.h"
/*
#include <linux/adc_battery.h>
*/

/*--------------------------------------------------------------------------------*/

/*
 * Pin Configuration
 */
 
 static unsigned long loox720_pin_config[] = {
/* Crystal and Clock Signals */
	GPIO10_HZ_CLK,
/* PC CARD */
	GPIO15_nPCE_1,
	GPIO78_nPCE_2,
	GPIO55_nPREG,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO49_nPWE,
	GPIO48_nPOE,
	GPIO57_nIOIS16,
	GPIO56_nPWAIT,
	GPIO79_PSKTSEL,
/* SDRAM and Static Memory I/O Signals */
	GPIO20_nSDCS_2,
	GPIO21_nSDCS_3,
	GPIO80_nCS_4,
	GPIO33_nCS_5,
/* Miscellaneous I/O and DMA Signals */
	GPIO18_RDY,
/* FFUART */
	GPIO35_FFUART_CTS,
	GPIO37_FFUART_DSR,
	GPIO34_FFUART_RXD,
	GPIO39_FFUART_TXD,
	GPIO41_FFUART_RTS,
	GPIO40_FFUART_DTR,
/* BTUART */
	GPIO44_BTUART_CTS,
	GPIO42_BTUART_RXD,
	GPIO45_BTUART_RTS,
	GPIO43_BTUART_TXD,
/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,
/* PWM 0/1/2/3 */
	GPIO16_PWM0_OUT,
	GPIO17_PWM1_OUT,
/* SSP 1 */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,
/* QCI - default to Master Mode: CIF_FV/CIF_LV Direction In */
	GPIO27_CIF_DD_0,
	GPIO114_CIF_DD_1,
	GPIO116_CIF_DD_2,
	GPIO115_CIF_DD_3,
	GPIO83_CIF_DD_4,
	GPIO82_CIF_DD_5,
	GPIO93_CIF_DD_6,
	GPIO108_CIF_DD_7,
	GPIO54_CIF_PCLK,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,
	GPIO53_CIF_MCLK,
/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
/* LCD */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,
/* USB Host Port 1/2 */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
/* I2S */
	GPIO29_I2S_SDATA_IN,
	GPIO28_I2S_BITCLK_OUT,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,
/* Keypad */
	GPIO100_KP_MKIN_0,
	GPIO101_KP_MKIN_1,
	GPIO102_KP_MKIN_2,
	GPIO97_KP_MKIN_3,
	GPIO98_KP_MKIN_4,
	GPIO99_KP_MKIN_5,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,
/* Platform-specific */
	MFP_CFG_OUT(GPIO3, AF0, DRIVE_HIGH),	// setup but function unknown
	MFP_CFG_OUT(GPIO4, AF0, DRIVE_LOW),	// setup but function unknown
	GPIO9_GPIO,
	GPIO11_GPIO,
	GPIO12_GPIO,
	GPIO13_GPIO,
	MFP_CFG_OUT(GPIO14, AF0, DRIVE_LOW),
	MFP_CFG_OUT(GPIO19, AF0, DRIVE_LOW),
	MFP_CFG_OUT(GPIO22, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO36, AF0, DRIVE_HIGH),
	GPIO38_GPIO,				// setup but function unknown
	GPIO52_GPIO,
	GPIO74_GPIO,
	MFP_CFG_OUT(GPIO75, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO81, AF0, DRIVE_LOW),	// setup but function unknown
	MFP_CFG_OUT(GPIO86, AF0, DRIVE_HIGH),	// setup but function unknown
	GPIO87_GPIO,
	MFP_CFG_OUT(GPIO90, AF0, DRIVE_HIGH),	// setup but function unknown
	MFP_CFG_OUT(GPIO91, AF0, DRIVE_HIGH),	// setup but function unknown
	GPIO94_GPIO,
	MFP_CFG_OUT(GPIO95, AF0, DRIVE_LOW),	// setup but function unknown
	GPIO96_GPIO,				// setup but function unknown
	MFP_CFG_OUT(GPIO106, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO107, AF0, DRIVE_HIGH),
	GPIO119_GPIO,				// setup but function unknown
	GPIO120_GPIO,
 };

/*--------------------------------------------------------------------------------*/

/*
 * IRDA
 */

static void loox_irda_transceiver_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);

	gpio_set_value(GPIO_NR_LOOX720_IR_ON_N, mode & IR_OFF);

	local_irq_restore(flags);
}

static int loox_irda_startup(struct device *dev)
{
	return gpio_request(GPIO_NR_LOOX720_IR_ON_N, "IrDA enable");
}

static void loox_irda_shutdown(struct device *dev)
{
	gpio_free(GPIO_NR_LOOX720_IR_ON_N);
}

static struct pxaficp_platform_data loox_ficp_info = {
	.transceiver_cap  = IR_SIRMODE | IR_FIRMODE | IR_OFF,
	.transceiver_mode = loox_irda_transceiver_mode,
	.startup = loox_irda_startup,
	.shutdown = loox_irda_shutdown,
};

/*--------------------------------------------------------------------------------*/

/*
 * Bluetooth - Relies on other loadable modules, like ASIC3 and Core,
 * so make the calls indirectly through pointers. Requires that the
 * loox720 bluetooth module be loaded before any attempt to use
 * bluetooth (obviously).
 */

#ifdef CONFIG_LOOX720_BT

static struct platform_device loox720_bt = {
        .name = "loox720-bt",
};

#endif

/*--------------------------------------------------------------------------------*/

/* PXA2xx Keys */

static struct gpio_keys_button loox720_button_table[] = {
	[0] = {
		.desc	= "wakeup",
		.code	= KEY_SUSPEND,
		.type	= EV_KEY,
		.gpio	= GPIO_NR_LOOX720_KEY_ON,
		.wakeup	= 1,
	},
};

static struct gpio_keys_platform_data loox720_pxa_keys_data = {
	.buttons = loox720_button_table,
	.nbuttons = ARRAY_SIZE(loox720_button_table),
};

static struct platform_device loox720_pxa_keys = {
	.name = "gpio-keys",
	.id   = -1,
	.dev  = {
		.platform_data = &loox720_pxa_keys_data,
	},
};

/*--------------------------------------------------------------------------------*/

/*
 * SPI
 */
 
/* borrowed from lubbock.c */
static struct pxa2xx_spi_master pxa_ssp_master_info = {
	.clock_enable = CKEN_SSP1,
	.num_chipselect	= 1,
	.enable_dma = 0,
};

static struct platform_device pxa_ssp = {
	.name		= "pxa2xx-spi",
	.id		= 1,
	.dev = {
		.platform_data	= &pxa_ssp_master_info,
	},
};

/*--------------------------------------------------------------------------------*/

/*
 * Touchscreen
 */

static struct platform_device loox720_ts = {
	.name = "loox720-ts",
};


/*
 * Buttons
 */

static struct platform_device loox720_buttons = {
	.name = "loox720-buttons",
	.id		= -1,
};

/*--------------------------------------------------------------------------------*/

/*
 * Battery charging
 */

static struct platform_device loox720_battery = {
	.name = "loox720-battery",
	.id		= -1,
};

/*--------------------------------------------------------------------------------*/

/*
 * ads7846 test device, provides crude touchscreen support through bitbanging.
 */

static struct platform_device loox720_ads7846 = {
	.name = "loox720-ads7846",
	.id		= -1,
};

/*--------------------------------------------------------------------------------*/

/*
 * USB Device
 */

static int
udc_detect(void)
{
        int detected = (gpio_get_value(GPIO_NR_LOOX720_USB_DETECT_N)==0);
	printk (KERN_NOTICE "udc_detect: %d\n", detected);
	return detected;
}

static void
udc_command(int cmd)
{
	switch (cmd)
	{
		case PXA2XX_UDC_CMD_DISCONNECT:
			printk (KERN_NOTICE "USB cmd disconnect\n");
			loox720_egpio_set_bit(LOOX720_CPLD_USB_PULLUP_BIT, 0);
			break;

		case PXA2XX_UDC_CMD_CONNECT:
			printk (KERN_NOTICE "USB cmd connect\n");
			loox720_egpio_set_bit(LOOX720_CPLD_USB_PULLUP_BIT, 1);
			break;
		default:
			printk (KERN_ERR "USB: invalid command: %d\n", cmd);
	}
}

static struct pxa2xx_udc_mach_info loox720_udc_info __initdata = {
	.udc_is_connected = udc_detect,
	.udc_command      = udc_command,
};

/*--------------------------------------------------------------------------------*/

/*
 * USB Host
 */

static int loox720_ohci_init(struct device *dev)
{
	/* missing GPIO setup here */

	/* no idea what this does, got the values from haret
	UHCHR = (UHCHR | UHCHR_SSEP2 | UHCHR_PCPL | UHCHR_CGR) &
			    ~(UHCHR_SSEP1 | UHCHR_SSEP3 | UHCHR_SSE);
		we don't know yet how to init.. */
	UHCHR = (UHCHR | UHCHR_SSEP3 | UHCHR_PSPL | UHCHR_CGR) &
			    ~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSE);
	return 0;
}

static struct pxaohci_platform_data loox720_ohci_info = {
	        .port_mode = PMM_PERPORT_MODE,
		.init = loox720_ohci_init,
};

/*--------------------------------------------------------------------------------*/

/*
 * Flash
 */

extern struct platform_device loox7xx_flash;

/*--------------------------------------------------------------------------------*/

/*
 * MMC/SD
 */

static struct platform_device loox720_pm = {
	.name = "loox720-pm",
	.id		= -1,
};

/*--------------------------------------------------------------------------------*/

/*
 * MMC/SD
 */

static int loox7xx_mci_init(struct device *dev,
				irq_handler_t detect_irq, void *data)
{
	int err;
	err = gpio_request(GPIO_NR_LOOX720_MMC_DETECT_N, "SD_DETECT_N");
	if (err)
		goto err_request_detect;
	gpio_direction_input(GPIO_NR_LOOX720_MMC_DETECT_N);
	err = request_irq(IRQ_GPIO(GPIO_NR_LOOX720_MMC_DETECT_N), detect_irq,
				IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"SD card detect", data);
	if (err)
		goto err_request_irq;
	err = gpio_request(GPIO_NR_LOOX720_MMC_RO, "SD_READONLY");
	if (err)
		goto err_request_readonly;
	gpio_direction_input(GPIO_NR_LOOX720_MMC_RO);

	return 0;

err_request_readonly:
	free_irq(IRQ_GPIO(GPIO_NR_LOOX720_MMC_DETECT_N), data);
err_request_irq:
	gpio_free(GPIO_NR_LOOX720_MMC_DETECT_N);
err_request_detect:
	return err;
}

static void loox7xx_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *pdata = dev->platform_data;

	loox720_egpio_set_bit(LOOX720_CPLD_SD_BIT, (1 << vdd) & pdata->ocr_mask);
}

static int loox7xx_mci_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO_NR_LOOX720_MMC_RO);
}

static void loox7xx_mci_exit(struct device *dev, void *data)
{
	gpio_free(GPIO_NR_LOOX720_MMC_RO);
	free_irq(IRQ_GPIO(GPIO_NR_LOOX720_MMC_DETECT_N), data);
	gpio_free(GPIO_NR_LOOX720_MMC_DETECT_N);
}

static struct pxamci_platform_data loox7xx_mci_info = {
	.ocr_mask 	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init     	= loox7xx_mci_init,
	.get_ro   	= loox7xx_mci_get_ro,
	.setpower 	= loox7xx_mci_setpower,
	.exit     	= loox7xx_mci_exit,
};

/*--------------------------------------------------------------------------------*/

/*
 * Loox 720
 */

static struct platform_device *devices[] __initdata = {
	&loox720_battery,
        &pxa_ssp,
#ifdef CONFIG_LOOX720_TS
	&loox720_ts,
#endif
	&loox720_pxa_keys,
#ifdef CONFIG_LOOX720_BUTTONS
	&loox720_buttons,
#endif
#ifdef CONFIG_LOOX720_BT
	&loox720_bt,
#endif
#ifdef CONFIG_LOOX720_FLASH
	&loox7xx_flash,
#endif
	&loox720_pm,
};

#ifdef CONFIG_LOOX720_ADS7846
static struct pxa2xx_spi_chip loox720_spi_ads7846_hw = {
	.tx_threshold		= 8,
	.rx_threshold		= 4,
	//.cs_control		= ads7846_cs,
	.timeout		= 1000,
	.enable_loopback	= 0,
	.dma_burst_size		= 0,
};
static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias		= "loox720_spi_ads7846",
		.controller_data 	= &loox720_spi_ads7846_hw,
		.irq			= IRQ_GPIO(GPIO_NR_LOOX720_TOUCHPANEL_IRQ_N),
		.max_speed_hz		= 200000 ,
		.bus_num		= 1,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
	},
/*
	{
		.modalias		= "spidev",
		.max_speed_hz		= 125000 ,
		.bus_num		= 1,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
	},
*/	
};
#endif

static void __init loox720_init( void )
{
#if 0	// keep for reference, from bootldr
	GPSR0 = 0x0935ede7;
	GPSR1 = 0xffdf40f7;
	GPSR2 = 0x0173c9f6;
	GPSR3 = 0x01f1e342;
	GPCR0 = ~0x0935ede7;
	GPCR1 = ~0xffdf40f7;
	GPCR2 = ~0x0173c9f6;
	GPCR3 = ~0x01f1e342;
	GPDR0 = 0xda7a841c;
	GPDR1 = 0x68efbf83;
	GPDR2 = 0xbfbff7db;
	GPDR3 = 0x007ffff5;
	GAFR0_L = 0x80115554;
	GAFR0_U = 0x591a8558;
	GAFR1_L = 0x600a9558;
	GAFR1_U = 0x0005a0aa;
	GAFR2_L = 0xa0000000;
	GAFR2_U = 0x00035402;
	GAFR3_L = 0x00010000;
	GAFR3_U = 0x00001404;
	MSC0 = 0x25e225e2;
	MSC1 = 0x12cc2364;
	MSC2 = 0x16dc7ffc;
#endif

	ARB_CNTRL = ARB_CORE_PARK | 0x234;
	pxa2xx_mfp_config(loox720_pin_config, ARRAY_SIZE(loox720_pin_config));

	gpio_request(GPIO_NR_LOOX720_USB_DETECT_N, "UDC VBus detect");
	pxa_set_udc_info(&loox720_udc_info);
	pxa_set_ficp_info(&loox_ficp_info);
	pxa_set_ohci_info(&loox720_ohci_info);
	pxa_set_i2c_info(NULL);

//#ifdef CONFIG_LOOX720_BT
//	pxa_set_btuart_info(&loox720_pxa_bt_funcs);
//#endif
	pxa_set_mci_info(&loox7xx_mci_info);
//	corgi_ssp_set_machinfo(&loox720_ssp_machinfo);

	platform_add_devices( devices, ARRAY_SIZE(devices) );
#ifdef CONFIG_LOOX720_ADS7846
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
#endif
}

MACHINE_START(LOOX720, "FSC Loox 720")
	.phys_io = 0x40000000,
	.io_pg_offst = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= CONFIG_DRAM_BASE + 0x100,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= loox720_init,
MACHINE_END
