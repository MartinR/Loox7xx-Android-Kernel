/*
 * loox7xx.c  --  SoC audio for Loox 7xx
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood
 *         liam.girdwood@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  Mainstone audio amplifier code taken from arch/arm/mach-pxa/mainstone.c
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Revision history
 *    30th Oct 2005   Initial version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <mach/audio.h>
#include <mach/hardware.h>

#include "../codecs/wm8750.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-i2s.h"

#include <mach/loox720-cpld.h>
#include <mach/loox720-gpio.h>

#include <mach/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>

#include "loox7xx.h"

#define LOOX_HP_ON      0
#define LOOX_HP_OFF     1
#define LOOX_SPK_ON     0
#define LOOX_SPK_OFF    1

static int loox_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params);
static int loox_startup(struct snd_pcm_substream *substream);
static int loox_wm8750_init(struct snd_soc_codec *codec);

static struct snd_soc_card loox;


static struct snd_soc_ops loox_ops = {
	.startup = loox_startup,
	.hw_params = loox_hw_params,
};

static struct snd_soc_dai_link loox_dai = {
	.name = "WM8750",
	.stream_name = "WM8750 HiFi",
	.cpu_dai = &pxa_i2s_dai,
	.codec_dai = &wm8750_dai,
	.init = loox_wm8750_init,
	.ops = &loox_ops,
};

/* for headphone switch IRQ handling */
static struct work_struct hp_switch_work;

static int loox_wm8780_resume_pre(struct platform_device *pdev) {
  disable_irq(IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET));
  loox720_egpio_set_bit(LOOX720_CPLD_SOUND_BIT, 0);
  loox720_egpio_set_bit(LOOX720_CPLD_SND_AMPLIFIER_BIT, 0);
  return 0;
}

static int loox_wm8780_resume_post(struct platform_device *pdev) {
  schedule_work(&hp_switch_work);
  return 0;
}

static int loox_wm8780_suspend_post(struct platform_device *pdev) {
  disable_irq(IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET));
  loox720_egpio_set_bit(LOOX720_CPLD_SOUND_BIT, 0);
  loox720_egpio_set_bit(LOOX720_CPLD_SND_AMPLIFIER_BIT, 0);
  return 0;
}

static struct snd_soc_card loox = {
	.name = "Loox Audio",
	.dai_link = &loox_dai,
	.num_links = 1,
	.resume_pre = loox_wm8780_resume_pre,
	.resume_post = loox_wm8780_resume_post,
	.suspend_post = loox_wm8780_suspend_post,
	.platform = &pxa2xx_soc_platform,
};

static struct wm8750_setup_data loox_wm8750_setup = {
	.i2c_address = 0x1a,
};


static struct snd_soc_device loox_snd_devdata = {
	.card = &loox,
	.codec_dev = &soc_codec_dev_wm8750,
	.codec_data = &loox_wm8750_setup,
};

static struct platform_device *loox_snd_device;

/* note that with this switching code ROUT2/LOUT2 (the VoIP speaker) is always deactivated */

/* irq for headphone switch GPIO */
static unsigned int hp_switch_irq;

static int loox_jack_func;
static int loox_spk_func;

static void loox_ext_control(struct snd_soc_codec *codec)
{
    switch (loox_spk_func) {
        case LOOX_SPK_ON:
            snd_soc_dapm_enable_pin(codec, "Spk");
            break;
        case LOOX_SPK_OFF:
            snd_soc_dapm_disable_pin(codec, "Spk");
            break;
    }

    switch (loox_jack_func) {
        case LOOX_HP_ON:
            snd_soc_dapm_enable_pin(codec, "Headphone Jack");
            break;
        case LOOX_HP_OFF:
            snd_soc_dapm_disable_pin(codec, "Headphone Jack");
            break;
    }
    snd_soc_dapm_sync(codec);
}

static int loox_startup(struct snd_pcm_substream *substream)
{
        struct snd_soc_pcm_runtime *rtd = substream->private_data;
        struct snd_soc_codec *codec = rtd->socdev->card->codec;

        /* check the jack status at stream startup */
        loox_ext_control(codec);
        return 0;
}

static int loox_get_jack(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
        ucontrol->value.integer.value[0] = loox_jack_func;
        return 0;
}

static int loox_set_jack(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
        struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

        if (loox_jack_func == ucontrol->value.integer.value[0])
                return 0;

        loox_jack_func = ucontrol->value.integer.value[0];
        loox_ext_control(codec);
        return 1;
}

static int loox_get_spk(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
        ucontrol->value.integer.value[0] = loox_spk_func;
        return 0;
}

static int loox_set_spk(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
        struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

        if (loox_spk_func == ucontrol->value.integer.value[0])
                return 0;

        loox_spk_func = ucontrol->value.integer.value[0];
        loox_ext_control(codec);
        return 1;
}

/* Loox dapm widgets */
static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
        SND_SOC_DAPM_HP("Headphone Jack", NULL),
        SND_SOC_DAPM_SPK("Spk", NULL),
        SND_SOC_DAPM_LINE("Line Jack", NULL),
        SND_SOC_DAPM_MIC("Mic", NULL),
};

/* Loox audio_map */
static const struct snd_soc_dapm_route audio_map[] = {

        /* headphone connected to LOUT1 */
        {"Headphone Jack", NULL, "LOUT1"},

        /* speaker connected to OUT3  */
        {"Spk", NULL , "OUT3"},

        /* mic is connected to LINPUT1 */
        {"LINPUT1", NULL, "Mic"},

        /* line is connected to RINPUT1 */
        {"RINPUT1", NULL, "Line Jack"},
};

static const char *jack_function[] = {"On", "Off"};
static const char *spk_function[] = {"On", "Off"};
static const struct soc_enum loox_enum[] = {
        SOC_ENUM_SINGLE_EXT(2, jack_function),
        SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new wm8750_loox_controls[] = {
        SOC_ENUM_EXT("Jack Function", loox_enum[0], loox_get_jack,
                loox_set_jack),
        SOC_ENUM_EXT("Speaker Function", loox_enum[1], loox_get_spk,
                loox_set_spk),
};

static int loox_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		clk = 11289600;
		break;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as input (unused) */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	// we have setup all registers when we booted..
	// so we probably don't need this anymore..
	// also.. it does not compile ;-)
	//pxa_gpio_mode(GPIO_NR_LOOX720_I2S_SYSCLK_MD);

	return 0;
}

static void loox_wm8750_hp_switch(struct work_struct *work)
{
	int hp_in;
	if (loox_snd_devdata.dev != NULL) {
	    hp_in = gpio_get_value(GPIO_NR_LOOX720_HEADPHONE_DET);
#if 0
	    if (hp_in) {
		snd_soc_dapm_disable_pin(loox_snd_devdata.card->codec,"OUT3");
		snd_soc_dapm_enable_pin(loox_snd_devdata.card->codec,"LOUT1");
	    } else {
		snd_soc_dapm_enable_pin(loox_snd_devdata.card->codec,"OUT3");
		snd_soc_dapm_disable_pin(loox_snd_devdata.card->codec,"LOUT1");
	    }
#endif
	    enable_irq(IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET));
	}
}

static irqreturn_t loox_wm8750_hp_switch_isr(int irq, void *data)
{
	disable_irq(IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET));
	schedule_work(&hp_switch_work);
	return IRQ_HANDLED;
}

#if 0
/* several controls aren't of interest on the Loox, hide those from mixer apps,
   if necessary set certain values
*/
static void loox_wm8750_set_and_hide_controls(struct snd_card *card) 
{
	unsigned int i;
	struct snd_kcontrol *ctl;
	struct snd_kcontrol_volatile *vd;
	struct snd_ctl_elem_value val;
	struct snd_ctl_elem_id id;
	unsigned int index_offset;

	for (i = 0; i < ARRAY_SIZE(loox7xx_hidden_controls); i++) {
          memset(&id, 0, sizeof(id));
          strcpy(id.name, loox7xx_hidden_controls[i].name);
          id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
/* find and deactivate control */
	  down_write(&card->controls_rwsem);
	  ctl = snd_ctl_find_id(card, &id);
	  index_offset = snd_ctl_get_ioff(ctl, &ctl->id);
	  vd = &ctl->vd[index_offset];
	  vd->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	  up_write(&card->controls_rwsem);
	  snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO, &id);
/* set deactivated controls which need an init value != default */
	  memset(&val, 0, sizeof(val));
	  switch(loox7xx_hidden_controls[i].type){
	  case LOOX_DAPM_ENUM:
	    val.value.enumerated.item[0] = loox7xx_hidden_controls[i].val;
	    ctl->put(ctl,&val);
	  break;
	  case LOOX_DAPM_VOLSW:
	    val.value.integer.value[0] = loox7xx_hidden_controls[i].val;
	    ctl->put(ctl,&val);
	  break;
	  default:
	  break;
	  }
        }
}

/* alsalib mixer layer is kinda picky on name of controls if it comes
   to deciding wether its a playback or a capture control. 
   Let's help it a bit!  (-;
*/
static void loox_wm8750_rename_controls(struct snd_card *card) 
{
	unsigned int i;
        struct snd_ctl_elem_id old_id, new_id;

	for (i = 0; i < ARRAY_SIZE(loox7xx_renamed_controls); i+=2) {
          memset(&old_id, 0, sizeof(old_id));
	  memset(&new_id, 0, sizeof(new_id));
          strcpy(old_id.name, loox7xx_renamed_controls[i]);
          old_id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
          strcpy(new_id.name, loox7xx_renamed_controls[i+1]);
          new_id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	  snd_ctl_rename_id(card, &old_id, &new_id);
        }
}
#endif

/* customize controls and set up headphone detection */
static int loox_wm8750_init(struct snd_soc_codec *codec)
{
#if 0
    loox_wm8750_set_and_hide_controls(codec->card);
    loox_wm8750_rename_controls(codec->card);
#endif

    hp_switch_irq = IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET);
    if (request_irq( hp_switch_irq, loox_wm8750_hp_switch_isr,IRQF_DISABLED|IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "Headphone detect", NULL) != 0) {
        printk( KERN_ERR "Unable to aquire headphone detect IRQ.\n" );
        free_irq( hp_switch_irq, NULL);
        hp_switch_irq =0;
        return 0;
    }
    snd_soc_dapm_nc_pin(codec, "RINPUT2");
    snd_soc_dapm_nc_pin(codec, "LINPUT3");
    snd_soc_dapm_nc_pin(codec, "RINPUT3");
    snd_soc_dapm_nc_pin(codec, "LINPUT3");
    snd_soc_dapm_nc_pin(codec, "MONO1");

/* deactivating VoIP speaker */
    snd_soc_dapm_disable_pin(codec,"LOUT2");
    snd_soc_dapm_disable_pin(codec,"ROUT2");
/* ROUT1 is always on */
    snd_soc_dapm_enable_pin(codec,"ROUT1");
    disable_irq(IRQ_GPIO(GPIO_NR_LOOX720_HEADPHONE_DET));
    INIT_WORK(&hp_switch_work, loox_wm8750_hp_switch);
    schedule_work(&hp_switch_work);

    /* Add Loox specific controls */
    snd_soc_add_controls(codec, wm8750_loox_controls, ARRAY_SIZE(wm8750_loox_controls));

    /* Add Loox specific widgets */
    snd_soc_dapm_new_controls(codec, wm8750_dapm_widgets, ARRAY_SIZE(wm8750_dapm_widgets));

    /* Set up Loox specific audio paths */
    snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

    snd_soc_dapm_sync(codec);
    return 0;
}

static int __init loox_init(void)
{
    int ret;
    printk(KERN_INFO "Loox 720 SoC sound Driver\n");
    loox720_egpio_set_bit(LOOX720_CPLD_SOUND_BIT, 1);
    loox720_egpio_set_bit(LOOX720_CPLD_SND_AMPLIFIER_BIT, 1);

    loox_snd_device = platform_device_alloc("soc-audio", -1);
    if (!loox_snd_device)
        return -ENOMEM;

    platform_set_drvdata(loox_snd_device, &loox_snd_devdata);
    loox_snd_devdata.dev = &loox_snd_device->dev;
    ret = platform_device_add(loox_snd_device);
    if (ret)
        platform_device_put(loox_snd_device);
    return ret;
}

static void __exit loox_exit(void)
{
    if (hp_switch_irq != 0)
        free_irq( hp_switch_irq, NULL);
    platform_device_unregister(loox_snd_device);
    loox720_egpio_set_bit(LOOX720_CPLD_SOUND_BIT, 0);
    loox720_egpio_set_bit(LOOX720_CPLD_SND_AMPLIFIER_BIT, 0);
}

module_init(loox_init);
module_exit(loox_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, liam.girdwood@wolfsonmicro.com, www.wolfsonmicro.com");
MODULE_DESCRIPTION("ALSA SoC Loox");
MODULE_LICENSE("GPL");
