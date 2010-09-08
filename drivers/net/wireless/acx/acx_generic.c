#include <linux/version.h>

#include <linux/vmalloc.h>

//
#include "acx.h"
#include "acx_generic.h"

inline void write_reg32(acx_device_t *adev, unsigned int offset, u32 val)
{
    adev->ops->write_reg32(adev, offset, val);
}

inline u32 read_reg32(acx_device_t *adev, unsigned int offset)
{
    return adev->ops->read_reg32(adev, offset);
}

inline void set_regbits (acx_device_t *adev, unsigned int offset, u32 bits)
{
    adev->ops->set_regbits(adev, offset, bits);
}

inline void clear_regbits (acx_device_t *adev, unsigned int offset, u32 bits)
{
    adev->ops->clear_regbits(adev, offset, bits);
}

inline void write_flush(acx_device_t *adev)
{
    adev->ops->write_flush(adev);
}

inline u16 read_reg16(acx_device_t *adev, unsigned int offset)
{
    return adev->ops->read_reg16(adev, offset);
}

inline void write_reg16(acx_device_t *adev, unsigned int offset, u16 val)
{
    return adev->ops->write_reg16(adev, offset, val);
}

inline u8 read_reg8(acx_device_t *adev, unsigned int offset)
{
    return adev->ops->read_reg8(adev, offset);
}

#define NO_AUTO_INCREMENT	1

void
acx_l_reset_mac(acx_device_t *adev)
{
  int count;
	FN_ENTER;

	/* halt eCPU */
	set_regbits (adev, IO_ACX_ECPU_CTRL, 0x1);

	/* now do soft reset of eCPU, set bit */
	set_regbits (adev, IO_ACX_SOFT_RESET, 0x1);
	log(L_DEBUG, "%s: enable soft reset...\n", __func__);

	/* Windows driver sleeps here for a while with this sequence */
	for (count = 0; count < 200; count++) {
	  udelay (50);
	}

	/* now clear bit again: deassert eCPU reset */
	log(L_DEBUG, "%s: disable soft reset and go to init mode...\n", __func__);
	clear_regbits (adev, IO_ACX_SOFT_RESET, 0x1);

	/* now start a burst read from initial EEPROM */
	set_regbits (adev, IO_ACX_EE_START, 0x1);

	/*
	 * Windows driver sleeps here for a while with this sequence
	 */
	for (count = 0; count < 200; count++) {
	  udelay (50);
	}

	/* Windows driver writes 0x10000 to register 0x808 here */
	
	write_reg32 (adev, 0x808, 0x10000);

	FN_EXIT0;
}

/***********************************************************************
** acxpci_s_validate_fw
**
** Compare the firmware image given with
** the firmware image written into the card.
**
** Arguments:
**	adev		wlan device structure
**   fw_image  firmware image.
**
** Returns:
**	NOT_OK	firmware image corrupted or not correctly written
**	OK	success
**
** Origin: Standard csum + Read IO
*/
int acx_s_validate_fw(acx_device_t * adev, const firmware_image_t *fw_image,
		     u32 offset)
{
	u32 sum, v32, w32;
	int len, size;
	int result = OK;
	/* we skip the first four bytes which contain the control sum */
	const u8 *p = (u8 *) fw_image + 4;

	FN_ENTER;

	/* start the image checksum by adding the image size value */
	sum = p[0] + p[1] + p[2] + p[3];
	p += 4;

	write_reg32(adev, IO_ACX_SLV_END_CTL, 0);

#if NO_AUTO_INCREMENT
	write_reg32(adev, IO_ACX_SLV_MEM_CTL, 0);	/* use basic mode */
#else
	write_reg32(adev, IO_ACX_SLV_MEM_CTL, 1);	/* use autoincrement mode */
	write_reg32(adev, IO_ACX_SLV_MEM_ADDR, offset);	/* configure start address */
#endif

	len = 0;
	size = le32_to_cpu(fw_image->size) & (~3);

	while (likely(len < size)) {
		v32 = be32_to_cpu(*(u32 *) p);
		p += 4;
		len += 4;

#if NO_AUTO_INCREMENT
		write_reg32(adev, IO_ACX_SLV_MEM_ADDR, offset + len - 4);
#endif
		w32 = read_reg32(adev, IO_ACX_SLV_MEM_DATA);

		if (unlikely(w32 != v32)) {
			printk("acx: FATAL: firmware upload: "
			       "data parts at offset %d don't match (0x%08X vs. 0x%08X)! "
			       "I/O timing issues or defective memory, with DWL-xx0+? "
			       "ACX_IO_WIDTH=16 may help. Please report\n",
			       len, v32, w32);
			result = NOT_OK;
			break;
		}

		sum +=
		    (u8) w32 + (u8) (w32 >> 8) + (u8) (w32 >> 16) +
		    (u8) (w32 >> 24);
	}

	/* sum control verification */
	if (result != NOT_OK) {
		if (sum != le32_to_cpu(fw_image->chksum)) {
			printk("acx: FATAL: firmware upload: "
			       "checksums don't match!\n");
			result = NOT_OK;
		}
	}

	FN_EXIT1(result);
	return result;
}

int acx_s_upload_fw(acx_device_t * adev)
{
	firmware_image_t *fw_image = NULL;
	int res = NOT_OK;
	int try;
	u32 file_size;
	char filename[sizeof("tiacx1NNcNN")];

	FN_ENTER;

	/* print exact chipset and radio ID to make sure people
	 * really get a clue on which files exactly they need to provide.
	 * Firmware loading is a frequent end-user PITA with these chipsets.
	 */
	printk( "acx: need firmware for acx1%02d chipset with radio ID %02X\n"
		"Please provide via firmware hotplug:\n"
		"either combined firmware (single file named 'tiacx1%02dc%02X')\n"
		"or two files (base firmware file 'tiacx1%02d' "
		"+ radio fw 'tiacx1%02dr%02X')\n",
		IS_ACX111(adev)*11, adev->radio_type,
		IS_ACX111(adev)*11, adev->radio_type,
		IS_ACX111(adev)*11,
		IS_ACX111(adev)*11, adev->radio_type
		);

	/* print exact chipset and radio ID to make sure people really get a clue on which files exactly they are supposed to provide,
	 * since firmware loading is the biggest enduser PITA with these chipsets.
	 * Not printing radio ID in 0xHEX in order to not confuse them into wrong file naming */
	printk(	"acx: need to load firmware for acx1%02d chipset with radio ID %02x, please provide via firmware hotplug:\n"
		"acx: either one file only (<c>ombined firmware image file, radio-specific) or two files (radio-less base image file *plus* separate <r>adio-specific extension file)\n",
		IS_ACX111(adev)*11, adev->radio_type);

	/* Try combined, then main image */
	adev->need_radio_fw = 0;
	snprintf(filename, sizeof(filename), "tiacx1%02dc%02X",
		 IS_ACX111(adev) * 11, adev->radio_type);

	fw_image = acx_s_read_fw(adev, filename, &file_size);
	if (!fw_image) {
		adev->need_radio_fw = 1;
		filename[sizeof("tiacx1NN") - 1] = '\0';
		fw_image =
		    acx_s_read_fw(adev, filename, &file_size);
		if (!fw_image) {
			FN_EXIT1(NOT_OK);
			return NOT_OK;
		}
	}

	for (try = 1; try <= 5; try++) {
		res = acx_s_write_fw(adev, fw_image, 0);
		log(L_DEBUG | L_INIT, "acx_write_fw (main/combined): %d\n", res);
		if (OK == res) {
			res = acx_s_validate_fw(adev, fw_image, 0);
			log(L_DEBUG | L_INIT, "acx_validate_fw "
			    		"(main/combined): %d\n", res);
		}

		if (OK == res) {
			SET_BIT(adev->dev_state_mask, ACX_STATE_FW_LOADED);
			break;
		}
		printk("acx: firmware upload attempt #%d FAILED, "
		       "retrying...\n", try);
		acx_s_mwait(1000);	/* better wait for a while... */
	}

	vfree(fw_image);

	FN_EXIT1(res);
	return res;
}

/***********************************************************************
** acxpci_s_write_fw
**
** Write the firmware image into the card.
**
** Arguments:
**	adev		wlan device structure
**	fw_image	firmware image.
**
** Returns:
**	1	firmware image corrupted
**	0	success
**
** Standard csum implementation + write to IO
*/
int acx_s_write_fw(acx_device_t * adev, const firmware_image_t *fw_image,
		  u32 offset)
{
	int len, size;
	u32 sum, v32;
	/* we skip the first four bytes which contain the control sum */

	const u8 *p = (u8 *) fw_image + 4;

	FN_ENTER;

	/* start the image checksum by adding the image size value */
	sum = p[0] + p[1] + p[2] + p[3];
	p += 4;

	write_reg32(adev, IO_ACX_SLV_END_CTL, 0);

#if NO_AUTO_INCREMENT
	write_reg32(adev, IO_ACX_SLV_MEM_CTL, 0);	/* use basic mode */
#else
	write_reg32(adev, IO_ACX_SLV_MEM_CTL, 1);	/* use autoincrement mode */
	write_reg32(adev, IO_ACX_SLV_MEM_ADDR, offset);	/* configure start address */
	write_flush(adev);
#endif

	len = 0;
	size = le32_to_cpu(fw_image->size) & (~3);

	while (likely(len < size)) {
		v32 = be32_to_cpu(*(u32 *) p);
		sum += p[0] + p[1] + p[2] + p[3];
		p += 4;
		len += 4;

#if NO_AUTO_INCREMENT
		write_reg32(adev, IO_ACX_SLV_MEM_ADDR, offset + len - 4);
		write_flush(adev);
#endif
		write_reg32(adev, IO_ACX_SLV_MEM_DATA, v32);
	}

	log(L_DEBUG, "firmware written, size:%d sum1:%x sum2:%x\n",
	    size, sum, le32_to_cpu(fw_image->chksum));

	/* compare our checksum with the stored image checksum */
	FN_EXIT1(sum != le32_to_cpu(fw_image->chksum));
	return (sum != le32_to_cpu(fw_image->chksum));
}

/***********************************************************************
** acxpci_s_verify_init
*/
int acx_s_verify_init(acx_device_t * adev)
{
	int result = NOT_OK;
	unsigned long timeout;

	FN_ENTER;

	timeout = jiffies + 2 * HZ;
	for (;;) {
		u16 irqstat = read_reg16(adev, IO_ACX_IRQ_STATUS_NON_DES);
		if (irqstat & HOST_INT_FCS_THRESHOLD) {
			result = OK;
			write_reg16(adev, IO_ACX_IRQ_ACK,
				    HOST_INT_FCS_THRESHOLD);
			break;
		}
		if (time_after(jiffies, timeout))
			break;
		/* Init may take up to ~0.5 sec total */
		acx_s_mwait(50);
	}

	FN_EXIT1(result);
	return result;
}

/***********************************************************************
** acxmem_s_read_phy_reg
**
** Messing with rx/tx disabling and enabling here
** (write_reg32(adev, IO_ACX_ENABLE, 0b000000xx)) kills traffic
*/
int
acxgen_s_read_phy_reg(acx_device_t *adev, u32 reg, u8 *charbuf)
{
	int result = NOT_OK;
	int count;

	FN_ENTER;

	write_reg32(adev, IO_ACX_PHY_ADDR, reg);
	write_flush(adev);
	write_reg32(adev, IO_ACX_PHY_CTL, 2);

	count = 0xffff;
	while (read_reg32(adev, IO_ACX_PHY_CTL)) {
		/* scheduling away instead of CPU burning loop
		 * doesn't seem to work here at all:
		 * awful delay, sometimes also failure.
		 * Doesn't matter anyway (only small delay). */
		if (unlikely(!--count)) {
			printk("%s: timeout waiting for phy read\n",
							wiphy_name(adev->ieee->wiphy));
			*charbuf = 0;
			goto fail;
		}
		cpu_relax();
	}

	log(L_DEBUG, "count was %u\n", count);
	*charbuf = read_reg8(adev, IO_ACX_PHY_DATA);

	log(L_DEBUG, "radio PHY at 0x%04X = 0x%02X\n", *charbuf, reg);
	result = OK;
	goto fail; /* silence compiler warning */
fail:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
*/
int
acxgen_s_write_phy_reg(acx_device_t *adev, u32 reg, u8 value)
{
        int count;
	FN_ENTER;

	/* mprusko said that 32bit accesses result in distorted sensitivity
	 * on his card. Unconfirmed, looks like it's not true (most likely since we
	 * now properly flush writes). */
	write_reg32(adev, IO_ACX_PHY_DATA, value);
	write_reg32(adev, IO_ACX_PHY_ADDR, reg);
	write_flush(adev);
	write_reg32(adev, IO_ACX_PHY_CTL, 1);
	write_flush(adev);

	count = 0xffff;
	while (read_reg32(adev, IO_ACX_PHY_CTL)) {
		/* scheduling away instead of CPU burning loop
		 * doesn't seem to work here at all:
		 * awful delay, sometimes also failure.
		 * Doesn't matter anyway (only small delay). */
		if (unlikely(!--count)) {
			printk("%s: timeout waiting for phy read\n",
							wiphy_name(adev->ieee->wiphy));
			goto fail;
		}
		cpu_relax();
	}

	log(L_DEBUG, "radio PHY write 0x%02X at 0x%04X\n", value, reg);
 fail:
	FN_EXIT1(OK);
	return OK;
}

/***************************************************************
** acxpci_l_alloc_tx
** Actually returns a txdesc_t* ptr
**
** FIXME: in case of fragments, should allocate multiple descrs
** after figuring out how many we need and whether we still have
** sufficiently many.
*/
tx_t *acxgen_l_alloc_tx(acx_device_t * adev)
{
	struct txdesc *txdesc;
	unsigned head;
	u8 ctl8;

	FN_ENTER;
printk("acx: l_alloc_tx :%X\n", (unsigned int)adev);

	if (unlikely(!adev->tx_free)) {
		printk("acx: BUG: no free txdesc left\n");
		txdesc = NULL;
		goto end;
	}

	head = adev->tx_head;
	txdesc = adev->ops->get_txdesc(adev, head);
printk("acx: txdesc: %X\n", (unsigned int) txdesc);
//	ctl8 = txdesc->Ctl_8;

	ctl8 = adev->ops->read_slavemem8 (adev, (u32) &(txdesc->Ctl_8));

printk("acx: Ctl_8: %u\n", (unsigned int) ctl8);
	

	/* 2005-10-11: there were several bug reports on this happening
	 ** but now cause seems to be understood & fixed */
	if (unlikely(DESC_CTL_HOSTOWN != (ctl8 & DESC_CTL_ACXDONE_HOSTOWN))) {
		/* whoops, descr at current index is not free, so probably
		 * ring buffer already full */
		printk("acx: BUG: tx_head:%d Ctl8:0x%02X - failed to find "
		       "free txdesc\n", head, ctl8);
		txdesc = NULL;
		goto end;
	}

	/* Needed in case txdesc won't be eventually submitted for tx */
//	txdesc->Ctl_8 = DESC_CTL_ACXDONE_HOSTOWN;
	adev->ops->write_slavemem8 (adev, (u32) &(txdesc->Ctl_8), DESC_CTL_ACXDONE_HOSTOWN);

	adev->tx_free--;
	log(L_BUFT, "tx: got desc %u, %u remain\n", head, adev->tx_free);
	/* Keep a few free descs between head and tail of tx ring.
	 ** It is not absolutely needed, just feels safer */
	if (adev->tx_free < TX_STOP_QUEUE) {
		log(L_BUF, "stop queue (%u tx desc left)\n", adev->tx_free);
		acx_stop_queue(adev->ieee, NULL);
	}

	/* returning current descriptor, so advance to next free one */
	adev->tx_head = (head + 1) % TX_CNT;
      end:
	FN_EXIT0;

	return (tx_t *) txdesc;
}

/***********************************************************************
*/
int acx100gen_s_set_tx_level(acx_device_t * adev, u8 level_dbm)
{
	/* since it can be assumed that at least the Maxim radio has a
	 * maximum power output of 20dBm and since it also can be
	 * assumed that these values drive the DAC responsible for
	 * setting the linear Tx level, I'd guess that these values
	 * should be the corresponding linear values for a dBm value,
	 * in other words: calculate the values from that formula:
	 * Y [dBm] = 10 * log (X [mW])
	 * then scale the 0..63 value range onto the 1..100mW range (0..20 dBm)
	 * and you're done...
	 * Hopefully that's ok, but you never know if we're actually
	 * right... (especially since Windows XP doesn't seem to show
	 * actual Tx dBm values :-P) */

	/* NOTE: on Maxim, value 30 IS 30mW, and value 10 IS 10mW - so the
	 * values are EXACTLY mW!!! Not sure about RFMD and others,
	 * though... */
	static const u8 dbm2val_maxim[21] = {
		63, 63, 63, 62,
		61, 61, 60, 60,
		59, 58, 57, 55,
		53, 50, 47, 43,
		38, 31, 23, 13,
		0
	};
	static const u8 dbm2val_rfmd[21] = {
		0, 0, 0, 1,
		2, 2, 3, 3,
		4, 5, 6, 8,
		10, 13, 16, 20,
		25, 32, 41, 50,
		63
	};
	const u8 *table;

	switch (adev->radio_type) {
	case RADIO_MAXIM_0D:
		table = &dbm2val_maxim[0];
		break;
	case RADIO_RFMD_11:
	case RADIO_RALINK_15:
		table = &dbm2val_rfmd[0];
		break;
	default:
		printk("%s: unknown/unsupported radio type, "
		       "cannot modify tx power level yet!\n", wiphy_name(adev->ieee->wiphy));
		return NOT_OK;
	}
	printk("%s: changing radio power level to %u dBm (%u)\n",
	       wiphy_name(adev->ieee->wiphy), level_dbm, table[level_dbm]);
	acxgen_s_write_phy_reg(adev, 0x11, table[level_dbm]);
	return OK;
}
