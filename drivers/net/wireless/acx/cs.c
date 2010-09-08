#include <linux/version.h>
#include <linux/utsrelease.h>
//#include <linux/compiler.h> /* required for Lx 2.6.8 ?? */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/if_arp.h>
#include <linux/irq.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/inetdevice.h>


#define PCMCIA_DEBUG 16

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
	
*/
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "acx.h"
#include "acx_hw.h"
#include "acx_generic.h"

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
//static char *version = "$Revision: 1.10 $";
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args);
#else
#define DEBUG(n, args...)
#endif

static win_req_t memwin;

struct cs_drv_data {
    struct net_device	*ndev;		/* pointer to linux netdevice */

	struct semaphore	sem;
        spinlock_t              txbuf_lock;

	unsigned int	tx_tail;
	u8		*txbuf_start;
	txhostdesc_t	*txhostdesc_start;
	txdesc_t	*txdesc_start;	/* points to PCI-mapped memory */
	dma_addr_t	txbuf_startphy;
	dma_addr_t	txhostdesc_startphy;
	/* sizes of above host memory areas */
	unsigned int	txbuf_area_size;
	unsigned int	txhostdesc_area_size;

	unsigned int	txdesc_size;	/* size of txdesc; ACX111 = ACX100 + 4 */
	client_t	*txc[TX_CNT];
	u16		txr[TX_CNT];

	/* same for rx */
	unsigned int	rx_tail;
	rxbuffer_t	*rxbuf_start;
	rxhostdesc_t	*rxhostdesc_start;
	rxdesc_t	*rxdesc_start;
	/* physical addresses of above host memory areas */
	dma_addr_t	rxbuf_startphy;
	/* dma_addr_t	rxhostdesc_startphy; */
	unsigned int	rxbuf_area_size;
	unsigned int	rxhostdesc_area_size;

	u8		irqs_active;	/* whether irq sending is activated */

	const u16	*io;		/* points to ACX100 or ACX111 PCI I/O register address set */
//	struct device	*dev;
	volatile u32	*membase;
	volatile u32	*iobase;
	void __iomem	*iobase2;
	/* command interface */
	u8 __iomem	*cmd_area;
	u8 __iomem	*info_area;

	u16		irq_mask;		/* interrupt types to mask out (not wanted) with many IRQs activated */
	u16		irq_mask_off;		/* interrupt types to mask out (not wanted) with IRQs off */
	unsigned int	irq_loops_this_jiffy;
	unsigned long	irq_last_jiffies;

        u32 acx_txbuf_start;
        int acx_txbuf_numblocks;
        u32 acx_txbuf_free;                    /* addr of head of free list          */
        int acx_txbuf_blocks_free;             /* how many are still open            */
        queueindicator_t *acx_queue_indicator;
	
	struct pcmcia_device *pdev;

} typedef cs_drv_data_t;

#define DRV_DATA(adev) ((cs_drv_data_t *)adev->drv_data)

#define CARD_EEPROM_ID_SIZE 6

#include <asm/io.h>

#define REG_ACX_VENDOR_ID 0x900
/*
 * This is the vendor id on the HX4700, anyway
 */
#define ACX_VENDOR_ID 0x8400104c

typedef enum {
	ACX_SOFT_RESET = 0,

	ACX_SLV_REG_ADDR,
	ACX_SLV_REG_DATA,
	ACX_SLV_REG_ADATA,

	ACX_SLV_MEM_CP,
	ACX_SLV_MEM_ADDR,
	ACX_SLV_MEM_DATA,
	ACX_SLV_MEM_CTL,
} acxreg_t;

int acx_init_netdev(acx_device_t *adev, int base_addr, int addr_size, int irq);
void acx_cs_release(struct pcmcia_device *link);
int acxcs_read_eeprom_byte(acx_device_t * adev, u32 addr, u8 * charbuf);

#define INLINE_IO static inline

INLINE_IO u32
read_id_register (acx_device_t *adev)
{
  writel (0x24, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR]);
  return readl (&DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA]);
}

INLINE_IO u32
read_reg32(acx_device_t *adev, unsigned int offset)
{
        u32 val;
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20) {
	  if (addr&3)
	  {
		// fix alignment..
		u32 ret = readb(((u8*)DRV_DATA(adev)->iobase) + addr);
		ret |= (readb(((u8*)DRV_DATA(adev)->iobase) + addr +1) <<8 );
		ret |= (readb(((u8*)DRV_DATA(adev)->iobase) + addr +2) <<16 );
		ret |= (readb(((u8*)DRV_DATA(adev)->iobase) + addr +3) <<24 );
		return ret;
	  }
	  else
	  return readl(((u8*)DRV_DATA(adev)->iobase) + addr);
	}

	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	val = readl( &DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );

	return val;
}

INLINE_IO u16
read_reg16(acx_device_t *adev, unsigned int offset)
{
	u16 lo;
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20) {
	    if (addr&1)
	    {
		u16 ret = readb(((u8*)DRV_DATA(adev)->iobase) + addr);
		ret |= (readb(((u8*)DRV_DATA(adev)->iobase) + addr +1) <<8 );
		return ret;
	    } else
	    return readw(((u8 *) DRV_DATA(adev)->iobase) + addr);
	}

	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	lo = readw( (u16 *)&DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );

	return lo;
}

INLINE_IO u8
read_reg8(acx_device_t *adev, unsigned int offset)
{
	u8 lo;
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20)
	    return readb(((u8 *)DRV_DATA(adev)->iobase) + addr);

	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	lo = readw( (u8 *)&DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );

	return (u8)lo;
}

INLINE_IO void
write_reg32(acx_device_t *adev, unsigned int offset, u32 val)
{
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20) {
	    if (addr&3)
	    {
		writeb(val,((u8*)DRV_DATA(adev)->iobase) + addr);
		writeb(val>>8,((u8*)DRV_DATA(adev)->iobase) + addr+1);
		writeb(val>>16,((u8*)DRV_DATA(adev)->iobase) + addr+2);
		writeb(val>>24,((u8*)DRV_DATA(adev)->iobase) + addr+3);
	    } else
	    writel(val, ((u8*)DRV_DATA(adev)->iobase) + addr);
	    return;
	}

	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	writel( val, &DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );
}

INLINE_IO void
write_reg16(acx_device_t *adev, unsigned int offset, u16 val)
{
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20) {
	    if (addr&1)
	    {
		writeb(val, ((u8 *)DRV_DATA(adev)->iobase) + addr);
		writeb(val>>8, ((u8 *)DRV_DATA(adev)->iobase) + addr+1);
	    } else
	    writew(val, ((u8 *)DRV_DATA(adev)->iobase) + addr);
	    return;
	}
	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	writew( val, (u16 *) &DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );
}

INLINE_IO void
write_reg8(acx_device_t *adev, unsigned int offset, u8 val)
{
	u32 addr;

        if (offset > IO_ACX_ECPU_CTRL)
	  addr = offset;
	else
	  addr = DRV_DATA(adev)->io[offset];

	if (addr < 0x20) {
	    writeb(val, ((u8 *) DRV_DATA(adev)->iobase) + addr);
	    return;
	}
	writel( addr, &DRV_DATA(adev)->iobase[ACX_SLV_REG_ADDR] );
	writeb( val, (u8 *)&DRV_DATA(adev)->iobase[ACX_SLV_REG_DATA] );
}

/* Handle PCI posting properly:
 * Make sure that writes reach the adapter in case they require to be executed
 * *before* the next write, by reading a random (and safely accessible) register.
 * This call has to be made if there is no read following (which would flush the data
 * to the adapter), yet the written data has to reach the adapter immediately. */
INLINE_IO void
write_flush(acx_device_t *adev)
{
	/* readb(DRV_DATA(adev)->iobase + DRV_DATA(adev)->io[IO_ACX_INFO_MAILBOX_OFFS]); */
	/* faster version (accesses the first register, IO_ACX_SOFT_RESET,
	 * which should also be safe): */
	(void) readl(DRV_DATA(adev)->iobase);
}

INLINE_IO void
set_regbits (acx_device_t *adev, unsigned int offset, u32 bits) {
  u32 tmp;

  tmp = read_reg32 (adev, offset);
  tmp = tmp | bits;
  write_reg32 (adev, offset, tmp);
  write_flush (adev);
}

INLINE_IO void
clear_regbits (acx_device_t *adev, unsigned int offset, u32 bits) {
  u32 tmp;

  tmp = read_reg32 (adev, offset);
  tmp = tmp & ~bits;
  write_reg32 (adev, offset, tmp);
  write_flush (adev);
}

/*
 * Copy from PXA memory to the ACX memory.  This assumes both the PXA and ACX
 * addresses are 32 bit aligned.  Count is in bytes.
 */
INLINE_IO void
write_slavemem32 (acx_device_t *adev, u32 slave_address, u32 val)
{
  write_reg32 (adev, IO_ACX_SLV_MEM_CTL, 0x0);
  write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, slave_address);
  udelay (10);
  write_reg32 (adev, IO_ACX_SLV_MEM_DATA, val);
}

INLINE_IO u32
read_slavemem32 (acx_device_t *adev, u32 slave_address)
{
  u32 val;

  write_reg32 (adev, IO_ACX_SLV_MEM_CTL, 0x0);
  write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, slave_address);
  udelay (10);
  val = read_reg32 (adev, IO_ACX_SLV_MEM_DATA);

  return val;
}

INLINE_IO void
write_slavemem8 (acx_device_t *adev, u32 slave_address, u8 val)
{
  u32 data;
  u32 base;
  int offset;

  /*
   * Get the word containing the target address and the byte offset in that word.
   */
  base = slave_address & ~3;
  offset = (slave_address & 3) * 8;

  data = read_slavemem32 (adev, base);
  data &= ~(0xff << offset);
  data |= val << offset;
  write_slavemem32 (adev, base, data);
}

INLINE_IO u8
read_slavemem8 (acx_device_t *adev, u32 slave_address)
{
  u8 val;
  u32 base;
  u32 data;
  int offset;

  base = slave_address & ~3;
  offset = (slave_address & 3) * 8;

  data = read_slavemem32 (adev, base);
 
  val = (data >> offset) & 0xff;

  return val;
}

/*
 * doesn't split across word boundaries
 */
INLINE_IO void
write_slavemem16 (acx_device_t *adev, u32 slave_address, u16 val)
{
  u32 data;
  u32 base;
  int offset;

  /*
   * Get the word containing the target address and the byte offset in that word.
   */
  base = slave_address & ~3;
  offset = (slave_address & 3) * 8;

  data = read_slavemem32 (adev, base);
  data &= ~(0xffff << offset);
  data |= val << offset;
  write_slavemem32 (adev, base, data);
}

/*
 * doesn't split across word boundaries
 */
INLINE_IO u16
read_slavemem16 (acx_device_t *adev, u32 slave_address)
{
  u16 val;
  u32 base;
  u32 data;
  int offset;

  base = slave_address & ~3;
  offset = (slave_address & 3) * 8;

  data = read_slavemem32 (adev, base);
 
  val = (data >> offset) & 0xffff;

  return val;
}

/*
 * Copy from slave memory
 *
 * TODO - rewrite using address autoincrement, handle partial words
 */
void
copy_from_slavemem (acx_device_t *adev, u8 *destination, u32 source, int count) {
  u32 tmp = 0;
  u8 *ptmp = (u8 *) &tmp;

  /*
   * Right now I'm making the assumption that the destination is aligned, but
   * I'd better check.
   */
  if ((u32) destination & 3) {
    printk ("acx copy_from_slavemem: warning!  destination not word-aligned!\n");
    
    while (count >= 4) {
	u32 inword;
        write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, source);
        udelay (10);
	inword = read_reg32 (adev, IO_ACX_SLV_MEM_DATA);
        destination[0] = inword;
        destination[1] = inword>>8;
        destination[2] = inword>>16;
        destination[3] = inword>>24;
	 
        count -= 4;
        source += 4;
        destination += 4;
    }
    
    
    
  } else 

  while (count >= 4) {
    write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, source);
    udelay (10);
    *((u32 *) destination) = read_reg32 (adev, IO_ACX_SLV_MEM_DATA);
    count -= 4;
    source += 4;
    destination += 4;
  }

  /*
   * If the word reads above didn't satisfy the count, read one more word
   * and transfer a byte at a time until the request is satisfied.
   */
  if (count) {
    write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, source);
    udelay (10);
    tmp = read_reg32 (adev, IO_ACX_SLV_MEM_DATA);
    while (count--) {
      *destination++ = *ptmp++;
    }
  }
}

void
chaincopy_from_slavemem (acx_device_t *adev, u8 *destination, u32 source, int count)
{
  u32 val;
  u32 *data = (u32 *) destination;
  static u8 aligned_destination[WLAN_A4FR_MAXLEN_WEP_FCS];
  int saved_count = count;

  /*
   * Warn if the pointers don't look right.  Destination must fit in [23:5] with
   * zero elsewhere and source should be 32 bit aligned.
   * Turns out the network stack sends unaligned things, so fix them before
   * copying to the ACX.
   */
  if ((source & 0x00ffffe0) != source) {
    printk ("acx chaincopy: source block 0x%04x not aligned!\n", source);
//    dump_acxmem (adev, 0, 0x10000);
  }
  if ((u32) destination & 3) {
    //printk ("acx chaincopy: data destination not word aligned!\n");
    data = (u32 *) aligned_destination;
    if (count > sizeof aligned_destination) {
	printk( KERN_ERR "chaincopy_from_slavemem overflow!\n" );
	count = sizeof aligned_destination;
    }
  }

  /*
   * SLV_MEM_CTL[17:16] = memory block chain mode with auto-increment
   * SLV_MEM_CTL[5:2] = offset to data portion = 1 word
   */
  val = (2 << 16) | (1 << 2);
  writel (val, &DRV_DATA(adev)->iobase[ACX_SLV_MEM_CTL]);

  /*
   * SLV_MEM_CP[23:5] = start of 1st block
   * SLV_MEM_CP[3:2] = offset to memblkptr = 0
   */
  val = source & 0x00ffffe0;
  writel (val, &DRV_DATA(adev)->iobase[ACX_SLV_MEM_CP]);

  /*
   * SLV_MEM_ADDR[23:2] = SLV_MEM_CTL[5:2] + SLV_MEM_CP[23:5]
   */
  val = (source & 0x00ffffe0) + (1<<2);
  writel (val, &DRV_DATA(adev)->iobase[ACX_SLV_MEM_ADDR]);

  /*
   * Read the data from the slave data register, rounding up to the end
   * of the word containing the last byte (hence the > 0)
   */
  while (count > 0) {
    *data++ = readl (&DRV_DATA(adev)->iobase[ACX_SLV_MEM_DATA]);
    count -= 4;
  }

  /*
   * If the destination wasn't aligned, we would have saved it in
   * the aligned buffer, so copy it where it should go.
   */
  if ((u32) destination & 3) {
    memcpy (destination, aligned_destination, saved_count);
  }
}

/*
 * Copy to slave memory
 *
 * TODO - rewrite using autoincrement, handle partial words
 */
void
copy_to_slavemem (acx_device_t *adev, u32 destination, u8 *source, int count)
{
  u32 tmp = 0;
  u8* ptmp = (u8 *) &tmp;
  static u8 src[512];	/* make static to avoid huge stack objects */

  /*
   * For now, make sure the source is word-aligned by copying it to a word-aligned
   * buffer.  Someday rewrite to avoid the extra copy.
   */
  if (count > sizeof (src)) {
    printk ("acx copy_to_slavemem: Warning! buffer overflow!\n");
    count = sizeof (src);
  }
  memcpy (src, source, count);
  source = src;

  while (count >= 4) {
    write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, destination);
    udelay (10);
    write_reg32 (adev, IO_ACX_SLV_MEM_DATA, *((u32 *) source));
    count -= 4;
    source += 4;
    destination += 4;
  }

  /*
   * If there are leftovers read the next word from the acx and merge in
   * what they want to write.
   */
  if (count) {
    write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, destination);
    udelay (10);
    tmp = read_reg32 (adev, IO_ACX_SLV_MEM_DATA);
    while (count--) {
      *ptmp++ = *source++;
    }
    /*
     * reset address in case we're currently in auto-increment mode
     */
    write_reg32 (adev, IO_ACX_SLV_MEM_ADDR, destination);
    udelay (10);
    write_reg32 (adev, IO_ACX_SLV_MEM_DATA, tmp);
    udelay (10);
  }
  
}

static int
acxmem_complete_hw_reset (acx_device_t *adev)
{
    printk(KERN_DEBUG "acx: complete_hw_reset - NOT IMPLEMENTED");
    return -1;
};

static inline txdesc_t*
get_txdesc(acx_device_t *adev, int index)
{
printk("acx: get_txdesc: txdesc_start=%X, index=%d, txdesc_size=%d\n", (unsigned int)DRV_DATA(adev)->txdesc_start, index, DRV_DATA(adev)->txdesc_size);
	return (txdesc_t*) (((u8*)DRV_DATA(adev)->txdesc_start) + index * DRV_DATA(adev)->txdesc_size);
}

static inline txdesc_t*
advance_txdesc(acx_device_t *adev, txdesc_t* txdesc, int inc)
{
	return (txdesc_t*) (((u8*)txdesc) + inc * DRV_DATA(adev)->txdesc_size);
}

static txhostdesc_t*
get_txhostdesc(acx_device_t *adev, txdesc_t* txdesc)
{
	int index = (u8*)txdesc - (u8*)DRV_DATA(adev)->txdesc_start;
	if (unlikely(ACX_DEBUG && (index % DRV_DATA(adev)->txdesc_size))) {
		printk("bad txdesc ptr %p\n", txdesc);
		return NULL;
	}
	index /= DRV_DATA(adev)->txdesc_size;
	if (unlikely(ACX_DEBUG && (index >= TX_CNT))) {
		printk("bad txdesc ptr %p\n", txdesc);
		return NULL;
	}
	return &DRV_DATA(adev)->txhostdesc_start[index*2];
}

static void handle_tx_error(acx_device_t * adev, u8 error, unsigned int finger,
		struct ieee80211_tx_status *status)
{
	const char *err = "unknown error";

	/* hmm, should we handle this as a mask
	 * of *several* bits?
	 * For now I think only caring about
	 * individual bits is ok... */
	switch (error) {
	case 0x01:
		err = "no Tx due to error in other fragment";
/*		adev->wstats.discard.fragment++; */
		break;
	case 0x02:
		err = "Tx aborted";
		adev->stats.tx_aborted_errors++;
		break;
	case 0x04:
		err = "Tx desc wrong parameters";
/*		adev->wstats.discard.misc++; */
		break;
	case 0x08:
		err = "WEP key not found";
/*		adev->wstats.discard.misc++; */
		break;
	case 0x10:
		err = "MSDU lifetime timeout? - try changing "
		    "'iwconfig retry lifetime XXX'";
/*		adev->wstats.discard.misc++; */
		break;
	case 0x20:
		err = "excessive Tx retries due to either distance "
		    "too high or unable to Tx or Tx frame error - "
		    "try changing 'iwconfig txpower XXX' or "
		    "'sens'itivity or 'retry'";
/*		adev->wstats.discard.retries++; */
		/* Tx error 0x20 also seems to occur on
		 * overheating, so I'm not sure whether we
		 * actually want to do aggressive radio recalibration,
		 * since people maybe won't notice then that their hardware
		 * is slowly getting cooked...
		 * Or is it still a safe long distance from utter
		 * radio non-functionality despite many radio recalibs
		 * to final destructive overheating of the hardware?
		 * In this case we really should do recalib here...
		 * I guess the only way to find out is to do a
		 * potentially fatal self-experiment :-\
		 * Or maybe only recalib in case we're using Tx
		 * rate auto (on errors switching to lower speed
		 * --> less heat?) or 802.11 power save mode?
		 *
		 * ok, just do it. */
		if (++adev->retry_errors_msg_ratelimit % 4 == 0) {
			if (adev->retry_errors_msg_ratelimit <= 20) {
				printk("%s: several excessive Tx "
				       "retry errors occurred, attempting "
				       "to recalibrate radio. Radio "
				       "drift might be caused by increasing "
				       "card temperature, please check the card "
				       "before it's too late!\n",
				       wiphy_name(adev->ieee->wiphy));
				if (adev->retry_errors_msg_ratelimit == 20)
					printk("disabling above message\n");
			}

			acx_schedule_task(adev,
					  ACX_AFTER_IRQ_CMD_RADIO_RECALIB);
		}
		status->excessive_retries++;
		break;
	case 0x40:
		err = "Tx buffer overflow";
		adev->stats.tx_fifo_errors++;
		break;
	case 0x80:
		/* possibly ACPI C-state powersaving related!!!
		 * (DMA timeout due to excessively high wakeup
		 * latency after C-state activation!?)
		 * Disable C-State powersaving and try again,
		 * then PLEASE REPORT, I'm VERY interested in
		 * whether my theory is correct that this is
		 * actually the problem here.
		 * In that case, use new Linux idle wakeup latency
		 * requirements kernel API to prevent this issue. */
		err = "DMA error";
/*		adev->wstats.discard.misc++; */
		break;
	}
	adev->stats.tx_errors++;
	if (adev->stats.tx_errors <= 20)
		printk("%s: tx error 0x%02X, buf %02u! (%s)\n",
		       wiphy_name(adev->ieee->wiphy), error, finger, err);
	else
		printk("%s: tx error 0x%02X, buf %02u!\n",
		       wiphy_name(adev->ieee->wiphy), error, finger);
}



static void
enable_acx_irq(acx_device_t *adev)
{
	FN_ENTER;
	write_reg16(adev, IO_ACX_IRQ_MASK, DRV_DATA(adev)->irq_mask);
	write_reg16(adev, IO_ACX_FEMR, 0x8000);
	DRV_DATA(adev)->irqs_active = 1;
	FN_EXIT0;
}

static void
disable_acx_irq(acx_device_t *adev)
{
	FN_ENTER;

	/* I guess mask is not 0xffff because acx100 won't signal
	** cmd completion then (needed for ifup).
	** Someone with acx100 please confirm */
	write_reg16(adev, IO_ACX_IRQ_MASK, DRV_DATA(adev)->irq_mask_off);
	write_reg16(adev, IO_ACX_FEMR, 0x0);
	DRV_DATA(adev)->irqs_active = 0;
	FN_EXIT0;
}

static void acxcs_s_down(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	unsigned long flags;

	FN_ENTER;

	/* Disable IRQs first, so that IRQs cannot race with us */
	/* then wait until interrupts have finished executing on other CPUs */
	printk("acxcs_s_down: acx_lock()\n");
	acx_s_mwait(1000);
	acx_lock(adev, flags);
	disable_acx_irq(adev);
	synchronize_irq(adev->pdev->irq);
	printk("acxcs_s_down: acx_unlock()\n");
	acx_s_mwait(1000);
	acx_unlock(adev, flags);

	/* we really don't want to have an asynchronous tasklet disturb us
	 ** after something vital for its job has been shut down, so
	 ** end all remaining work now.
	 **
	 ** NB: carrier_off (done by set_status below) would lead to
	 ** not yet fully understood deadlock in flush_scheduled_work().
	 ** That's why we do FLUSH first.
	 **
	 ** NB2: we have a bad locking bug here: flush_scheduled_work()
	 ** waits for acx_e_after_interrupt_task to complete if it is running
	 ** on another CPU, but acx_e_after_interrupt_task
	 ** will sleep on sem forever, because it is taken by us!
	 ** Work around that by temporary sem unlock.
	 ** This will fail miserably if we'll be hit by concurrent
	 ** iwconfig or something in between. TODO! */
	printk("acxcs_s_down: flush_scheduled_work()\n");
	acx_s_mwait(1000);
	flush_scheduled_work();

	/* This is possible:
	 ** flush_scheduled_work -> acx_e_after_interrupt_task ->
	 ** -> set_status(ASSOCIATED) -> wake_queue()
	 ** That's why we stop queue _after_ flush_scheduled_work
	 ** lock/unlock is just paranoia, maybe not needed */

	/* kernel/timer.c says it's illegal to del_timer_sync()
	 ** a timer which restarts itself. We guarantee this cannot
	 ** ever happen because acx_i_timer() never does this if
	 ** status is ACX_STATUS_0_STOPPED */
	printk("acxcs_s_down: del_timer_sync()\n");
	acx_s_mwait(1000);
	del_timer_sync(&adev->mgmt_timer);

	FN_EXIT0;
}

void
acxcs_free_desc_queues(acx_device_t *adev)
{
#define ACX_FREE_QUEUE(size, ptr, phyaddr) \
        if (ptr) { \
                kfree(ptr); \
                ptr = NULL; \
                size = 0; \
        }

	FN_ENTER;

	ACX_FREE_QUEUE(DRV_DATA(adev)->txhostdesc_area_size, DRV_DATA(adev)->txhostdesc_start, DRV_DATA(adev)->txhostdesc_startphy);
	ACX_FREE_QUEUE(DRV_DATA(adev)->txbuf_area_size, DRV_DATA(adev)->txbuf_start, DRV_DATA(adev)->txbuf_startphy);

	DRV_DATA(adev)->txdesc_start = NULL;

	ACX_FREE_QUEUE(DRV_DATA(adev)->rxhostdesc_area_size, DRV_DATA(adev)->rxhostdesc_start, adev->rxhostdesc_startphy);
	ACX_FREE_QUEUE(DRV_DATA(adev)->rxbuf_area_size, DRV_DATA(adev)->rxbuf_start, adev->rxbuf_startphy);

	DRV_DATA(adev)->rxdesc_start = NULL;

	FN_EXIT0;
}

/***********************************************************************
** acxmem_s_delete_dma_regions
*/
static void
acxcs_s_delete_dma_regions(acx_device_t *adev)
{
	unsigned long flags;

	FN_ENTER;
	/* disable radio Tx/Rx. Shouldn't we use the firmware commands
	 * here instead? Or are we that much down the road that it's no
	 * longer possible here? */
	/*
	 * slave memory interface really doesn't like this.
	 */
	/*
	write_reg16(adev, IO_ACX_ENABLE, 0);
	*/

	msleep(100);

	acx_lock(adev, flags);
	acxcs_free_desc_queues(adev);
	acx_unlock(adev, flags);

	FN_EXIT0;
}



unsigned int acxcs_l_clean_txdesc(acx_device_t * adev)
{
	txdesc_t *txdesc;
	txhostdesc_t *hostdesc;
	unsigned finger;
	int num_cleaned;
	u16 r111;
	u8 error, ack_failures, rts_failures, rts_ok, r100;

	FN_ENTER;

//	if (unlikely(acx_debug & L_DEBUG))
//!!!!		log_txbuffer(adev);

	log(L_BUFT, "tx: cleaning up bufs from %u\n", DRV_DATA(adev)->tx_tail);

	/* We know first descr which is not free yet. We advance it as far
	 ** as we see correct bits set in following descs (if next desc
	 ** is NOT free, we shouldn't advance at all). We know that in
	 ** front of tx_tail may be "holes" with isolated free descs.
	 ** We will catch up when all intermediate descs will be freed also */

	finger = DRV_DATA(adev)->tx_tail;
	num_cleaned = 0;
	while (likely(finger != adev->tx_head)) {
		txdesc = get_txdesc(adev, finger);

		/* If we allocated txdesc on tx path but then decided
		 ** to NOT use it, then it will be left as a free "bubble"
		 ** in the "allocated for tx" part of the ring.
		 ** We may meet it on the next ring pass here. */

		/* stop if not marked as "tx finished" and "host owned" */
		if ((txdesc->Ctl_8 & DESC_CTL_ACXDONE_HOSTOWN)
		    != DESC_CTL_ACXDONE_HOSTOWN) {
			if (unlikely(!num_cleaned)) {	/* maybe remove completely */
				log(L_BUFT, "clean_txdesc: tail isn't free. "
				    "tail:%d head:%d\n",
				    DRV_DATA(adev)->tx_tail, adev->tx_head);
			}
			break;
		}

		/* remember desc values... */
		error = txdesc->error;
		ack_failures = txdesc->ack_failures;
		rts_failures = txdesc->rts_failures;
		rts_ok = txdesc->rts_ok;
		r100 = txdesc->u.r1.rate;
		r111 = le16_to_cpu(txdesc->u.r2.rate111);

		/* need to check for certain error conditions before we
		 * clean the descriptor: we still need valid descr data here */
		hostdesc = get_txhostdesc(adev, txdesc);

		hostdesc->txstatus.flags |= IEEE80211_TX_STATUS_ACK;
		if (unlikely(0x30 & error)) {
			/* only send IWEVTXDROP in case of retry or lifetime exceeded;
			 * all other errors mean we screwed up locally */
/*			union iwreq_data wrqu;
			struct ieee80211_hdr_3addr *hdr;
			hdr = (struct ieee80211_hdr_3addr *) hostdesc->data;
			MAC_COPY(wrqu.addr.sa_data, hdr->addr1);
*/
			hostdesc->txstatus.flags &= ~IEEE80211_TX_STATUS_ACK;
		}

		/* ...and free the desc */
		txdesc->error = 0;
		txdesc->ack_failures = 0;
		txdesc->rts_failures = 0;
		txdesc->rts_ok = 0;
		/* signal host owning it LAST, since ACX already knows that this
		 ** descriptor is finished since it set Ctl_8 accordingly. */
		txdesc->Ctl_8 = DESC_CTL_HOSTOWN;

		adev->tx_free++;
		num_cleaned++;

		if ((adev->tx_free >= TX_START_QUEUE)
/*		    && (adev->status == ACX_STATUS_4_ASSOCIATED) */
		    /*&& (acx_queue_stopped(adev->ieee))*/
		    ) {
			log(L_BUF, "tx: wake queue (avail. Tx desc %u)\n",
			    adev->tx_free);
			acx_wake_queue(adev->ieee, NULL);
		}

		/* do error checking, rate handling and logging
		 * AFTER having done the work, it's faster */

		/* Rate handling is done in mac80211 */
/*		if (adev->rate_auto) {
			struct client *clt = get_txc(adev, txdesc);
			if (clt) {
				u16 cur = get_txr(adev, txdesc);
				if (clt->rate_cur == cur) {
					acx_l_handle_txrate_auto(adev, clt, cur,*/	/* intended rate */
								 /*r100, r111,*/	/* actually used rate */
								 /*(error & 0x30),*/	/* was there an error? */
/*								 TX_CNT +
								 TX_CLEAN_BACKLOG
								 -
								 adev->tx_free);
				}
			}
		}
*/
		if (unlikely(error))
			handle_tx_error(adev, error, finger,  &hostdesc->txstatus);

		if (IS_ACX111(adev))
			log(L_BUFT,
			    "tx: cleaned %u: !ACK=%u !RTS=%u RTS=%u r111=%04X tx_free=%u\n",
			    finger, ack_failures, rts_failures, rts_ok, r111, adev->tx_free);
		else
			log(L_BUFT,
			    "tx: cleaned %u: !ACK=%u !RTS=%u RTS=%u rate=%u\n",
			    finger, ack_failures, rts_failures, rts_ok, r100);

		/* And finally report upstream */
		if (hostdesc)
		{
			hostdesc->txstatus.excessive_retries = rts_failures ;
			hostdesc->txstatus.retry_count = ack_failures;
			ieee80211_tx_status(adev->ieee,hostdesc->skb,&hostdesc->txstatus);
			memset(&hostdesc->txstatus, 0, sizeof(struct ieee80211_tx_status));
		}
		/* update pointer for descr to be cleaned next */
		finger = (finger + 1) % TX_CNT;
	}
	/* remember last position */
	DRV_DATA(adev)->tx_tail = finger;
/* end: */
	FN_EXIT1(num_cleaned);
	return num_cleaned;
}

/* clean *all* Tx descriptors, and regardless of their previous state.
 * Used for brute-force reset handling. */
void acxcs_l_clean_txdesc_emergency(acx_device_t * adev)
{
	txdesc_t *txdesc;
	int i;

	FN_ENTER;

	for (i = 0; i < TX_CNT; i++) {
		txdesc = get_txdesc(adev, i);

		/* free it */
		txdesc->ack_failures = 0;
		txdesc->rts_failures = 0;
		txdesc->rts_ok = 0;
		txdesc->error = 0;
		txdesc->Ctl_8 = DESC_CTL_HOSTOWN;
	}

	adev->tx_free = TX_CNT;

	FN_EXIT0;
}

static irqreturn_t acxcs_i_interrupt(int irq, void *dev_id)
{

	acx_device_t *adev = dev_id;
	unsigned long flags;
	register u16 irqtype;
	u16 unmasked;
	
	printk("acx: INTERRUPT (adev=%X)\n", adev);

	if (!adev)
		return IRQ_NONE;
	/* LOCKING: can just spin_lock() since IRQs are disabled anyway.
	 * I am paranoid */

	acx_lock(adev, flags);

	unmasked = read_reg16(adev, IO_ACX_IRQ_STATUS_CLEAR);

	if (unlikely(0xffff == unmasked)) {
		/* 0xffff value hints at missing hardware,
		 * so don't do anything.
		 * Not very clean, but other drivers do the same... */
		log(L_IRQ, "IRQ type:FFFF - device removed? IRQ_NONE\n");
printk("IRQ type:FFFF - device removed? IRQ_NONE\n");
		goto none;
	}

	/* We will check only "interesting" IRQ types */
	irqtype = unmasked & ~DRV_DATA(adev)->irq_mask;
	if (!irqtype) {
		/* We are on a shared IRQ line and it wasn't our IRQ */
		log(L_IRQ,
		    "IRQ type:%04X, mask:%04X - all are masked, IRQ_NONE\n",
		    unmasked, DRV_DATA(adev)->irq_mask);
printk("IRQ type:%04X, mask:%04X - all are masked, IRQ_NONE\n",
		    unmasked, DRV_DATA(adev)->irq_mask);
		goto none;
	}

printk("IRQ type:%04X, mask:%04X\n",
		    unmasked, DRV_DATA(adev)->irq_mask);

	/* Go ahead and ACK our interrupt */
	write_reg16(adev, IO_ACX_IRQ_ACK, 0xffff);
	if (irqtype & HOST_INT_CMD_COMPLETE) {
		log(L_IRQ, "got Command_Complete IRQ\n");
printk("got Command_Complete IRQ\n");
		/* save the state for the running issue_cmd() */
		SET_BIT(adev->irq_status, HOST_INT_CMD_COMPLETE);
	}

	/* Only accept IRQs, if we are initialized properly.
	 * This avoids an RX race while initializing.
	 * We should probably not enable IRQs before we are initialized
	 * completely, but some careful work is needed to fix this. I think it
	 * is best to stay with this cheap workaround for now... .
	 */
	if (likely(adev->initialized)) {
		/* disable all IRQs. They are enabled again in the bottom half. */
		/* save the reason code and call our bottom half. */
		adev->irq_reason = irqtype;
		if ((irqtype & HOST_INT_RX_DATA) || (irqtype & HOST_INT_TX_COMPLETE))
		{
printk("scheduling task... (irq_reason:%04X)\n", adev->irq_reason);
			acx_schedule_task(adev, 0);
		}
		else
		  printk("not scheduling task...\n");
	}
	else
printk("acx: adev->initialied = false!!!\n");

	acx_unlock(adev, flags);
	return IRQ_HANDLED;
      none:
	acx_unlock(adev, flags);
	return IRQ_NONE;

}

static void acxcs_s_up(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	unsigned long flags;

	FN_ENTER;

	acx_lock(adev, flags);
	enable_acx_irq(adev);
	acx_unlock(adev, flags);

	/* acx fw < 1.9.3.e has a hardware timer, and older drivers
	 ** used to use it. But we don't do that anymore, our OS
	 ** has reliable software timers */
	init_timer(&adev->mgmt_timer);
	adev->mgmt_timer.function = acx_i_timer;
	adev->mgmt_timer.data = (unsigned long)adev;

	/* Need to set ACX_STATE_IFACE_UP first, or else
	 ** timer won't be started by acx_set_status() */
	SET_BIT(adev->dev_state_mask, ACX_STATE_IFACE_UP);

	acx_s_start(adev);

	FN_EXIT0;
}

/***********************************************************************
** acxpci_e_open
**
** Called as a result of SIOCSIFFLAGS ioctl changing the flags bit IFF_UP
** from clear to set. In other words: ifconfig up.
**
** Returns:
**	0	success
**	>0	f/w reported error
**	<0	driver reported error
*/
static int acxcs_e_open(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	int result = OK;

printk("acx: cs_e_open\n");
//return OK;

	FN_ENTER;

	acx_sem_lock(adev);

	adev->initialized = 0;

/* TODO: pci_set_power_state(pdev, PCI_D0); ? */

	/* request shared IRQ handler */
	if (request_irq
	    (adev->irq, acxcs_i_interrupt, IRQF_SHARED, KBUILD_MODNAME, adev)) {
		printk("%s: request_irq FAILED\n", wiphy_name(adev->ieee->wiphy));
		result = -EAGAIN;
		goto done;
	}
	log(L_DEBUG | L_IRQ, "request_irq %d successful\n", adev->irq);

	/* ifup device */
	acxcs_s_up(hw);

	/* We don't currently have to do anything else.
	 * The setup of the MAC should be subsequently completed via
	 * the mlme commands.
	 * Higher layers know we're ready from dev->start==1 and
	 * dev->tbusy==0.  Our rx path knows to pass up received/
	 * frames because of dev->flags&IFF_UP is true.
	 */
	acx_setup_modes(adev);
	
	ieee80211_start_queues(adev->ieee);

	adev->initialized = 1;
      done:
	acx_sem_unlock(adev);

	FN_EXIT1(result);
	return result;
}

static void acxcs_e_close(struct ieee80211_hw *hw)
{
	acx_device_t *adev = ieee2adev(hw);
	unsigned long flags;
	
printk("acx: CS_E_CLOSE\n");
return;

	FN_ENTER;
	printk("putting interface DOWN - this is filled with printk's and will take 8-10 seconds!\n");
	printk("acxpci_e_close: acx_lock()\n");
	acx_s_mwait(1000);
	acx_lock(adev,flags);
	/* ifdown device */
	CLEAR_BIT(adev->dev_state_mask, ACX_STATE_IFACE_UP);
	if (adev->initialized) {
		printk("acxpci_e_close: acxpci_s_down()\n");
		acx_s_mwait(1000);
		acxcs_s_down(hw);
	}

//	if (adev->modes)
//		acx_free_modes(adev);
	/* disable all IRQs, release shared IRQ handler */
	write_reg16(adev, IO_ACX_IRQ_MASK, 0xffff);
	write_reg16(adev, IO_ACX_FEMR, 0x0);

/* TODO: pci_set_power_state(pdev, PCI_D3hot); ? */

	/* We currently don't have to do anything else.
	 * Higher layers know we're not ready from dev->start==0 and
	 * dev->tbusy==1.  Our rx path knows to not pass up received
	 * frames because of dev->flags&IFF_UP is false.
	 */
	printk("acxpci_e_close: acx_unlock()\n");
	acx_s_mwait(1000);
	acx_unlock(adev,flags);

	log(L_INIT, "closed device\n");
	FN_EXIT0;
}

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static int acx_cs_config(struct pcmcia_device *link)
{
	tuple_t tuple;
	cisparse_t parse;
	int last_fn, last_ret;
	int result = -EIO;
	u_char buf[64];
	win_req_t req;
	memreq_t map;

	acx_device_t *adev = link->priv;

	DEBUG(0, "acx_cs_config(0x%p)\n", link);

	/*
	  In this loop, we scan the CIS for configuration table entries,
	  each of which describes a valid card configuration, including
	  voltage, IO window, memory window, and interrupt settings.
	  
	  We make no assumptions about the card to be configured: we use
	  just the information available in the CIS.  In an ideal world,
	  this would work for any PCMCIA card, but it requires a complete
	  and accurate CIS.  In practice, a driver usually "knows" most of
	  these things without consulting the CIS, and most client drivers
	  will only use the CIS to fill in implementation-defined details.
	*/
    tuple.Attributes = 0;
    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleDataMax = sizeof(buf);
    tuple.TupleOffset = 0;
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;

    /* don't trust the CIS on this; Linksys got it wrong */
    //link->conf.Present = 0x63;

    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
        while (1) {
                cistpl_cftable_entry_t dflt = { 0 };
                cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
                if (pcmcia_get_tuple_data(link, &tuple) != 0 ||
                                pcmcia_parse_tuple(link, &tuple, &parse) != 0)
                        goto next_entry;

                if (cfg->flags & CISTPL_CFTABLE_DEFAULT) dflt = *cfg;
                if (cfg->index == 0) goto next_entry;
                link->conf.ConfigIndex = cfg->index;

                /* Does this card need audio output? */
                if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
                        link->conf.Attributes |= CONF_ENABLE_SPKR;
                        link->conf.Status = CCSR_AUDIO_ENA;
                }

                /* Use power settings for Vcc and Vpp if present */
                /*  Note that the CIS values need to be rescaled */
                if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
                        link->conf.Vpp =
                                cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
                else if (dflt.vpp1.present & (1<<CISTPL_POWER_VNOM))
                        link->conf.Vpp =
                                dflt.vpp1.param[CISTPL_POWER_VNOM]/10000;

                /* Do we need to allocate an interrupt? */
                if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
                        link->conf.Attributes |= CONF_ENABLE_IRQ;
                if ((cfg->mem.nwin > 0) || (dflt.mem.nwin > 0)) {
                        cistpl_mem_t *mem =
                                (cfg->mem.nwin) ? &cfg->mem : &dflt.mem;
//                        req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_AM|WIN_ENABLE|WIN_USE_WAIT;
                      req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM|WIN_ENABLE|WIN_USE_WAIT;
                        req.Base = mem->win[0].host_addr;
                        req.Size = mem->win[0].len;
                        req.Size=0x1000;
                        req.AccessSpeed = 0;
                        if (pcmcia_request_window(&link, &req, &link->win) != 0)
                                goto next_entry;
                        map.Page = 0; map.CardOffset = mem->win[0].card_addr;
                        if (pcmcia_map_mem_page(link->win, &map) != 0)
                                goto next_entry;
                        else
                           printk(KERN_INFO "MEMORY WINDOW FOUND!!!\n");
                }
                /* If we got this far, we're cool! */
                break;

        next_entry:
                CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(link, &tuple));
        }

       if (link->conf.Attributes & CONF_ENABLE_IRQ) {
                printk(KERN_INFO "requesting Irq...\n");
                CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
       }

        /*
          This actually configures the PCMCIA socket -- setting up
          the I/O windows and the interrupt mapping, and putting the
          card and host interface into "Memory and IO" mode.
        */
        CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));
	DEBUG(0,"RequestConfiguration OK\n");


        memwin.Base=req.Base;
        memwin.Size=req.Size;

	if ((result = acx_init_netdev(adev, memwin.Base, memwin.Size, link->irq.AssignedIRQ)))
	  return result;

#if 0
//!!!!
	/*
	  At this point, the dev_node_t structure(s) need to be
	  initialized and arranged in a linked list at link->dev_node.
	*/
	strcpy(local->node.dev_name, local->ndev->name );
	local->node.major = local->node.minor = 0;
	link->dev_node = &local->node;
	
	adev->irq = link->irq.AssignedIRQ;
	
	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x: ",
	       local->ndev->name, link->conf.ConfigIndex);
#endif
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk("irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1+link->io.NumPorts1-1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2+link->io.NumPorts2-1);
	if (link->win)
		printk(", mem 0x%06lx-0x%06lx\n", req.Base,
		       req.Base+req.Size-1);
	return 0;

 cs_failed:
	cs_error(link, last_fn, last_ret);
	acx_cs_release(link);
	return -ENODEV;
} /* acx_config */

static inline void
acxmem_write_cmd_type_status(acx_device_t *adev, u16 type, u16 status)
{
  write_slavemem32 (adev, (u32) DRV_DATA(adev)->cmd_area, type | (status << 16));
  write_flush(adev);
}

static const u16 IO_ACX100[] = {
	0x0000,			/* IO_ACX_SOFT_RESET */

	0x0014,			/* IO_ACX_SLV_MEM_ADDR */
	0x0018,			/* IO_ACX_SLV_MEM_DATA */
	0x001c,			/* IO_ACX_SLV_MEM_CTL */
	0x0020,			/* IO_ACX_SLV_END_CTL */

	0x0034,			/* IO_ACX_FEMR */

	0x007c,			/* IO_ACX_INT_TRIG */
	0x0098,			/* IO_ACX_IRQ_MASK */
	0x00a4,			/* IO_ACX_IRQ_STATUS_NON_DES */
	0x00a8,			/* IO_ACX_IRQ_STATUS_CLEAR */
	0x00ac,			/* IO_ACX_IRQ_ACK */
	0x00b0,			/* IO_ACX_HINT_TRIG */

	0x0104,			/* IO_ACX_ENABLE */

	0x0250,			/* IO_ACX_EEPROM_CTL */
	0x0254,			/* IO_ACX_EEPROM_ADDR */
	0x0258,			/* IO_ACX_EEPROM_DATA */
	0x025c,			/* IO_ACX_EEPROM_CFG */

	0x0268,			/* IO_ACX_PHY_ADDR */
	0x026c,			/* IO_ACX_PHY_DATA */
	0x0270,			/* IO_ACX_PHY_CTL */

	0x0290,			/* IO_ACX_GPIO_OE */

	0x0298,			/* IO_ACX_GPIO_OUT */

	0x02a4,			/* IO_ACX_CMD_MAILBOX_OFFS */
	0x02a8,			/* IO_ACX_INFO_MAILBOX_OFFS */
	0x02ac,			/* IO_ACX_EEPROM_INFORMATION */

	0x02d0,			/* IO_ACX_EE_START */
	0x02d4,			/* IO_ACX_SOR_CFG */
	0x02d8			/* IO_ACX_ECPU_CTRL */
};

static const u16 IO_ACX111[] = {
	0x0000,			/* IO_ACX_SOFT_RESET */

	0x0014,			/* IO_ACX_SLV_MEM_ADDR */
	0x0018,			/* IO_ACX_SLV_MEM_DATA */
	0x001c,			/* IO_ACX_SLV_MEM_CTL */
	0x0020,			/* IO_ACX_SLV_END_CTL */

	0x0034,			/* IO_ACX_FEMR */

	0x00b4,			/* IO_ACX_INT_TRIG */
	0x00d4,			/* IO_ACX_IRQ_MASK */
	/* we do mean NON_DES (0xf0), not NON_DES_MASK which is at 0xe0: */
	0x00f0,			/* IO_ACX_IRQ_STATUS_NON_DES */
	0x00e4,			/* IO_ACX_IRQ_STATUS_CLEAR */
	0x00e8,			/* IO_ACX_IRQ_ACK */
	0x00ec,			/* IO_ACX_HINT_TRIG */

	0x01d0,			/* IO_ACX_ENABLE */

	0x0338,			/* IO_ACX_EEPROM_CTL */
	0x033c,			/* IO_ACX_EEPROM_ADDR */
	0x0340,			/* IO_ACX_EEPROM_DATA */
	0x0344,			/* IO_ACX_EEPROM_CFG */

	0x0350,			/* IO_ACX_PHY_ADDR */
	0x0354,			/* IO_ACX_PHY_DATA */
	0x0358,			/* IO_ACX_PHY_CTL */

	0x0374,			/* IO_ACX_GPIO_OE */

	0x037c,			/* IO_ACX_GPIO_OUT */

	0x0388,			/* IO_ACX_CMD_MAILBOX_OFFS */
	0x038c,			/* IO_ACX_INFO_MAILBOX_OFFS */
	0x0390,			/* IO_ACX_EEPROM_INFORMATION */

	0x0100,			/* IO_ACX_EE_START */
	0x0104,			/* IO_ACX_SOR_CFG */
	0x0108,			/* IO_ACX_ECPU_CTRL */
};

typedef struct device_id {
	unsigned char id[6];
	char *descr;
	char *type;
} device_id_t;

static const device_id_t
device_ids[] =
{
	{
		{'G', 'l', 'o', 'b', 'a', 'l'},
		NULL,
		NULL,
	},
	{
		{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		"uninitialized",
		"SpeedStream SS1021 or Gigafast WF721-AEX"
	},
	{
		{0x80, 0x81, 0x82, 0x83, 0x84, 0x85},
		"non-standard",
		"DrayTek Vigor 520"
	},
	{
		{'?', '?', '?', '?', '?', '?'},
		"non-standard",
		"Level One WPC-0200"
	},
	{
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		"empty",
		"DWL-650+ variant"
	}
};

static void
acx_show_card_eeprom_id(acx_device_t *adev)
{
	unsigned char buffer[CARD_EEPROM_ID_SIZE];
	int i;

	memset(&buffer, 0, CARD_EEPROM_ID_SIZE);
	/* use direct EEPROM access */
	for (i = 0; i < CARD_EEPROM_ID_SIZE; i++) {
		if (OK != acxcs_read_eeprom_byte(adev,
					 ACX100_EEPROM_ID_OFFSET + i,
					 &buffer[i])) {
			printk("acx: reading EEPROM FAILED\n");
			break;
		}
	}

	for (i = 0; i < VEC_SIZE(device_ids); i++) {
		if (!memcmp(&buffer, device_ids[i].id, CARD_EEPROM_ID_SIZE)) {
			if (device_ids[i].descr) {
				printk("acx: EEPROM card ID string check "
					"found %s card ID: is this %s?\n",
					device_ids[i].descr, device_ids[i].type);
			}
			break;
		}
	}
	if (i == VEC_SIZE(device_ids)) {
		printk("acx: EEPROM card ID string check found "
			"unknown card: expected 'Global', got '%.*s\'. "
			"Please report\n", CARD_EEPROM_ID_SIZE, buffer);
	}
}

static inline void
init_mboxes(acx_device_t *adev)
{
	u32 cmd_offs, info_offs;

	cmd_offs = read_reg32(adev, IO_ACX_CMD_MAILBOX_OFFS);
	info_offs = read_reg32(adev, IO_ACX_INFO_MAILBOX_OFFS);
	DRV_DATA(adev)->cmd_area = (u8*) cmd_offs;
	DRV_DATA(adev)->info_area = (u8*) info_offs;
	/*
	log(L_DEBUG, "iobase2=%p\n"
	*/
	/*log( L_DEBUG,*/printk( "acx: cmd_mbox_offset=%X cmd_area=%p\n"
		"info_mbox_offset=%X info_area=%p\n",
		cmd_offs, DRV_DATA(adev)->cmd_area,
		info_offs, DRV_DATA(adev)->info_area);
}

/***********************************************************************
** acxmem_write_cmd_type_status
*/

static inline void
acxcs_write_cmd_type_status(acx_device_t *adev, u16 type, u16 status)
{
  write_slavemem32 (adev, (u32) DRV_DATA(adev)->cmd_area, type | (status << 16));
  write_flush(adev);
}

static inline void
read_eeprom_area(acx_device_t *adev)
{
#if ACX_DEBUG > 1
	int offs;
	u8 tmp;

	for (offs = 0x8c; offs < 0xb9; offs++)
		acxcs_read_eeprom_byte(adev, offs, &tmp);
#endif
}

static int
acxcs_s_reset_dev(acx_device_t *adev)
{
	const char* msg = "";
	unsigned long flags;
	int result = NOT_OK;
	u16 hardware_info;
	u16 ecpu_ctrl;
	int count;
	u32 tmp;

	FN_ENTER;
	/*
	write_reg32 (adev, IO_ACX_SLV_MEM_CP, 0);
	*/
	/* reset the device to make sure the eCPU is stopped
	 * to upload the firmware correctly */

	acx_lock(adev, flags);

	/* Windows driver does some funny things here */
	/*
	 * clear bit 0x200 in register 0x2A0
	 */
	clear_regbits (adev, 0x2A0, 0x200);

	/*
	 * Set bit 0x200 in ACX_GPIO_OUT
	 */
	set_regbits (adev, IO_ACX_GPIO_OUT, 0x200);

	/*
	 * read register 0x900 until its value is 0x8400104C, sleeping
	 * in between reads if it's not immediate
	 */
	tmp = read_reg32 (adev, REG_ACX_VENDOR_ID);
	count = 500;
	while (count-- && (tmp != ACX_VENDOR_ID)) {
	  mdelay (10);
	  tmp = read_reg32 (adev, REG_ACX_VENDOR_ID);
	}

	/* end what Windows driver does */

	acx_l_reset_mac(adev);

	ecpu_ctrl = read_reg32(adev, IO_ACX_ECPU_CTRL) & 1;
	if (!ecpu_ctrl) {
		msg = "eCPU is already running. ";
		goto end_unlock;
	}

#ifdef WE_DONT_NEED_THAT_DO_WE
	if (read_reg16(adev, IO_ACX_SOR_CFG) & 2) {
		/* eCPU most likely means "embedded CPU" */
		msg = "eCPU did not start after boot from flash. ";
		goto end_unlock;
	}

	/* check sense on reset flags */
	if (read_reg16(adev, IO_ACX_SOR_CFG) & 0x10) {
		printk("%s: eCPU did not start after boot (SOR), "
			"is this fatal?\n", adev->ndev->name);
	}
#endif
	/* scan, if any, is stopped now, setting corresponding IRQ bit */
	adev->irq_status |= HOST_INT_SCAN_COMPLETE;

	acx_unlock(adev, flags);
	
	/* need to know radio type before fw load */
	/* Need to wait for arrival of this information in a loop,
	 * most probably since eCPU runs some init code from EEPROM
	 * (started burst read in reset_mac()) which also
	 * sets the radio type ID */

	count = 0xffff;
	do {
		hardware_info = read_reg16(adev, IO_ACX_EEPROM_INFORMATION);
		if (!--count) {
			msg = "eCPU didn't indicate radio type";
			goto end_fail;
		}
		cpu_relax();
	} while (!(hardware_info & 0xff00)); /* radio type still zero? */
	printk("ACX radio type 0x%02x\n", (hardware_info >> 8) & 0xff);
	/* printk("DEBUG: count %d\n", count); */
	adev->form_factor = hardware_info & 0xff;
	adev->radio_type = hardware_info >> 8;

	/* load the firmware */
	if (OK != acx_s_upload_fw(adev))
		goto end_fail;

	/* acx_s_msleep(10);	this one really shouldn't be required */

	/* now start eCPU by clearing bit */
	clear_regbits (adev, IO_ACX_ECPU_CTRL, 0x1);
	log(L_DEBUG, "booted eCPU up and waiting for completion...\n");

	/* Windows driver clears bit 0x200 in register 0x2A0 here */
	clear_regbits (adev, 0x2A0, 0x200);

	/* Windows driver sets bit 0x200 in ACX_GPIO_OUT here */
	set_regbits (adev, IO_ACX_GPIO_OUT, 0x200);
	/* wait for eCPU bootup */
	if (OK != acx_s_verify_init(adev)) {
		msg = "timeout waiting for eCPU. ";
		goto end_fail;
	}
	log(L_DEBUG, "eCPU has woken up, card is ready to be configured\n");
	init_mboxes(adev);
	acxcs_write_cmd_type_status(adev, ACX1xx_CMD_RESET, 0);

	/* test that EEPROM is readable */
	read_eeprom_area(adev);

	result = OK;
	goto end;

/* Finish error message. Indicate which function failed */
end_unlock:
	acx_unlock(adev, flags);
end_fail:
	printk("acx: %sreset_dev() FAILED\n", msg);
end:
	FN_EXIT1(result);
	return result;
}

/***********************************************************************
** acxmem_read_cmd_type_status
*/
static u32
acxcs_read_cmd_type_status(acx_device_t *adev)
{
	u32 cmd_type, cmd_status;

	cmd_type = read_slavemem32 (adev, (u32) DRV_DATA(adev)->cmd_area);

	cmd_status = (cmd_type >> 16);
	cmd_type = (u16)cmd_type;

	log(L_CTL, "cmd_type:%04X cmd_status:%04X [%s]\n",
		cmd_type, cmd_status,
		acx_cmd_status_str(cmd_status));

	return cmd_status;
}

static int acxcs_e_suspend( acx_device_t *adev, pm_message_t state)
{
	struct ieee80211_hw *hw = adev->ieee;
printk("acx: suspend not supported\n");
return OK;
	FN_ENTER;
	printk("acx: suspend handler is experimental!\n");
	printk("sus: dev %p\n", hw);

/*	if (!netif_running(ndev))
		goto end;
*/
	adev = ieee2adev(hw);
	printk("sus: adev %p\n", adev);

	acx_sem_lock(adev);

	ieee80211_unregister_hw(hw);	/* this one cannot sleep */
//	acxpci_s_down(hw);
	/* down() does not set it to 0xffff, but here we really want that */
/*	write_reg16(adev, IO_ACX_IRQ_MASK, 0xffff);
	write_reg16(adev, IO_ACX_FEMR, 0x0);
	acxpci_s_delete_dma_regions(adev);
	pci_save_state(pdev);
	pci_set_power_state(pdev, PCI_D3hot);*/

	acxcs_s_down(hw);
	/* down() does not set it to 0xffff, but here we really want that */
	write_reg16(adev, IO_ACX_IRQ_MASK, 0xffff);
	write_reg16(adev, IO_ACX_FEMR, 0x0);
	acxcs_s_delete_dma_regions(adev);

	acx_sem_unlock(adev);
	FN_EXIT0;
	return OK;
    
    return 0;
}

/***********************************************************************
** acxmem_s_issue_cmd_timeo
**
** Sends command to fw, extract result
**
** NB: we do _not_ take lock inside, so be sure to not touch anything
** which may interfere with IRQ handler operation
**
** TODO: busy wait is a bit silly, so:
** 1) stop doing many iters - go to sleep after first
** 2) go to waitqueue based approach: wait, not poll!
*/
#undef FUNC
#define FUNC "issue_cmd"

#if !ACX_DEBUG
int
acxcs_s_issue_cmd_timeo(
	acx_device_t *adev,
	unsigned int cmd,
	void *buffer,
	unsigned buflen,
	unsigned cmd_timeout)
{
#else
int
acxcs_s_issue_cmd_timeo_debug(
	acx_device_t *adev,
	unsigned cmd,
	void *buffer,
	unsigned buflen,
	unsigned cmd_timeout,
	const char* cmdstr)
{
	unsigned long start = jiffies;
#endif
	const char *devname;
	unsigned counter;
	u16 irqtype;
	int i, j;
	u8 *p;
	u16 cmd_status;
	unsigned long timeout;

	FN_ENTER;

	devname = wiphy_name(adev->ieee->wiphy);
	
	if (!devname || !devname[0] || devname[4]=='%')
		devname = "acx";

	log(L_CTL, FUNC"(cmd:%s,buflen:%u,timeout:%ums,type:0x%04X)\n",
		cmdstr, buflen, cmd_timeout,
		buffer ? le16_to_cpu(((acx_ie_generic_t *)buffer)->type) : -1);

	if (!(adev->dev_state_mask & ACX_STATE_FW_LOADED)) {
		printk("%s: "FUNC"(): firmware is not loaded yet, "
			"cannot execute commands!\n", devname);
		goto bad;
	}

	if ((acx_debug & L_DEBUG) && (cmd != ACX1xx_CMD_INTERROGATE)) {
		printk("input buffer (len=%u):\n", buflen);
		acx_dump_bytes(buffer, buflen);
	}

	/* wait for firmware to become idle for our command submission */
	timeout = HZ/5;
	counter = (timeout * 1000 / HZ) - 1; /* in ms */
	timeout += jiffies;
	do {
		cmd_status = acxcs_read_cmd_type_status(adev);
		/* Test for IDLE state */
		if (!cmd_status)
			break;
		if (counter % 8 == 0) {
			if (time_after(jiffies, timeout)) {
				counter = 0;
				break;
			}
			/* we waited 8 iterations, no luck. Sleep 8 ms */
//			acx_s_msleep(8);
			mdelay(8);
			//acx_s_mwait(8);
		}
	} while (likely(--counter));

	if (!counter) {
		/* the card doesn't get idle, we're in trouble */
		printk("%s: "FUNC"(): cmd_status is not IDLE: 0x%04X!=0\n",
			devname, cmd_status);
#if DUMP_IF_SLOW > 0
		dump_acxmem (adev, 0, 0x10000);
		panic ("not idle");
#endif
		goto bad;
	} else if (counter < 190) { /* if waited >10ms... */
		log(L_CTL|L_DEBUG, FUNC"(): waited for IDLE %dms. "
			"Please report\n", 199 - counter);
	}

	/* now write the parameters of the command if needed */
	if (buffer && buflen) {
		/* if it's an INTERROGATE command, just pass the length
		 * of parameters to read, as data */
#if CMD_DISCOVERY
		if (cmd == ACX1xx_CMD_INTERROGATE)
			memset_io(adev->cmd_area + 4, 0xAA, buflen);
#endif
		/*
		 * slave memory version
		 */
		copy_to_slavemem (adev, (u32) (DRV_DATA(adev)->cmd_area + 4), buffer, 
			       (cmd == ACX1xx_CMD_INTERROGATE) ? 4 : buflen);
	}
	/* now write the actual command type */
	acxmem_write_cmd_type_status(adev, cmd, 0);

	/* clear CMD_COMPLETE bit. can be set only by IRQ handler: */
	adev->irq_status &= ~HOST_INT_CMD_COMPLETE;

	/* execute command */
	write_reg16(adev, IO_ACX_INT_TRIG, INT_TRIG_CMD);
	write_flush(adev);

	/* wait for firmware to process command */

	/* Ensure nonzero and not too large timeout.
	** Also converts e.g. 100->99, 200->199
	** which is nice but not essential */
	cmd_timeout = (cmd_timeout-1) | 1;
	if (unlikely(cmd_timeout > 1199))
		cmd_timeout = 1199;

	/* we schedule away sometimes (timeout can be large) */
	counter = cmd_timeout;
	timeout = jiffies + cmd_timeout * HZ / 1000;
	do {
		if (!DRV_DATA(adev)->irqs_active) { /* IRQ disabled: poll */
			irqtype = read_reg16(adev, IO_ACX_IRQ_STATUS_NON_DES);
			if (irqtype & HOST_INT_CMD_COMPLETE) {
				write_reg16(adev, IO_ACX_IRQ_ACK,
						HOST_INT_CMD_COMPLETE);
				break;
			}
		} else { /* Wait when IRQ will set the bit */
			irqtype = adev->irq_status;
			if (irqtype & HOST_INT_CMD_COMPLETE)
				break;
		}

		if (counter % 8 == 0) {
			if (time_after(jiffies, timeout)) {
				counter = 0;
				break;
			}
			/* we waited 8 iterations, no luck. Sleep 8 ms */
			//acx_s_mwait(8);
			mdelay(8);
		}
	} while (likely(--counter));

	/* save state for debugging */
	cmd_status = acxcs_read_cmd_type_status(adev);

	/* put the card in IDLE state */
	acxmem_write_cmd_type_status(adev, ACX1xx_CMD_RESET, 0);

	if (!counter) {	/* timed out! */
		printk("%s: "FUNC"(): timed out %s for CMD_COMPLETE. "
			"irq bits:0x%04X irq_status:0x%04X timeout:%dms "
			"cmd_status:%d (%s)\n",
			devname, (DRV_DATA(adev)->irqs_active) ? "waiting" : "polling",
			irqtype, adev->irq_status, cmd_timeout,
			cmd_status, acx_cmd_status_str(cmd_status));
		printk("%s: "FUNC"(): device irq status 0x%04x\n",
		       devname, read_reg16(adev, IO_ACX_IRQ_STATUS_NON_DES));
		printk("%s: "FUNC"(): IO_ACX_IRQ_MASK 0x%04x IO_ACX_FEMR 0x%04x\n",
		       devname,
		       read_reg16 (adev, IO_ACX_IRQ_MASK),
		       read_reg16 (adev, IO_ACX_FEMR));
		if (read_reg16 (adev, IO_ACX_IRQ_MASK) == 0xffff) {
			printk ("acxmem: firmware probably hosed - reloading\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
                	{
				pm_message_t state;
				/* acxmem_e_suspend (resume_pdev, state); */
				acxcs_e_suspend (adev , state);

			}
#else
			acxcs_e_suspend (adev, 0);
#endif
			{
				printk("acx: resume: FIXME\n");
//!!!!				resume_ndev = adev->ndev;
//				fw_resumer (NULL);
			}
		}

		goto bad;
	} else if (cmd_timeout - counter > 30) { /* if waited >30ms... */
		log(L_CTL|L_DEBUG, FUNC"(): %s for CMD_COMPLETE %dms. "
			"count:%d. Please report\n",
			(DRV_DATA(adev)->irqs_active) ? "waited" : "polled",
			cmd_timeout - counter, counter);
	}

	if (1 != cmd_status) { /* it is not a 'Success' */
		printk("%s: "FUNC"(): cmd_status is not SUCCESS: %d (%s). "
			"Took %dms of %d\n",
			devname, cmd_status, acx_cmd_status_str(cmd_status),
			cmd_timeout - counter, cmd_timeout);
		/* zero out result buffer
		 * WARNING: this will trash stack in case of illegally large input
		 * length! */
		if (buflen > 388) {
		  /*
		   * 388 is maximum command length
		   */
		  printk ("invalid length 0x%08x\n", buflen);
		  buflen = 388;
		}
		p = (u8 *) buffer;
		for (i = 0; i < buflen; i+= 16) {
		  printk ("%04x:", i);
		  for (j = 0; (j < 16) && (i+j < buflen); j++) {
		    printk (" %02x", *p++);
		  }
		  printk ("\n");
		}
		if (buffer && buflen)
			memset(buffer, 0, buflen);
		goto bad;
	}

	/* read in result parameters if needed */
	if (buffer && buflen && (cmd == ACX1xx_CMD_INTERROGATE)) {
		copy_from_slavemem (adev, buffer, (u32) (DRV_DATA(adev)->cmd_area + 4), buflen);
		if (acx_debug & L_DEBUG) {
			printk("output buffer (len=%u): ", buflen);
			acx_dump_bytes(buffer, buflen);
		}
	}

/* ok: */
	log(L_CTL, FUNC"(%s): took %ld jiffies to complete\n",
			 cmdstr, jiffies - start);
	FN_EXIT1(OK);
	return OK;

bad:
	/* Give enough info so that callers can avoid
	** printing their own diagnostic messages */
#if ACX_DEBUG
	printk("%s: "FUNC"(cmd:%s) FAILED\n", devname, cmdstr);
#else
	printk("%s: "FUNC"(cmd:0x%04X) FAILED\n", devname, cmd);
#endif
	dump_stack();
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}

#define MAX_IRQLOOPS_PER_JIFFY  (20000/HZ)	/* a la orinoco.c */

/* Interrupt handler bottom-half */
void acxcs_interrupt_tasklet(struct work_struct *work)
{

#ifdef CONFIG_ACX_MAC80211_DEBUG
	u32 _handled = 0x00000000;
# define acxirq_handled(irq)    do { _handled |= (irq); } while (0)
#else
# define acxirq_handled(irq)    do { /* nothing */ } while (0)
#endif /* CONFIG_ACX_MAC80211_DEBUG */

	acx_device_t *adev = container_of(work,struct acx_device, after_interrupt_task);
	
//	unsigned int irqcount = MAX_IRQLOOPS_PER_JIFFY;
	int irqtype;

	/* LOCKING: can just spin_lock() since IRQs are disabled anyway.
	 * I am paranoid */
	acx_sem_lock(adev);


	FN_ENTER;
	
	printk("acx: tasklet!\n");
	
	irqtype = adev->irq_reason;
	adev->irq_reason = 0;

	printk("acx: tasklet: reason: %04X\n", irqtype);

#define IRQ_ITERATE 0
#if IRQ_ITERATE
	if (jiffies != adev->irq_last_jiffies) {
		adev->irq_loops_this_jiffy = 0;
		adev->irq_last_jiffies = jiffies;
	}

/* safety condition; we'll normally abort loop below
 * in case no IRQ type occurred */
	while (likely(--irqcount)) {
#endif
		/* ACK all IRQs ASAP */


		/* Handle most important IRQ types first */
		if ((irqtype & HOST_INT_RX_COMPLETE) || (irqtype & HOST_INT_RX_DATA)) {
			log(L_IRQ, "got Rx_Complete IRQ\n");
printk("got Rx_Complete IRQ\n");
			adev->ops->l_process_rxdesc(adev);
		}
		if (irqtype & HOST_INT_TX_COMPLETE) {
			log(L_IRQ, "got Tx_Complete IRQ\n");
			/* don't clean up on each Tx complete, wait a bit
			 * unless we're going towards full, in which case
			 * we do it immediately, too (otherwise we might lockup
			 * with a full Tx buffer if we go into
			 * acxpci_l_clean_txdesc() at a time when we won't wakeup
			 * the net queue in there for some reason...) */
//			if (adev->tx_free <= TX_START_CLEAN) {
//!!!!				adev->ops->l_clean_txdesc(adev);
//			}
		}

		/* Less frequent ones */
		if (irqtype & (0
			       | HOST_INT_CMD_COMPLETE
			       | HOST_INT_INFO | HOST_INT_SCAN_COMPLETE)) {
			if (irqtype & HOST_INT_INFO) {
//!!!!				handle_info_irq(adev);
			}
			if (irqtype & HOST_INT_SCAN_COMPLETE) {
				log(L_IRQ, "got Scan_Complete IRQ\n");
				/* need to do that in process context */
				/* remember that fw is not scanning anymore */
				SET_BIT(adev->irq_status,
					HOST_INT_SCAN_COMPLETE);
			}
		}

		/* These we just log, but either they happen rarely
		 * or we keep them masked out */
		if (irqtype & (0 | HOST_INT_RX_DATA
			       /* | HOST_INT_TX_COMPLETE   */
			       | HOST_INT_TX_XFER
			       /* | HOST_INT_RX_COMPLETE   */
			       | HOST_INT_DTIM
			       | HOST_INT_BEACON
			       | HOST_INT_TIMER
			       | HOST_INT_KEY_NOT_FOUND
			       | HOST_INT_IV_ICV_FAILURE
			       /* | HOST_INT_CMD_COMPLETE  */
			       /* | HOST_INT_INFO	  */
			       | HOST_INT_OVERFLOW | HOST_INT_PROCESS_ERROR
			       /* | HOST_INT_SCAN_COMPLETE */
			       | HOST_INT_FCS_THRESHOLD | HOST_INT_UNKNOWN)) {
//!!!!			log_unusual_irq(irqtype);
		}
#if IRQ_ITERATE
		unmasked = read_reg16(adev, IO_ACX_IRQ_STATUS_CLEAR);
		irqtype = unmasked & ~adev->irq_mask;
		/* Bail out if no new IRQ bits or if all are masked out */
		if (!irqtype)
			break;

		if (unlikely
		    (++adev->irq_loops_this_jiffy > MAX_IRQLOOPS_PER_JIFFY)) {
			printk(KERN_ERR
			       "acx: too many interrupts per jiffy!\n");
			/* Looks like card floods us with IRQs! Try to stop that */
			write_reg16(adev, IO_ACX_IRQ_MASK, 0xffff);
			/* This will short-circuit all future attempts to handle IRQ.
			 * We cant do much more... */
			adev->irq_mask = 0;
			break;
		}
	}
#endif
	/* Routine to perform blink with range */
//!!!!	if (unlikely(adev->led_power == 2))
//		update_link_quality_led(adev);

/* handled: */
	if (adev->after_interrupt_jobs)
		acx_e_after_interrupt_task(&adev->after_interrupt_task);

	/* write_flush(adev); - not needed, last op was read anyway */
	acx_sem_unlock(adev);
	FN_EXIT0;
	return;			

}

#if 0
/***********************************************************************
** PCI
*/
void acxcs_set_interrupt_mask(acx_device_t * adev)
{
	if (IS_ACX111(adev)) {
		DRV_DATA(adev)->irq_mask = (u16) ~ (0
					  /* | HOST_INT_RX_DATA	*/
					  | HOST_INT_TX_COMPLETE
					  /* | HOST_INT_TX_XFER	*/
					  | HOST_INT_RX_COMPLETE
					  /* | HOST_INT_DTIM	   */
					  /* | HOST_INT_BEACON	 */
					  /* | HOST_INT_TIMER	  */
					  /* | HOST_INT_KEY_NOT_FOUND  */
					  | HOST_INT_IV_ICV_FAILURE
					  | HOST_INT_CMD_COMPLETE
					  | HOST_INT_INFO
					  /* | HOST_INT_OVERFLOW       */
					  /* | HOST_INT_PROCESS_ERROR  */
					  | HOST_INT_SCAN_COMPLETE
					  | HOST_INT_FCS_THRESHOLD
					  /* | HOST_INT_UNKNOWN	*/
		    );
		/* Or else acx100 won't signal cmd completion, right? */
		DRV_DATA(adev)->irq_mask_off = (u16) ~ (HOST_INT_CMD_COMPLETE);	/* 0xfdff */
	} else {
		DRV_DATA(adev)->irq_mask = (u16) ~ (0
					  /* | HOST_INT_RX_DATA	*/
					  | HOST_INT_TX_COMPLETE
					  /* | HOST_INT_TX_XFER	*/
					  | HOST_INT_RX_COMPLETE
					  /* | HOST_INT_DTIM	   */
					  /* | HOST_INT_BEACON	 */
					  /* | HOST_INT_TIMER	  */
					  /* | HOST_INT_KEY_NOT_FOUND  */
					  /* | HOST_INT_IV_ICV_FAILURE */
					  | HOST_INT_CMD_COMPLETE
					  | HOST_INT_INFO
					  /* | HOST_INT_OVERFLOW       */
					  /* | HOST_INT_PROCESS_ERROR  */
					  | HOST_INT_SCAN_COMPLETE
					  /* | HOST_INT_FCS_THRESHOLD  */
					  /* | HOST_INT_UNKNOWN	*/
		    );
		DRV_DATA(adev)->irq_mask_off = (u16) ~ (HOST_INT_UNKNOWN);	/* 0x7fff */
	}
}


/***********************************************************************/
#else
void
acxcs_set_interrupt_mask(acx_device_t *adev)
{
	if (IS_ACX111(adev)) {
		DRV_DATA(adev)->irq_mask = (u16) ~(0
				| HOST_INT_RX_DATA       
				| HOST_INT_TX_COMPLETE
				/* | HOST_INT_TX_XFER        */
				/* | HOST_INT_RX_COMPLETE    */
				/* | HOST_INT_DTIM           */
				/* | HOST_INT_BEACON         */
				/* | HOST_INT_TIMER          */
				/* | HOST_INT_KEY_NOT_FOUND  */
				| HOST_INT_IV_ICV_FAILURE
				| HOST_INT_CMD_COMPLETE
				| HOST_INT_INFO
				| HOST_INT_OVERFLOW    
				/* | HOST_INT_PROCESS_ERROR  */
				| HOST_INT_SCAN_COMPLETE
				| HOST_INT_FCS_THRESHOLD
				| HOST_INT_UNKNOWN
				);
		/* Or else acx100 won't signal cmd completion, right? */
		DRV_DATA(adev)->irq_mask_off = (u16)~( HOST_INT_CMD_COMPLETE ); /* 0xfdff */
	} else {
		DRV_DATA(adev)->irq_mask = (u16) ~(0
				| HOST_INT_RX_DATA 
				| HOST_INT_TX_COMPLETE
				/* | HOST_INT_TX_XFER        */
				/* | HOST_INT_RX_COMPLETE    */
				/* | HOST_INT_DTIM           */
				/* | HOST_INT_BEACON         */
				/* | HOST_INT_TIMER          */
				/* | HOST_INT_KEY_NOT_FOUND  */
				/* | HOST_INT_IV_ICV_FAILURE */
				| HOST_INT_CMD_COMPLETE
				| HOST_INT_INFO
				/* | HOST_INT_OVERFLOW       */
				/* | HOST_INT_PROCESS_ERROR  */
				| HOST_INT_SCAN_COMPLETE
				/* | HOST_INT_FCS_THRESHOLD  */
				/* | HOST_INT_BEACON_MISSED        */
				);
		DRV_DATA(adev)->irq_mask_off = (u16)~( HOST_INT_UNKNOWN ); /* 0x7fff */
	}
}
#endif

struct device * get_dev(acx_device_t *adev)
{
    printk("acx: get_dev (%X)\n", (unsigned int)&DRV_DATA(adev)->pdev->dev);
    return &DRV_DATA(adev)->pdev->dev;
}

/***********************************************************************
** acxmem_s_create_tx_host_desc_queue
*/

static void*
allocate(acx_device_t *adev, size_t size, dma_addr_t *phy, const char *msg)
{
	void *ptr;
        ptr = kmalloc (size, GFP_KERNEL);
	/*
	 * The ACX can't use the physical address, so we'll have to fake it
	 * later and it might be handy to have the virtual address.
	 */
	*phy = (dma_addr_t) NULL;

	if (ptr) {
		log(L_DEBUG, "%s sz=%d adr=0x%p phy=0x%08llx\n",
				msg, (int)size, ptr, (unsigned long long)*phy);
		memset(ptr, 0, size);
		return ptr;
	}
	printk(KERN_ERR "acx: %s allocation FAILED (%d bytes)\n",
					msg, (int)size);
	return NULL;
}


/*
 * In the generic slave memory access mode, most of the stuff in
 * the txhostdesc_t is unused.  It's only here because the rest of
 * the ACX driver expects it to be since the PCI version uses indirect
 * host memory organization with DMA.  Since we're not using DMA the
 * only use we have for the host descriptors is to store the packets
 * on the way out.
 */
static int
acxcs_s_create_tx_host_desc_queue(acx_device_t *adev)
{
	txhostdesc_t *hostdesc;
	u8 *txbuf;
	int i;

	FN_ENTER;

	/* allocate TX buffer */
	DRV_DATA(adev)->txbuf_area_size = TX_CNT * WLAN_A4FR_MAXLEN_WEP_FCS;

	DRV_DATA(adev)->txbuf_start = allocate(adev, DRV_DATA(adev)->txbuf_area_size,
				     &DRV_DATA(adev)->txbuf_startphy, "txbuf_start");
	if (!DRV_DATA(adev)->txbuf_start)
	  goto fail;

	/* allocate the TX host descriptor queue pool */
	DRV_DATA(adev)->txhostdesc_area_size = TX_CNT * 2*sizeof(*hostdesc);

	DRV_DATA(adev)->txhostdesc_start = allocate(adev, DRV_DATA(adev)->txhostdesc_area_size,
					  &DRV_DATA(adev)->txhostdesc_startphy, "txhostdesc_start");
	if (!DRV_DATA(adev)->txhostdesc_start)
	  goto fail;

	/* check for proper alignment of TX host descriptor pool */
	if ((long) DRV_DATA(adev)->txhostdesc_start & 3) {
		printk("acx: driver bug: dma alloc returns unaligned address\n");
		goto fail;
	}

	hostdesc = DRV_DATA(adev)->txhostdesc_start;
	txbuf = DRV_DATA(adev)->txbuf_start;

#if 0
/* Each tx buffer is accessed by hardware via
** txdesc -> txhostdesc(s) -> txbuffer(s).
** We use only one txhostdesc per txdesc, but it looks like
** acx111 is buggy: it accesses second txhostdesc
** (via hostdesc.desc_phy_next field) even if
** txdesc->length == hostdesc->length and thus
** entire packet was placed into first txhostdesc.
** Due to this bug acx111 hangs unless second txhostdesc
** has le16_to_cpu(hostdesc.length) = 3 (or larger)
** Storing NULL into hostdesc.desc_phy_next
** doesn't seem to help.
**
** Update: although it worked on Xterasys XN-2522g
** with len=3 trick, WG311v2 is even more bogus, doesn't work.
** Keeping this code (#ifdef'ed out) for documentational purposes.
*/
	for (i = 0; i < TX_CNT*2; i++) {
		hostdesc_phy += sizeof(*hostdesc);
		if (!(i & 1)) {
			hostdesc->data_phy = cpu2acx(txbuf_phy);
			/* hostdesc->data_offset = ... */
			/* hostdesc->reserved = ... */
			hostdesc->Ctl_16 = cpu_to_le16(DESC_CTL_HOSTOWN);
			/* hostdesc->length = ... */
			hostdesc->desc_phy_next = cpu2acx(hostdesc_phy);
			hostdesc->pNext = ptr2acx(NULL);
			/* hostdesc->Status = ... */
			/* below: non-hardware fields */
			hostdesc->data = txbuf;

			txbuf += WLAN_A4FR_MAXLEN_WEP_FCS;
			txbuf_phy += WLAN_A4FR_MAXLEN_WEP_FCS;
		} else {
			/* hostdesc->data_phy = ... */
			/* hostdesc->data_offset = ... */
			/* hostdesc->reserved = ... */
			/* hostdesc->Ctl_16 = ... */
			hostdesc->length = cpu_to_le16(3); /* bug workaround */
			/* hostdesc->desc_phy_next = ... */
			/* hostdesc->pNext = ... */
			/* hostdesc->Status = ... */
			/* below: non-hardware fields */
			/* hostdesc->data = ... */
		}
		hostdesc++;
	}
#endif
/* We initialize two hostdescs so that they point to adjacent
** memory areas. Thus txbuf is really just a contiguous memory area */
	for (i = 0; i < TX_CNT*2; i++) {
		/* ->data is a non-hardware field: */
		hostdesc->data = txbuf;

		if (!(i & 1)) {
			txbuf += WLAN_HDR_A3_LEN;
		} else {
			txbuf += WLAN_A4FR_MAXLEN_WEP_FCS - WLAN_HDR_A3_LEN;
		}
		hostdesc++;
	}
	hostdesc--;

	FN_EXIT1(OK);
	return OK;
fail:
	printk("acx: create_tx_host_desc_queue FAILED\n");
	/* dealloc will be done by free function on error case */
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***************************************************************
** acxmem_s_create_rx_host_desc_queue
*/
/* the whole size of a data buffer (header plus data body)
 * plus 32 bytes safety offset at the end */
#define RX_BUFFER_SIZE (sizeof(rxbuffer_t) + 32)

static int
acxcs_s_create_rx_host_desc_queue(acx_device_t *adev)
{
	rxhostdesc_t *hostdesc;
	rxbuffer_t *rxbuf;
	int i;

	FN_ENTER;

	/* allocate the RX host descriptor queue pool */
	DRV_DATA(adev)->rxhostdesc_area_size = RX_CNT * sizeof(*hostdesc);

	DRV_DATA(adev)->rxhostdesc_start = allocate(adev, DRV_DATA(adev)->rxhostdesc_area_size,
					  &adev->rxhostdesc_startphy, "rxhostdesc_start");
	if (!DRV_DATA(adev)->rxhostdesc_start)
	  goto fail;

	/* check for proper alignment of RX host descriptor pool */
	if ((long) DRV_DATA(adev)->rxhostdesc_start & 3) {
		printk("acx: driver bug: dma alloc returns unaligned address\n");
		goto fail;
	}

	/* allocate Rx buffer pool which will be used by the acx
	 * to store the whole content of the received frames in it */
	DRV_DATA(adev)->rxbuf_area_size = RX_CNT * RX_BUFFER_SIZE;

	DRV_DATA(adev)->rxbuf_start = allocate(adev, DRV_DATA(adev)->rxbuf_area_size,
				     &DRV_DATA(adev)->rxbuf_startphy, "rxbuf_start");
	if (!DRV_DATA(adev)->rxbuf_start)
	  goto fail;

	rxbuf = DRV_DATA(adev)->rxbuf_start;
	hostdesc = DRV_DATA(adev)->rxhostdesc_start;

	/* don't make any popular C programming pointer arithmetic mistakes
	 * here, otherwise I'll kill you...
	 * (and don't dare asking me why I'm warning you about that...) */
	for (i = 0; i < RX_CNT; i++) {
		hostdesc->data = rxbuf;
		hostdesc->length = cpu_to_le16(RX_BUFFER_SIZE);
		rxbuf++;
		hostdesc++;
	}
	hostdesc--;
	FN_EXIT1(OK);
	return OK;
fail:
	printk("acx: create_rx_host_desc_queue FAILED\n");
	/* dealloc will be done by free function on error case */
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***************************************************************
** acxmem_s_create_hostdesc_queues
*/
int
acxcs_s_create_hostdesc_queues(acx_device_t *adev)
{
	int result;
	result = acxcs_s_create_tx_host_desc_queue(adev);
	if (OK != result) return result;
	result = acxcs_s_create_rx_host_desc_queue(adev);
	return result;
}

/***************************************************************
** acxmem_create_tx_desc_queue
*/
static void
acxcs_create_tx_desc_queue(acx_device_t *adev, u32 tx_queue_start)
{
	txdesc_t *txdesc;
	u32 clr;
	int i;

	FN_ENTER;

	if (IS_ACX100(adev))
		DRV_DATA(adev)->txdesc_size = sizeof(*txdesc);
	else
		/* the acx111 txdesc is 4 bytes larger */
		DRV_DATA(adev)->txdesc_size = sizeof(*txdesc) + 4;

	/*
	 * This refers to an ACX address, not one of ours
	 */
	DRV_DATA(adev)->txdesc_start = (txdesc_t *) tx_queue_start;

printk("acx: create_tx_desc_queue: adev->txdesc_start=%p\n", DRV_DATA(adev)->txdesc_start);

	log(L_DEBUG, "adev->txdesc_start=%p\n",
			DRV_DATA(adev)->txdesc_start);

	adev->tx_free = TX_CNT;
	/* done by memset: adev->tx_head = 0; */
	/* done by memset: adev->tx_tail = 0; */
	txdesc = DRV_DATA(adev)->txdesc_start;

	if (IS_ACX111(adev)) {
		/* ACX111 has a preinitialized Tx buffer! */
		/* loop over whole send pool */
		/* FIXME: do we have to do the hostmemptr stuff here?? */
		for (i = 0; i < TX_CNT; i++) {
			txdesc->Ctl_8 = DESC_CTL_HOSTOWN;
			/* reserve two (hdr desc and payload desc) */
			txdesc = advance_txdesc(adev, txdesc, 1);
		}
	} else {
		/* ACX100 Tx buffer needs to be initialized by us */
		/* clear whole send pool. sizeof is safe here (we are acx100) */

		/*
		 * adev->txdesc_start refers to device memory, so we can't write
		 * directly to it.
		 */
		clr = (u32) DRV_DATA(adev)->txdesc_start;
		while (clr < (u32) DRV_DATA(adev)->txdesc_start + (TX_CNT * sizeof(*txdesc))) {
		  write_slavemem32 (adev, clr, 0);
		  clr += 4;
		}

		/* loop over whole send pool */
		for (i = 0; i < TX_CNT; i++) {
			log(L_DEBUG, "configure card tx descriptor: 0x%p, "
				"size: 0x%X\n", txdesc, DRV_DATA(adev)->txdesc_size);

			/* initialise ctl */
			/*
			 * No auto DMA here
			 */
			write_slavemem8 (adev, (u32) &(txdesc->Ctl_8),
					(u8) (DESC_CTL_HOSTOWN | DESC_CTL_FIRSTFRAG));
			/* done by memset(0): txdesc->Ctl2_8 = 0; */

			/* point to next txdesc */
			write_slavemem32 (adev, (u32) &(txdesc->pNextDesc),
					  (u32) cpu_to_le32 ((u8 *) txdesc + DRV_DATA(adev)->txdesc_size));

			/* go to the next one */
			/* ++ is safe here (we are acx100) */
			txdesc++;
		}
		/* go back to the last one */
		txdesc--;
		/* and point to the first making it a ring buffer */
		write_slavemem32 (adev, (u32) &(txdesc->pNextDesc),
				  (u32) cpu_to_le32 (tx_queue_start));
	}
	FN_EXIT0;
}


/***************************************************************
** acxmem_create_rx_desc_queue
*/
static void
acxcs_create_rx_desc_queue(acx_device_t *adev, u32 rx_queue_start)
{
	rxdesc_t *rxdesc;
	u32 mem_offs;
	int i;
	FN_ENTER;

	/* done by memset: adev->rx_tail = 0; */

	/* ACX111 doesn't need any further config: preconfigures itself.
	 * Simply print ring buffer for debugging */
	if (IS_ACX111(adev)) {
		/* rxdesc_start already set here */

		DRV_DATA(adev)->rxdesc_start = (rxdesc_t *) rx_queue_start;

		rxdesc = DRV_DATA(adev)->rxdesc_start;
		for (i = 0; i < RX_CNT; i++) {
			log(L_DEBUG, "rx descriptor %d @ 0x%p\n", i, rxdesc);
			rxdesc = DRV_DATA(adev)->rxdesc_start = (rxdesc_t *)
			  acx2cpu(rxdesc->pNextDesc);
		}
	} else {
		/* we didn't pre-calculate rxdesc_start in case of ACX100 */
		/* rxdesc_start should be right AFTER Tx pool */
		DRV_DATA(adev)->rxdesc_start = (rxdesc_t *)
			((u8 *) DRV_DATA(adev)->txdesc_start + (TX_CNT * sizeof(txdesc_t)));
		/* NB: sizeof(txdesc_t) above is valid because we know
		** we are in if (acx100) block. Beware of cut-n-pasting elsewhere!
		** acx111's txdesc is larger! */

		mem_offs = (u32) DRV_DATA(adev)->rxdesc_start;
		while (mem_offs < (u32) DRV_DATA(adev)->rxdesc_start + (RX_CNT * sizeof (*rxdesc))) {
		  write_slavemem32 (adev, mem_offs, 0);
		  mem_offs += 4;
		}

		/* loop over whole receive pool */
		rxdesc = DRV_DATA(adev)->rxdesc_start;
		for (i = 0; i < RX_CNT; i++) {
			log(L_DEBUG, "rx descriptor @ 0x%p\n", rxdesc);
			/* point to next rxdesc */
			write_slavemem32 (adev, (u32) &(rxdesc->pNextDesc),
					  (u32) cpu_to_le32 ((u8 *) rxdesc + sizeof(*rxdesc)));
			/* go to the next one */
			rxdesc++;
		}
		/* go to the last one */
		rxdesc--;

		/* and point to the first making it a ring buffer */
		write_slavemem32 (adev, (u32) &(rxdesc->pNextDesc),
				  (u32) cpu_to_le32 (rx_queue_start));
	}
	FN_EXIT0;
}


/***************************************************************
** acxmem_create_desc_queues
*/
void
acxcs_create_desc_queues(acx_device_t *adev, u32 tx_queue_start, u32 rx_queue_start)
{
  u32 *p;
  int i;

	acxcs_create_tx_desc_queue(adev, tx_queue_start);
	acxcs_create_rx_desc_queue(adev, rx_queue_start);
	p = (u32 *) DRV_DATA(adev)->acx_queue_indicator;
	for (i = 0; i < 4; i++) {
	  write_slavemem32 (adev, (u32) p, 0);
	  p++;
	}
}

/***********************************************************************
** acxpci_s_upload_radio
**
** Uploads the appropriate radio module firmware into the card.
**
** Origin: Standard Read/Write to IO
*/
int acxcs_s_upload_radio(acx_device_t * adev)
{
	acx_ie_memmap_t mm;
	firmware_image_t *radio_image;
	acx_cmd_radioinit_t radioinit;
	int res = NOT_OK;
	int try;
	u32 offset;
	u32 size;
	char filename[sizeof("tiacx1NNrNN")];

	if (!adev->need_radio_fw)
		return OK;

	FN_ENTER;

	acx_s_interrogate(adev, &mm, ACX1xx_IE_MEMORY_MAP);
	offset = le32_to_cpu(mm.CodeEnd);

	snprintf(filename, sizeof(filename), "tiacx1%02dr%02X",
		 IS_ACX111(adev) * 11, adev->radio_type);
	radio_image = acx_s_read_fw(adev, filename, &size);
	if (!radio_image) {
		printk("acx: can't load radio module '%s'\n", filename);
		goto fail;
	}

	acx_s_issue_cmd(adev, ACX1xx_CMD_SLEEP, NULL, 0);

	for (try = 1; try <= 5; try++) {
		res = acx_s_write_fw(adev, radio_image, offset);
		log(L_DEBUG | L_INIT, "acx_write_fw (radio): %d\n", res);
		if (OK == res) {
			res = acx_s_validate_fw(adev, radio_image, offset);
			log(L_DEBUG | L_INIT, "acx_validate_fw (radio): %d\n",
			    res);
		}

		if (OK == res)
			break;
		printk("acx: radio firmware upload attempt #%d FAILED, "
		       "retrying...\n", try);
		acx_s_mwait(1000);	/* better wait for a while... */
	}

	acx_s_issue_cmd(adev, ACX1xx_CMD_WAKE, NULL, 0);
	radioinit.offset = cpu_to_le32(offset);
	/* no endian conversion needed, remains in card CPU area: */
	radioinit.len = radio_image->size;

	vfree(radio_image);

	if (OK != res)
		goto fail;

	/* will take a moment so let's have a big timeout */
	acx_s_issue_cmd_timeo(adev, ACX1xx_CMD_RADIOINIT,
			      &radioinit, sizeof(radioinit),
			      CMD_TIMEOUT_MS(1000));

	res = acx_s_interrogate(adev, &mm, ACX1xx_IE_MEMORY_MAP);
      fail:
	FN_EXIT1(res);
	return res;
}

void*
acxcs_l_get_txbuf(acx_device_t *adev, tx_t* tx_opaque)
{
	return get_txhostdesc(adev, (txdesc_t*)tx_opaque)->data;
}

void acxcs_l_tx_data(acx_device_t * adev, tx_t * tx_opaque, int len,
		 struct ieee80211_tx_control *ieeectl,struct sk_buff* skb)
{
	txdesc_t *txdesc = (txdesc_t *) tx_opaque;
	struct ieee80211_hdr *wireless_header;
	txhostdesc_t *hostdesc1, *hostdesc2;
	int rate_cur;
	u8 Ctl_8, Ctl2_8;
	int wlhdr_len;
	
printk("acx: l_tx_data\n");

	FN_ENTER;

	/* fw doesn't tx such packets anyhow */
/*	if (unlikely(len < WLAN_HDR_A3_LEN))
		goto end;
*/
	hostdesc1 = get_txhostdesc(adev, txdesc);
printk("acx: l_tx_data: hostdesc1: %p\n", hostdesc1);
	wireless_header = (struct ieee80211_hdr *)hostdesc1->data;
printk("acx: l_tx_data: wireless_header: %p\n", hostdesc1);
	/* modify flag status in separate variable to be able to write it back
	 * in one big swoop later (also in order to have less device memory
	 * accesses) */
//	Ctl_8 = txdesc->Ctl_8;
	Ctl_8 = read_slavemem8 (adev, (u32) &(txdesc->Ctl_8));

	Ctl2_8 = 0;		/* really need to init it to 0, not txdesc->Ctl2_8, it seems */

	hostdesc2 = hostdesc1 + 1;

	/* DON'T simply set Ctl field to 0 here globally,
	 * it needs to maintain a consistent flag status (those are state flags!!),
	 * otherwise it may lead to severe disruption. Only set or reset particular
	 * flags at the exact moment this is needed... */

	/* let chip do RTS/CTS handshaking before sending
	 * in case packet size exceeds threshold */
	if (ieeectl->flags & IEEE80211_TXCTL_USE_RTS_CTS)
		SET_BIT(Ctl2_8, DESC_CTL2_RTS);
	else
		CLEAR_BIT(Ctl2_8, DESC_CTL2_RTS);

	rate_cur = ieeectl->tx_rate;
	if (unlikely(!rate_cur)) {
		printk("acx: driver bug! bad ratemask\n");
		goto end;
	}

	/* used in tx cleanup routine for auto rate and accounting: */
/*	put_txcr(adev, txdesc, clt, rate_cur);  deprecated by mac80211 */

//	txdesc->total_length = cpu_to_le16(len);
	write_slavemem8 (adev, (u32) &(txdesc->total_length), cpu_to_le16(len));

	wlhdr_len = ieee80211_get_hdrlen(le16_to_cpu(wireless_header->frame_control));
	hostdesc2->length = cpu_to_le16(len - wlhdr_len);
/*
	if (!ieeectl->do_not_encrypt && ieeectl->key_idx>= 0)
	{
		u16 key_idx = (u16)(ieeectl->key_idx);
		struct acx_key* key = &(adev->key[key_idx]);
		int wlhdr_len;
		if (key->enabled)
		{
			memcpy(ieeehdr->wep_iv, ((u8*)wireless_header) + wlhdr_len, 4);
		}
	}
*/
	if (IS_ACX111(adev)) {
		/* note that if !txdesc->do_auto, txrate->cur
		 ** has only one nonzero bit */
		txdesc->u.r2.rate111 = cpu_to_le16(rate_cur
						   /* WARNING: I was never able to make it work with prism54 AP.
						    ** It was falling down to 1Mbit where shortpre is not applicable,
						    ** and not working at all at "5,11 basic rates only" setting.
						    ** I even didn't see tx packets in radio packet capture.
						    ** Disabled for now --vda */
						   /*| ((clt->shortpre && clt->cur!=RATE111_1) ? RATE111_SHORTPRE : 0) */
		    );
#ifdef TODO_FIGURE_OUT_WHEN_TO_SET_THIS
		/* should add this to rate111 above as necessary */
		|(clt->pbcc511 ? RATE111_PBCC511 : 0)
#endif
		    hostdesc1->length = cpu_to_le16(len);
	} else {		/* ACX100 */
		u8 rate_100 = ieeectl->tx_rate;
//		txdesc->u.r1.rate = rate_100;
		write_slavemem8 (adev, (u32) &(txdesc->u.r1.rate), rate_100);

#ifdef TODO_FIGURE_OUT_WHEN_TO_SET_THIS
		if (clt->pbcc511) {
			if (n == RATE100_5 || n == RATE100_11)
				n |= RATE100_PBCC511;
		}

		if (clt->shortpre && (clt->cur != RATE111_1))
			SET_BIT(Ctl_8, DESC_CTL_SHORT_PREAMBLE);	/* set Short Preamble */
#endif
		/* set autodma and reclaim and 1st mpdu */
		SET_BIT(Ctl_8,
			DESC_CTL_AUTODMA | DESC_CTL_RECLAIM |
			DESC_CTL_FIRSTFRAG);
#if ACX_FRAGMENTATION
		/* SET_BIT(Ctl2_8, DESC_CTL2_MORE_FRAG); cannot set it unconditionally, needs to be set for all non-last fragments */
#endif
		hostdesc1->length = cpu_to_le16(wlhdr_len);
	}
	/* don't need to clean ack/rts statistics here, already
	 * done on descr cleanup */

	/* clears HOSTOWN and ACXDONE bits, thus telling that the descriptors
	 * are now owned by the acx100; do this as LAST operation */
	CLEAR_BIT(Ctl_8, DESC_CTL_ACXDONE_HOSTOWN);
	/* flush writes before we release hostdesc to the adapter here */
	wmb();
	CLEAR_BIT(hostdesc1->Ctl_16, cpu_to_le16(DESC_CTL_HOSTOWN));
	CLEAR_BIT(hostdesc2->Ctl_16, cpu_to_le16(DESC_CTL_HOSTOWN));

	/* write back modified flags */
	CLEAR_BIT(Ctl2_8, DESC_CTL2_WEP);
//	txdesc->Ctl2_8 = Ctl2_8;
	write_slavemem8 (adev, (u32) &(txdesc->Ctl2_8), Ctl2_8);
//	txdesc->Ctl_8 = Ctl_8;
	write_slavemem8 (adev, (u32) &(txdesc->Ctl_8), Ctl_8);

	/* unused: txdesc->tx_time = cpu_to_le32(jiffies); */

	/* flush writes before we tell the adapter that it's its turn now */
	mmiowb();
	write_reg16(adev, IO_ACX_INT_TRIG, INT_TRIG_TXPRC);
	write_flush(adev);
	/* log the packet content AFTER sending it,
	 * in order to not delay sending any further than absolutely needed
	 * Do separate logs for acx100/111 to have human-readable rates */
	memcpy(&(hostdesc1->txstatus.control),ieeectl,sizeof(struct ieee80211_tx_control));
	hostdesc1->skb = skb;
      end:
	FN_EXIT0;
}

static void
acxcs_l_process_rxdesc(acx_device_t *adev)
{
	register rxhostdesc_t *hostdesc;
	register rxdesc_t *rxdesc;
	unsigned count, tail;
	u32 addr;
	u8 Ctl_8;

	FN_ENTER;

printk("acx: rx_desc\n");

//!!!!	if (unlikely(acx_debug & L_BUFR))
//		log_rxbuffer(adev);

	/* First, have a loop to determine the first descriptor that's
	 * full, just in case there's a mismatch between our current
	 * rx_tail and the full descriptor we're supposed to handle. */
	tail = DRV_DATA(adev)->rx_tail;
	count = RX_CNT;
	while (1) {
		hostdesc = &DRV_DATA(adev)->rxhostdesc_start[tail];
		rxdesc = &DRV_DATA(adev)->rxdesc_start[tail];
		/* advance tail regardless of outcome of the below test */
		tail = (tail + 1) % RX_CNT;

		/*
		 * Unlike the PCI interface, where the ACX can write directly to
		 * the host descriptors, on the slave memory interface we have to
		 * pull these.  All we really need to do is check the Ctl_8 field
		 * in the rx descriptor on the ACX, which should be 0x11000000 if
		 * we should process it.
		 */
		Ctl_8 = hostdesc->Ctl_16 = read_slavemem8 (adev, (u32) &(rxdesc->Ctl_8));
                if ((Ctl_8 & DESC_CTL_HOSTOWN) &&
		    (Ctl_8 & DESC_CTL_ACXDONE))
		  break;		/* found it! */

		if (unlikely(!--count))	/* hmm, no luck: all descs empty, bail out */
			goto end;
	}

	/* now process descriptors, starting with the first we figured out */
	while (1) {
		log(L_BUFR, "rx: tail=%u Ctl_8=%02X\n",	tail, Ctl_8);
printk("rx: tail=%u Ctl_8=%02X\n",	tail, Ctl_8);
		/*
		 * If the ACX has CTL_RECLAIM set on this descriptor there
		 * is no buffer associated; it just wants us to tell it to
		 * reclaim the memory.
		 */
		if (!(Ctl_8 & DESC_CTL_RECLAIM)) {

		  /*
		 * slave interface - pull data now
		 */
		hostdesc->length = read_slavemem16 (adev, (u32) &(rxdesc->total_length));

		/*
		 * hostdesc->data is an rxbuffer_t, which includes header information,
		 * but the length in the data packet doesn't.  The header information
		 * takes up an additional 12 bytes, so add that to the length we copy.
		 */
		addr = read_slavemem32 (adev, (u32) &(rxdesc->ACXMemPtr));
		  if (addr) {
		    /*
		     * How can &(rxdesc->ACXMemPtr) above ever be zero?  Looks like we
		     * get that now and then - try to trap it for debug.
		     */
		    if (addr & 0xffff0000) {
		      printk("rxdesc 0x%08x\n", (u32) rxdesc);
//		      dump_acxmem (adev, 0, 0x10000);
		      panic ("Bad access!");
		    }
		  chaincopy_from_slavemem (adev, (u8 *) hostdesc->data, addr,
					   hostdesc->length +
					   (u32) &((rxbuffer_t *)0)->hdr_a3);
		acx_l_process_rxbuf(adev, hostdesc->data);
		  }
		}
		else {
		  printk ("rx reclaim only!\n");
		}

		hostdesc->Status = 0;

		/*
		 * Let the ACX know we're done.
		 */
		CLEAR_BIT (Ctl_8, DESC_CTL_HOSTOWN);
                SET_BIT (Ctl_8, DESC_CTL_HOSTDONE);
		SET_BIT (Ctl_8, DESC_CTL_RECLAIM);
		write_slavemem8 (adev, (u32) &rxdesc->Ctl_8, Ctl_8);

		/*
		 * Now tell the ACX we've finished with the receive buffer so 
		 * it can finish the reclaim.
		 */
		write_reg16 (adev, IO_ACX_INT_TRIG, INT_TRIG_RXPRC);

		/* ok, descriptor is handled, now check the next descriptor */
		hostdesc = &DRV_DATA(adev)->rxhostdesc_start[tail];
		rxdesc = &DRV_DATA(adev)->rxdesc_start[tail];

		Ctl_8 = hostdesc->Ctl_16 = read_slavemem8 (adev, (u32) &(rxdesc->Ctl_8));

		/* if next descriptor is empty, then bail out */
		if (!(Ctl_8 & DESC_CTL_HOSTOWN) || !(Ctl_8 & DESC_CTL_ACXDONE))
			break;

		tail = (tail + 1) % RX_CNT;
	}
end:
	DRV_DATA(adev)->rx_tail = tail;
	FN_EXIT0;
}


static const struct ieee80211_ops acxpcimcia_hw_ops = {
	.tx = acx_i_start_xmit,
	.conf_tx = acx_net_conf_tx,
	.add_interface = acx_add_interface,
	.remove_interface = acx_remove_interface,
	.start = acxcs_e_open,
	.configure_filter = acx_i_set_multicast_list,
	.stop = acxcs_e_close,
	.config = acx_net_config,
	.config_interface = acx_config_interface,
	.set_key = acx_net_set_key,
	.get_stats = acx_e_get_stats,
	.get_tx_stats = acx_net_get_tx_stats,
};

static acx_ops_t acx_pcimcia_ops = {
	// low level ops
	.write_reg16 = write_reg16,
	.write_reg32 = write_reg32,
	.read_reg16 = read_reg16,
	.read_reg32 = read_reg32,
	.read_reg8 = read_reg8,
	.set_regbits = set_regbits,
	.write_flush = write_flush,
	.clear_regbits = clear_regbits,
	.read_slavemem8 = read_slavemem8,
	.write_slavemem8 = write_slavemem8,
	//
	.get_dev = get_dev,
	.interrupt_tasklet = acxcs_interrupt_tasklet,
	.set_interrupt_mask = acxcs_set_interrupt_mask,
	.s_create_hostdesc_queues = acxcs_s_create_hostdesc_queues,
	.create_desc_queues = acxcs_create_desc_queues,
	.get_txdesc = get_txdesc,
	.l_get_txbuf = acxcs_l_get_txbuf,
	.l_tx_data = acxcs_l_tx_data,
	.l_process_rxdesc = acxcs_l_process_rxdesc,
	.s_upload_radio = acxcs_s_upload_radio,
	.s_issue_cmd_timeo = acxcs_s_issue_cmd_timeo_debug,
	.s_issue_cmd_timeo_debug = acxcs_s_issue_cmd_timeo_debug,
	.s_reset_dev = acxcs_s_reset_dev,	
};

static int acx_cs_probe(struct pcmcia_device *pdev)
{
//	local_info_t *local;
	acx111_ie_configoption_t co;
	int result = -EIO;
	int err;
	struct ieee80211_hw *ieee;
	DECLARE_MAC_BUF(mac);
	acx_device_t	*adev;

	DEBUG(0, "acx_attach()\n");

	ieee = ieee80211_alloc_hw(sizeof(acx_device_t), &acxpcimcia_hw_ops);
	if (!ieee) {
		printk("acx: could not allocate ieee80211 structure %s\n",
		       "!!!!"/*pcmcia_name(pdev)*/);
		goto fail_alloc_netdev;
	}
	ieee->flags &=	 ~IEEE80211_HW_RX_INCLUDES_FCS;
	ieee->queues = 1;

	/* (NB: memsets to 0 entire area) */
	if (!ieee) {
		printk("acx: could not allocate ieee structure %s\n",
		       "!!!"/*!!!!pci_name(pdev)*/);
		goto fail_alloc_netdev;
	}

	adev = ieee2adev(ieee);

	memset(adev, 0, sizeof(*adev));
	adev->drv_data = kzalloc(sizeof(cs_drv_data_t), GFP_KERNEL);
	memset(adev->drv_data, 0, sizeof(cs_drv_data_t));

	adev->ops = &acx_pcimcia_ops;
	adev->ieee = ieee;
	adev->dev_type = DEVTYPE_MEM;
	
	DRV_DATA(adev)->pdev = pdev;
	
	/** Set up our private interface **/
	spin_lock_init(&adev->lock);	/* initial state: unlocked */
	/* We do not start with downed sem: we want PARANOID_LOCKING to work */
	printk("mutex_init(&adev->mutex); // adev = 0x%px\n", adev);
	mutex_init(&adev->mutex);
	
// !!!!
	/* since nobody can see new netdev yet, we can as well
	 ** just _presume_ that we're under sem (instead of actually taking it): */
	/* acx_sem_lock(adev); */

        /* Interrupt setup */
        pdev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
        pdev->irq.IRQInfo1 = IRQ_LEVEL_ID;
        pdev->irq.Handler = acxcs_i_interrupt;
        pdev->irq.Instance = adev;

	/*
	  General socket configuration defaults can go here.  In this
	  client, we assume very little, and rely on the CIS for almost
	  everything.  In most clients, many details (i.e., number, sizes,
	  and attributes of IO windows) are fixed by the nature of the
	  device, and can be hard-wired here.
	*/
	pdev->conf.Attributes = CONF_ENABLE_IRQ;
	pdev->conf.IntType = INT_MEMORY_AND_IO;
	pdev->conf.Present = PRESENT_OPTION | PRESENT_COPY;
	
	pdev->priv = adev;

	if (acx_cs_config(pdev))
	  return -EIO;
	  
	printk("acx: after cs_config: adev->irq: %d assigned irq: %d\n", adev->irq, pdev->irq.AssignedIRQ);
	
	adev->irq = pdev->irq.AssignedIRQ;
	
	if (0 == adev->irq) {
		printk("acx: can't use IRQ 0\n");
		goto fail_irq;
	}

	printk("acx: using IRQ %d\n", adev->irq);
	  
	printk("acx: setting ieee80211 device...");
	
	SET_IEEE80211_DEV(ieee, &pdev->dev);
	
	acx_init_task_scheduler(adev);

	/* to find crashes due to weird driver access
	 * to unconfigured interface (ifup) */
	adev->mgmt_timer.function = (void (*)(unsigned long))0x0000dead;


#ifdef NONESSENTIAL_FEATURES
	acx_show_card_eeprom_id(adev);
#endif /* NONESSENTIAL_FEATURES */

	/* NB: read_reg() reads may return bogus data before reset_dev(),
	 * since the firmware which directly controls large parts of the I/O
	 * registers isn't initialized yet.
	 * acx100 seems to be more affected than acx111 */
	if (OK != adev->ops->s_reset_dev(adev))
		goto fail_reset;

	if (IS_ACX100(adev)) {
		/* ACX100: configopt struct in cmd mailbox - directly after reset */
		copy_from_slavemem (adev, (u8*) &co, (u32) DRV_DATA(adev)->cmd_area, sizeof (co));
//!!!!		memcpy_fromio(&co, DRV_DATA(adev)->cmd_area, sizeof(co));
	}

	if (OK != acx_s_init_mac(adev))
		goto fail_init_mac;

	if (IS_ACX111(adev)) {
		/* ACX111: configopt struct needs to be queried after full init */
		acx_s_interrogate(adev, &co, ACX111_IE_CONFIG_OPTIONS);
	}
/* TODO: merge them into one function, they are called just once and are the same for pci & usb */
	if (OK != acxcs_read_eeprom_byte(adev, 0x05, &adev->eeprom_version))
		goto fail_read_eeprom_version;

	acx_s_parse_configoption(adev, &co);
	acx_s_set_defaults(adev);
	acx_s_get_firmware_version(adev);	/* needs to be after acx_s_init_mac() */
	acx_display_hardware_details(adev);

	/* Register the card, AFTER everything else has been set up,
	 * since otherwise an ioctl could step on our feet due to
	 * firmware operations happening in parallel or uninitialized data */

	acx_proc_register_entries(ieee);

	/* Now we have our device, so make sure the kernel doesn't try
	 * to send packets even though we're not associated to a network yet */

	/* after register_netdev() userspace may start working with dev
	 * (in particular, on other CPUs), we only need to up the sem */
	/* acx_sem_unlock(adev); */

	printk("acx " ACX_RELEASE ": net device %s, driver compiled "
	       "against wireless extensions %d and Linux %s\n",
	       wiphy_name(adev->ieee->wiphy), WIRELESS_EXT, UTS_RELEASE);

//	MAC_COPY(adev->ieee->wiphy->perm_addr, adev->dev_addr);

	printk("MAC: %s\n", print_mac(mac, adev->dev_addr));
	
	SET_IEEE80211_PERM_ADDR(adev->ieee, adev->dev_addr);
	
	acx_setup_modes(adev);

	log(L_IRQ | L_INIT, "using IRQ %d\n", adev->irq);

/** done with board specific setup **/

	/* need to be able to restore PCI state after a suspend */
//#ifdef CONFIG_PM
//	pci_save_state(pdev);
//#endif

	
	err = ieee80211_register_hw(adev->ieee);
	if (OK != err) {
		printk("acx: ieee80211_register_hw() FAILED: %d\n", err);
		goto fail_register_netdev;
	}
#if CMD_DISCOVERY
	great_inquisitor(adev);
#endif

	result = OK;
	goto done;


      fail_init_mac:
      fail_read_eeprom_version:
      fail_reset:
      fail_alloc_netdev:
      fail_irq:
      fail_register_netdev:
	ieee80211_free_hw(ieee);
      done:
	FN_EXIT1(result);
	return result;
}

static void acx_cs_detach(struct pcmcia_device *link)
{
	DEBUG(0, "acx_detach(0x%p)\n", link);


//!!!!	if ( ((local_info_t*)link->priv)->ndev ) {
//		acxmem_e_close( ((local_info_t*)link->priv)->ndev );
//	}

	acx_cs_release(link);

//!!!!	((local_info_t*)link->priv)->ndev = NULL;

	kfree(link->priv);
} /* acx_detach */

int acx_init_netdev(acx_device_t *adev, int base_addr, int addr_size, int irq)
{
	const char *chip_name;
	int result = -EIO;
//	int err;
	u8 chip_type;
 
	FN_ENTER;

	/* FIXME: prism54 calls pci_set_mwi() here,
	 * should we do/support the same? */

	/* chiptype is u8 but id->driver_data is ulong
	** Works for now (possible values are 1 and 2) */
	chip_type = CHIPTYPE_ACX100;
	/* acx100 and acx111 have different PCI memory regions */
	if (chip_type == CHIPTYPE_ACX100) {
		chip_name = "ACX100";
	} else if (chip_type == CHIPTYPE_ACX111) {
		chip_name = "ACX111";
	} else {
		printk("acx: unknown chip type 0x%04X\n", chip_type);
		goto fail_unknown_chiptype;
	}

	printk("acx: found %s-based wireless network card\n", chip_name);
	
//	log(L_ANY, "initial debug setting is 0x%04X\n", acx_debug);

	printk (KERN_INFO "memwinbase=%lx memwinsize=%u\n",memwin.Base,memwin.Size);
	if (addr_size == 0 || irq == 0)
		goto fail_hw_params;
#if IW_HANDLER_VERSION <= 5
//	ndev->get_wireless_stats = &acx_e_get_wireless_stats;
#endif
	spin_lock_init(&adev->lock);	/* initial state: unlocked */
	spin_lock_init(&DRV_DATA(adev)->txbuf_lock);
	/* We do not start with downed sem: we want PARANOID_LOCKING to work */
	sema_init(&DRV_DATA(adev)->sem, 1);	/* initial state: 1 (upped) */

	/* since nobody can see new netdev yet, we can as well
	** just _presume_ that we're under sem (instead of actually taking it): */
	/* acx_sem_lock(adev); */

printk("adev: %X\n", (unsigned int)adev);
//return -EIO;	
	adev->dev_type = DEVTYPE_MEM;
	adev->chip_type = chip_type;
	adev->chip_name = chip_name;
	DRV_DATA(adev)->io = (CHIPTYPE_ACX100 == chip_type) ? IO_ACX100 : IO_ACX111;
	DRV_DATA(adev)->membase = (volatile u32 *) base_addr;
	DRV_DATA(adev)->iobase = (volatile u32 *) ioremap_nocache (base_addr, addr_size);
	if (((u32)DRV_DATA(adev)->iobase)&3) 
	    printk( " IO BASE of ACX is not Word aligned\n" );    
	/* to find crashes due to weird driver access
	 * to unconfigured interface (ifup) */
	
	printk("acx: io:%X iobase: %X membase: %X\n", (unsigned int)DRV_DATA(adev)->io, (unsigned int)DRV_DATA(adev)->iobase, (unsigned int)DRV_DATA(adev)->membase);
	
	adev->mgmt_timer.function = (void (*)(unsigned long))0x0000dead;

//#if defined(NONESSENTIAL_FEATURES)
	acx_show_card_eeprom_id(adev);
//#endif /* NONESSENTIAL_FEATURES */

//#ifdef SET_MODULE_OWNER
//	SET_MODULE_OWNER(ndev);
//#endif
	// need to fix that @@
//	SET_NETDEV_DEV(ndev, dev);

//	log(L_IRQ|L_INIT, "using IRQ %d\n", ndev->irq);

	/* ok, pci setup is finished, now start initializing the card */
	
	
	
	/*!!!!!!!!!!!!!!!!!!!!!!!!!*/

//!!!!	if (OK != acxmem_complete_hw_reset (adev))
///	  goto fail_reset;

	/*
	 * Set up default things for most of the card settings.
	 */
//	acx_s_set_defaults(adev);

	/* Register the card, AFTER everything else has been set up,
	 * since otherwise an ioctl could step on our feet due to
	 * firmware operations happening in parallel or uninitialized data */
//!!!!	err = register_netdev(ndev);
//	if (OK != err) {
//		printk("acx: register_netdev() FAILED: %d\n", err);
//		goto fail_register_netdev;
//	}

//!!!!	acx_proc_register_entries(ndev);

	/* Now we have our device, so make sure the kernel doesn't try
	 * to send packets even though we're not associated to a network yet */
//!!!!	acx_stop_queue(adev, "on probe");
//!!!!	acx_carrier_off(adev, "on probe");

	/*
	 * Set up a default monitor type so that poor combinations of initialization
	 * sequences in monitor mode don't end up destroying the hardware type.
	 */
	adev->monitor_type = ARPHRD_ETHER;

	/*
	 * Register to receive inetaddr notifier changes.  This will allow us to
	 * catch if the user changes the MAC address of the interface.
	 */
//!!!!	register_netdevice_notifier(&acx_netdev_notifier);

	/* after register_netdev() userspace may start working with dev
	 * (in particular, on other CPUs), we only need to up the sem */
	/* acx_sem_unlock(adev); */

//!!!!	printk("acx "ACX_RELEASE": net device %s, driver compiled "
//		"against wireless extensions %d and Linux %s\n",
//		ndev->name, WIRELESS_EXT, UTS_RELEASE);

//#if CMD_DISCOVERY
//	great_inquisitor(adev);
//#endif

	result = OK;
	goto done;

	/* error paths: undo everything in reverse order... */

//fail_register_netdev:
//!!!!	acxmem_s_delete_dma_regions(adev);

fail_reset:
fail_hw_params:
//	free_netdev(ndev);
fail_unknown_chiptype:


done:
	FN_EXIT1(result);
	return result;
}

int acxcs_read_eeprom_byte(acx_device_t * adev, u32 addr, u8 * charbuf)
{
	int result;
	int count;

	FN_ENTER;

	write_reg32(adev, IO_ACX_EEPROM_CFG, 0);
	write_reg32(adev, IO_ACX_EEPROM_ADDR, addr);
	write_flush(adev);
	write_reg32(adev, IO_ACX_EEPROM_CTL, 2);

	count = 0xffff;
	while (read_reg16(adev, IO_ACX_EEPROM_CTL)) {
		/* scheduling away instead of CPU burning loop
		 * doesn't seem to work here at all:
		 * awful delay, sometimes also failure.
		 * Doesn't matter anyway (only small delay). */
		if (unlikely(!--count)) {
			printk("%s: timeout waiting for EEPROM read\n",
			       "!!!"/*!!!!wiphy_name(adev->ieee->wiphy)*/);
			result = NOT_OK;
			goto fail;
		}
		cpu_relax();
	}

	*charbuf = read_reg8(adev, IO_ACX_EEPROM_DATA);
	log(L_DEBUG, "EEPROM at 0x%04X = 0x%02X\n", addr, *charbuf);
	result = OK;

      fail:
	FN_EXIT1(result);
	return result;
}


/*======================================================================
  
  After a card is removed, acx_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.
  
  ======================================================================*/

//static 
void acx_cs_release(struct pcmcia_device *link)
{
	DEBUG(0, "acx_release(0x%p)\n", link);
//!!!!	acxcs_e_remove(link);
	pcmcia_disable_device(link);
}

static int acx_cs_suspend(struct pcmcia_device *link)
{
//	acx_device_t *adev = link->priv;

//	pm_message_t state;
//!!!!	acxmem_e_suspend ( adev, state);
	/* Already done in suspend
	 * netif_device_detach(local->ndev); */

	return 0;
}

static int acx_cs_resume(struct pcmcia_device *link)
{
	acx_device_t *adev = link->priv;
	struct ieee80211_hw *hw = adev->ieee;
printk("acx: resume not supported\n");
return OK;

	FN_ENTER;

	printk("acx: resume handler is experimental!\n");
	printk("rsm: got dev %p\n", hw);


	adev = ieee2adev(hw);
	printk("rsm: got adev %p\n", adev);

	acx_sem_lock(adev);

//	pci_set_power_state(pdev, PCI_D0);
//	printk("rsm: power state PCI_D0 set\n");
//	pci_restore_state(pdev);
//	printk("rsm: PCI state restored\n");

	if (OK != acxpci_s_reset_dev(adev))
		goto end_unlock;
	printk("rsm: device reset done\n");
	if (OK != acx_s_init_mac(adev))
		goto end_unlock;
	printk("rsm: init MAC done\n");

	acxcs_s_up(hw);


	/* now even reload all card parameters as they were before suspend,
	 * and possibly be back in the network again already :-) */
	if (ACX_STATE_IFACE_UP & adev->dev_state_mask) {
		adev->set_mask = GETSET_ALL;
		acx_s_update_card_settings(adev);
		printk("rsm: settings updated\n");
	}
	ieee80211_register_hw(hw);
	printk("rsm: device attached\n");

      end_unlock:
	acx_sem_unlock(adev);
	/* we need to return OK here anyway, right? */
	FN_EXIT0;
	return OK;
}

static struct pcmcia_device_id acx_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0097, 0x8402),
        PCMCIA_DEVICE_MANF_CARD(0x0250, 0xb001),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, acx_ids);




static struct pcmcia_driver acx_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
	    .name	= "acx_cs",
	},
	.probe		= acx_cs_probe,
	.remove		= acx_cs_detach,
	.id_table 	= acx_ids,
//	.suspend	= acx_cs_suspend,
//	.resume		= acx_cs_resume,
};

int acx_cs_init(void)
{
        /* return success if at least one succeeded */
	DEBUG(0, "acxcs_init()\n");
	return pcmcia_register_driver(&acx_driver);
}

void acx_cs_cleanup(void)
{
	pcmcia_unregister_driver(&acx_driver);
}


MODULE_DESCRIPTION( "ACX Cardbus Driver" );
MODULE_LICENSE( "GPL" );
