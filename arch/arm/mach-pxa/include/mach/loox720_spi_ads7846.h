
/*--------------------------------------------------------------------------*/
/* The ADS7846 has touchscreen and other sensors.
 * Earlier ads784x chips are somewhat compatible.
 */
#define	ADS_START		(1 << 7)
#define	ADS_A2A1A0_d_y		(1 << 4)	/* differential */
#define	ADS_A2A1A0_d_z1		(3 << 4)	/* differential */
#define	ADS_A2A1A0_d_z2		(4 << 4)	/* differential */
#define	ADS_A2A1A0_d_x		(5 << 4)	/* differential */
#define	ADS_A2A1A0_temp0	(0 << 4)	/* non-differential */
#define	ADS_A2A1A0_vbatt	(2 << 4)	/* non-differential */
#define	ADS_A2A1A0_vaux		(6 << 4)	/* non-differential */
#define	ADS_A2A1A0_temp1	(7 << 4)	/* non-differential */
#define	ADS_8_BIT		(1 << 3)
#define	ADS_12_BIT		(0 << 3)
#define	ADS_SER			(1 << 2)	/* non-differential */
#define	ADS_DFR			(0 << 2)	/* differential */
#define	ADS_PD10_PDOWN		(0 << 0)	/* lowpower mode + penirq */
#define	ADS_PD10_ADC_ON		(1 << 0)	/* ADC on */
#define	ADS_PD10_REF_ON		(2 << 0)	/* vREF on + penirq */
#define	ADS_PD10_ALL_ON		(3 << 0)	/* ADC + vREF on */

#define	MAX_12BIT	((1<<12)-1)

/* leave ADC powered up (disables penirq) between differential samples */
#define	READ_12BIT_DFR(x, adc, vref) (ADS_START | ADS_A2A1A0_d_ ## x \
	| ADS_12_BIT | ADS_DFR | \
	(adc ? ADS_PD10_ADC_ON : 0) | (vref ? ADS_PD10_REF_ON : 0))

#define	READ_X(vref)	(READ_12BIT_DFR(x,  1, vref))
#define	READ_Y(vref)	(READ_12BIT_DFR(y,  1, vref))
#define	READ_Z1(vref)	(READ_12BIT_DFR(z1, 1, vref))
#define	READ_Z2(vref)	(READ_12BIT_DFR(z2, 1, vref))

#define	PWRDOWN		(READ_12BIT_DFR(y,  0, 0))	/* LAST */

/* single-ended samples need to first power up reference voltage;
 * we leave both ADC and VREF powered
 */
#define	READ_12BIT_SER(x) (ADS_START | ADS_A2A1A0_ ## x \
	| ADS_12_BIT | ADS_SER | ADS_PD10_ADC_ON)

#define	REF_ON	(READ_12BIT_DFR(x, 1, 1))
#define	REF_OFF	(READ_12BIT_DFR(y, 0, 0))

#define	MAX_12BIT	((1<<12)-1)

#define convert_data_12( data, pos ) (((data)->receiveblock[ pos ] &0x3f)<<6)|(((data)->receiveblock[ pos + 1] &0xfc00)>>10)
#define convert_hwmon_data_12( data, pos ) (((data)->hwmon_receiveblock[ pos ] &0x3f)<<6)|(((data)->hwmon_receiveblock[ pos + 1] &0xfc00)>>10)

#define LOOX720_CPLD_VAUX_VBATT		0
#define LOOX720_CPLD_VAUX_IBATT		1
#define LOOX720_CPLD_VAUX_TBATT		3
#define LOOX720_CPLD_VAUX_UNK		7

typedef struct loox720_ads7846_spi_message
{
	unsigned int		cmd;
	unsigned int		data;
} loox720_ads7846_spi_message;

typedef struct loox720_ads7846_device_info {
	unsigned int 			irq,pen;
	volatile unsigned int 		x, y, rt;
	unsigned int			xmin, xmax, ymin, ymax;
	unsigned int 			sample_rate;
	unsigned int			pen_recheck_usecs;
	volatile int			busy;
//	volatile int			pendown;
	void * 				input;
	struct spi_device 		*spi;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	unsigned short 			sendblock[16];
	unsigned short			receiveblock[16];
	unsigned int			report_button;
	unsigned int			disabled;
	struct hrtimer			timer;
	struct work_struct		work;	
#if defined(CONFIG_HWMON) || defined(CONFIG_HWMON_MODULE)
	struct attribute_group		*attr_group;
	struct device			*hwmon;
	u16				vref_mv;
	struct spi_transfer		hwmon_xfer;
	struct spi_message		hwmon_msg;
	unsigned short 			hwmon_sendblock[8];
	unsigned short			hwmon_receiveblock[8];
#endif
} loox720_ads7846_device_info;

static inline unsigned int readpen(loox720_ads7846_device_info *dev)	\
{										\
	if(dev)									\
		return (gpio_get_value(dev->pen )?1:0);			\
	printk( KERN_INFO "ADS7846 trying to read data with empty dev\n");	\
	return 0;								\
}
