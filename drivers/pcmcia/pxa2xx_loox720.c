/*
 * PCMCIA Support for HP iPAQ hx2750
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@o-hand.com>
 *
 * Based on pxa2xx_mainstone.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include <pcmcia/ss.h>

#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/arch/loox720-gpio.h>
#include <asm/arch/loox720-cpld.h>
#include <asm/arch/loox720.h>
#include <asm/arch/irqs.h>
#include <linux/platform_device.h>

#include "soc_common.h"

static struct pcmcia_irqs irqs[] = {
	{ 1, LOOX720_CPLD_IRQ(CARD_DETECT), "PCMCIA1 CD" },
};

static int loox720_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	skt->irq = (skt->nr == 1) ? LOOX720_CPLD_IRQ(CF) : LOOX720_CPLD_IRQ(WIFI);
	printk(KERN_INFO "loox720_pcmcia: Using IRQ %d for socket %d.\n", skt->irq, skt->nr);
	if(skt->nr == 1)
		return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
	else
		return 0;
}

static void loox720_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	if(skt->nr == 1)
		soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

#define GPLR_BIT(n) (GPLR((n)) & GPIO_bit((n)))
#define GPSR_BIT(n) (GPSR((n)) = GPIO_bit((n)))
#define GPCR_BIT(n) (GPCR((n)) = GPIO_bit((n)))

static void loox720_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	if(skt->nr == 1){
		state->detect = loox720_cpld_reg_test(2,LOOX720_CPLD_CF_DETECT_N) ? 0 : 1;
		state->ready  = loox720_cpld_reg_test(2,LOOX720_CPLD_CF_READY) ? 1 : 0;
	}
	else{
		state->detect = 1;
		state->ready  = (loox720_cpld_reg_test(2,(LOOX720_CPLD_WIFI_ENABLED | LOOX720_CPLD_WIFI_READY)) 
				== (LOOX720_CPLD_WIFI_ENABLED | LOOX720_CPLD_WIFI_READY)) ? 1 : 0;
	}

	state->bvd1   = 1;  /* not available */
	state->bvd2   = 1;  /* not available */
	state->vs_3v  = 1;  /* not available */
	state->vs_Xv  = 0;  /* not available */
	state->wrprot = 0;  /* not available */
}

static int loox720_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	int ret = 0;

	if(state->flags & SS_RESET) {
		if(skt->nr == 1)
			loox720_egpio_set_bit(LOOX720_CPLD_CF_RESET, 1);
		else
			gpio_set_value(GPIO_NR_LOOX720_WIFI_RST, 1);
	} else {
		if(skt->nr == 1)
			loox720_egpio_set_bit(LOOX720_CPLD_CF_RESET, 0);
		else
			gpio_set_value(GPIO_NR_LOOX720_WIFI_RST, 0);
	}

	/* Apply socket voltage */
	switch (state->Vcc) {
		case 0:
			if(skt->nr == 1)
			{
		                loox720_egpio_set_bit(LOOX720_CPLD_CF_3V3_BIT, 0);
				loox720_egpio_set_bit(LOOX720_CPLD_CF_5V_BIT, 0);
			}
			else
			{
				loox720_egpio_set_bit(LOOX720_CPLD_WIFI_POWER, 0);
				gpio_set_value(GPIO_NR_LOOX720_WIFI_PWR, 0);
				loox720_disable_led(LOOX720_LED_LEFT, LOOX720_LED_COLOR_B);
			}
			break;
		case 50:
		case 33:
			/* Apply power to socket */
			if(skt->nr == 1)
			{
				loox720_egpio_set_bit(LOOX720_CPLD_CF_5V_BIT, 1);
				loox720_egpio_set_bit(LOOX720_CPLD_CF_3V3_BIT, 1);
			}
			else
			{
				loox720_egpio_set_bit(LOOX720_CPLD_WIFI_POWER, 1);
				gpio_set_value(GPIO_NR_LOOX720_WIFI_PWR, 1);
				loox720_enable_led(LOOX720_LED_LEFT, LOOX720_LED_COLOR_B | LOOX720_LED_BLINK);
			}
			break;
		default:
			printk (KERN_ERR "%s: Unsupported Vcc:%d\n", __FUNCTION__, state->Vcc);
			ret = -1;
			break;
	}
	
	return ret;
}

static void loox720_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void loox720_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level loox720_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= loox720_pcmcia_hw_init,
	.hw_shutdown		= loox720_pcmcia_hw_shutdown,
	.socket_state		= loox720_pcmcia_socket_state,
	.configure_socket	= loox720_pcmcia_configure_socket,
	.socket_init		= loox720_pcmcia_socket_init,
	.socket_suspend		= loox720_pcmcia_socket_suspend,
	.nr			= 2,
};

static struct platform_device loox720_pcmcia_device = {
	.name		= "pxa2xx-pcmcia",
	.dev		= {
		.platform_data 	= &loox720_pcmcia_ops,
	},
};

static int __init loox720_pcmcia_init(void)
{
	int ret;
	if(gpio_request(GPIO_NR_LOOX720_WIFI_PWR, "WiFi power") != 0) {
		printk(KERN_ERR "Failed to request WiFi power GPIO\n");
		return -ENODEV;
	}
		
	if(gpio_request(GPIO_NR_LOOX720_WIFI_RST, "WiFi reset") != 0) {
		printk(KERN_ERR "Failed to request WiFi reset GPIO\n");
		gpio_free(GPIO_NR_LOOX720_WIFI_PWR);
		return -ENODEV;
	}
	if((ret = platform_device_register(&loox720_pcmcia_device)) != 0) {
		gpio_free(GPIO_NR_LOOX720_WIFI_PWR);
		gpio_free(GPIO_NR_LOOX720_WIFI_RST);
		return ret;
	}
	return 0;
}

static void __exit loox720_pcmcia_exit(void)
{
	gpio_free(GPIO_NR_LOOX720_WIFI_PWR);
	gpio_free(GPIO_NR_LOOX720_WIFI_RST);
	platform_device_unregister(&loox720_pcmcia_device);
}

module_init(loox720_pcmcia_init);
module_exit(loox720_pcmcia_exit);

MODULE_AUTHOR("Richard Purdie <richard@o-hand.com>");
MODULE_DESCRIPTION("iPAQ hx2750 PCMCIA Support");
MODULE_LICENSE("GPL");
