/* Power Management support for FSC Loox 720
 *
 * Authors: Tomasz Figa
 *
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
#include <mach/pm.h>

//#include <linux/dpm.h>
//#include <asm/arch/pxa-pm_ll.h>

static u32 save[6];

static int loox720_suspend(struct platform_device *pdev, pm_message_t state)
{
	/*
	 * Note that WEP1 wake up event is used by bootldr to set the
	 * LEDS when power is applied/removed for charging.
	 */
	PWER = PWER_RTC | PWER_GPIO0 | PWER_GPIO1 | PWER_GPIO9 | PWER_GPIO11 | PWER_GPIO12 | PWER_GPIO13;
	PFER = PWER_GPIO0 | PWER_GPIO1 | PWER_GPIO9 | PWER_GPIO11 | PWER_GPIO13;                                // Falling Edge Detect
	PRER = PWER_GPIO0 | PWER_GPIO1 | PWER_GPIO9 | PWER_GPIO12 | PWER_GPIO13;                // Rising Edge Detect
	PKWR = PWER_GPIO0;

	PGSR0 = 0x01308018; // hx4700: 0x080DC01C
	PGSR1 = 0x008F0112; // hx4700: 0x34CF0002
	PGSR2 = 0x0C03C000; // hx4700: 0x0123C18C
	PGSR3 = 0x00000C80; // hx4700: 0x00100202

	/* These next checks are specifically for charging.  We want to enable
	* it if it is already enabled */
	/* Check for charge enable, GPIO 72 */
//      if(GPLR2 & (1 << 8)) {
	/* Set it */
//              PGSR2 |= (1U << 8);
//      } else {
	/* Clear it */
//              PGSR2 &= ~(1U << 8);
//      }
	/* Check for USB_CHARGE_RATE, GPIO 96 */
//      if(GPLR3 & (1 << 0)) {
	/* Set it */
//              PGSR3 |= (1U << 0);
//      } else {
	/* Clear it */
//              PGSR3 &= ~(1U << 0);
//      }

	PCFR = PCFR_GPROD|PCFR_GPR_EN|PCFR_OPDE|PCFR_PI2CEN; /* was 0x1091; */
	/* The 2<<2 below turns on the Power Island state preservation
	 * and counters.  This allows us to wake up bootldr after a
	 * period of time, and it can set the LEDs correctly based on
	 * the power state.  The bootldr turns it off when it's
	 * charged.
	z        */
	PSLR=0xcc400000 | (2 << 2);

	return 0;
}

static int loox720_resume(struct platform_device *pdev)
{
	loox720_cpld_resume();
	__asm__ volatile ("mrc p14, 0, r3, c6, c0, 0\n"
	                  "orr r3, r3, #0x0008\n"
	                  "mcr p14, 0, r3, c6, c0, 0\n"
                  :::"r3");

	return 0;
}

static void test_resume(void) __attribute__((naked));
static void test_resume(void)
{
	// our private resume handler restores the data at 0xA8024000
	// and then turns over control to the pxa_resume handler
	asm volatile("mov r0,#0xa8000000 \n");      // base memory
	asm volatile("orr r1,r0,#0x24000 \n");              // memory offset
	asm volatile("ldr r2,[r1,#20] \n");         // r2 is store
	asm volatile("ldr r3,[r1,#16] \n");         // r3 is our resume

	asm volatile("ldr r0,[r2],#4 \n");          // restore word at 0
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("ldr r0,[r2],#4 \n");          // restore a word at 4
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("ldr r0,[r2],#4 \n");          // restore a word at 8
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("ldr r0,[r2],#4 \n");          // restore a word at 12
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("ldr r0,[r2],#4 \n");          // restore a word at 16
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("ldr r0,[r2],#4 \n");          // restore a word at 20
	asm volatile("str r0,[r1],#4 \n");          // ..
	asm volatile("mov r0,#0x10000000 \n");      // CPLD address
	asm volatile("mov r1,#0xa800 \n");          // blue led
	asm volatile("orr r1,r1,#0x0C \n");         // ..
	asm volatile("strh r1,[r0,#16] \n");        // set LEDs

	asm volatile("mov r15,r3 \n");
	asm volatile("mov r0,r0 \n");
	asm volatile("mov r0,r0 ");
}

static void
loox720_pxa_ll_pm_suspend(unsigned long resume_addr)
{
//    register unsigned int R10 asm("r10");
	int i;
	u32 csum, tmp;
	u32 * p;

	tmp =  virt_to_phys(save);
	if ((tmp>=0xa8024000)&&(tmp<=0xa824036))
		panic("Resume storage should not be overwritten");

	tmp = virt_to_phys(test_resume);

	if ((tmp>=0xa8024000)&&(tmp<=0xa824036))
		panic("Resume handler should not be overwritten");
	/* Make sure that bootloader will not found reset pattern
	 * by setting special memory places to 0x00 */

	p = phys_to_virt(0xA8025304);
	if (!p)
		panic("Tried to put sleep informations into unmapped memory!");
	if (*p == 0x1A2B3C4D)
		*p = 0;

	p = phys_to_virt(0xA8025314);
	if (!p)
		panic("Tried to put sleep informations into unmapped memory!");
	if (*p == 0x1A2B3C4D)
		*p = 0;

	/* Save the first 4 words from 0xa8024000. */
	p = phys_to_virt(0xA8024000);
	if (!p)
		panic("Tried to put sleep informations into unmapped memory!");
	for (i = 0; i < 6; i++)
		save[i] = p[i];

	/* Set the first four words at 0xa8024000 to:
	 * resume address; MMU control; TLB base addr; domain id */
	p[0] = tmp;//(unsigned int) test_resume;//resume_addr;

asm( "mrc\tp15, 0, %0, c1, c0, 0" : "=r" (tmp) );
	p[1] = tmp & (~0xFFFF3987);         /* mmu off */

asm( "mrc\tp15, 0, %0, c2, c0, 0" : "=r" (tmp) );
	p[2] = tmp & 0xFFFFC000;        /* Shouldn't matter, since MMU will be off. */

asm( "mrc\tp15, 0, %0, c3, c0, 0" : "=r" (tmp) );
	p[3] = tmp;     /* Shouldn't matter, since MMU will be off. */
	p[4] = resume_addr;
	p[5] = virt_to_phys(save);

	/* Set PSPR to the checksum the HTC bootloader wants to see. */
	//pr_debug("loox720_pxa_ll_pm_resume: Saving system state...\n");
	for (csum = 0, i = 0; i < 54; i++) {
		tmp = p[i] & 0x1;
		tmp = tmp << 31;
		tmp |= tmp >> 1;
		csum += tmp;
		//      pr_debug("offset: %2x - value: %8x - checksum: %8x", i*4, p[i], csum);
	}

	PSPR = csum;
/* clear the Fastbus bit as for some reason letting it set leads to crash on resume */
__asm__ volatile ("mrc p14, 0, r3, c6, c0, 0\n"
                  "bic	r3, r3, #8\n"
                  "mcr p14, 0, r3, c6, c0, 0\n"
                 :::"r3");
}

extern void pxa27x_cpu_pm_restore(unsigned long *sleep_save);
void loox720_cpu_pm_restore(unsigned long *sleep_save)
{
	int i;
	u32 *p;

	/* Restore the data at 0xa8024000. */
	for (p = phys_to_virt(0xa8024000), i = 0; i < 5; i++)
	      p[i] = save[i];
	
	pxa27x_cpu_pm_restore(sleep_save);

	/* XXX Do we need to flush the cache? */
	/*
	__asm__ volatile ("mrc p14, 0, r3, c6, c0, 0\n"
	        "orr r3, r3, #0x0008\n"
	        "mcr p14, 0, r3, c6, c0, 0\n"
	        :::"r3");
	        */

}

void loox720_cpu_pm_enter(suspend_state_t state)
{
	extern void pxa_cpu_standby(void);

	/* ensure voltage-change sequencer not initiated, which hangs */
	PCFR &= ~PCFR_FVC;

	/* Clear edge-detect status register. */
	PEDR = 0xDF12FE1B;

	/* Clear reset status */
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		pxa_cpu_standby();
		break;
	case PM_SUSPEND_MEM:
		/* set resume return address */
		loox720_pxa_ll_pm_suspend(virt_to_phys(pxa_cpu_resume));
		pxa27x_cpu_suspend(PWRMODE_SLEEP);
		break;
	}
}

static int loox720_cpu_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM || state == PM_SUSPEND_STANDBY;
}

extern void pxa27x_cpu_pm_save(unsigned long *sleep_save);

enum {	SLEEP_SAVE_PGSR0, SLEEP_SAVE_PGSR1, SLEEP_SAVE_PGSR2, SLEEP_SAVE_PGSR3,

	SLEEP_SAVE_GAFR0_L, SLEEP_SAVE_GAFR0_U,
	SLEEP_SAVE_GAFR1_L, SLEEP_SAVE_GAFR1_U,
	SLEEP_SAVE_GAFR2_L, SLEEP_SAVE_GAFR2_U,
	SLEEP_SAVE_GAFR3_L, SLEEP_SAVE_GAFR3_U,

	SLEEP_SAVE_PSTR,

	SLEEP_SAVE_CKEN,

	SLEEP_SAVE_MDREFR,
	SLEEP_SAVE_PWER, SLEEP_SAVE_PCFR, SLEEP_SAVE_PRER,
	SLEEP_SAVE_PFER, SLEEP_SAVE_PKWR,

	SLEEP_SAVE_COUNT
};

static struct pxa_cpu_pm_fns loox720_cpu_pm_fns = {
	.save_count	= SLEEP_SAVE_COUNT,
	.save		= pxa27x_cpu_pm_save,
	.restore	= loox720_cpu_pm_restore,
	.valid		= loox720_cpu_pm_valid,
	.enter		= loox720_cpu_pm_enter,
};

static int
loox720_pm_probe( struct platform_device *pdev )
{
	pxa_cpu_pm_fns = &loox720_cpu_pm_fns;
	return 0;
}

static int
loox720_pm_remove( struct platform_device *dev )
{
	return 0;
}

struct platform_driver loox720_pm_driver = {
	.driver = {
		.name     = "loox720-pm",
	},
	.probe    = loox720_pm_probe,
	.remove   = loox720_pm_remove,
	.suspend  = loox720_suspend,
	.resume   = loox720_resume,
};

static int __init
loox720_pm_init( void )
{
	return platform_driver_register( &loox720_pm_driver );
}


static void __exit
loox720_pm_exit( void )
{
	platform_driver_unregister( &loox720_pm_driver );
}

module_init( loox720_pm_init );
module_exit( loox720_pm_exit );

MODULE_AUTHOR("Tomasz Figa");
MODULE_DESCRIPTION("Loox 720 Power Management Support");
MODULE_LICENSE("GPL");

