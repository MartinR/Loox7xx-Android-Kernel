#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <mach/irqs.h>
#include <asm/uaccess.h>
#include <mach/loox720-cpld.h>

#include <linux/seq_file.h>

#define PROCFS_NAME	"cpld"

static struct proc_dir_entry *proc_intf;

#define PROCFS_MAX_SIZE		20

static char procfs_buffer[PROCFS_MAX_SIZE];
static unsigned long procfs_buffer_size = 0;

void loox720_set_leds_cache(u8 value);

static void handle_request(void)
{
	char *p = NULL;
	unsigned long id = simple_strtoul(procfs_buffer+2, &p, 10);
	switch(procfs_buffer[0]) {
		case 'L':
			loox720_egpio_set_bit(id, 1);
			printk(KERN_ERR "CPLD-edit: CPLD bit %lu set high\n", id);
			break;
		case 'l':
			loox720_egpio_set_bit(id, 0);
			printk(KERN_ERR "CPLD-edit: CPLD bit %lu set low\n", id);
			break;
		case 'r':
			printk(KERN_ERR "CPLD-edit: Value of CPLD reg %lu is %08X\n", id, loox720_cpld_reg_read(id));
			break;
		case '0':
			loox720_cpld_reg_write(0, id);
			break;
		case 'd':
			printk(KERN_ERR "CPLD-edit: setting led cache to %02X\n", id);
			loox720_set_leds_cache(id);
			break;
		default:
			printk(KERN_ERR "CPLD-edit: Unknown request\n");
			break;
	}
}

static int procfile_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	procfs_buffer_size = count;
	if (procfs_buffer_size > PROCFS_MAX_SIZE ) {
		procfs_buffer_size = PROCFS_MAX_SIZE;
	}
	
	/* write data to the buffer */
	if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
		return -EFAULT;
	}
	
	handle_request();
	
	return procfs_buffer_size;
}


static int __init cpld_edit_init(void)
{
	proc_intf = create_proc_entry(PROCFS_NAME, 0644, NULL);
	if (proc_intf == NULL) {
		remove_proc_entry(PROCFS_NAME, NULL);
		printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", PROCFS_NAME);
		return -ENOMEM;
	}
	
//	proc_intf->read_proc = procfile_read;
	proc_intf->write_proc = procfile_write;
//	proc_intf->owner     = THIS_MODULE;
	proc_intf->mode      = S_IFREG | S_IRUGO;
	proc_intf->uid       = 0;
	proc_intf->gid       = 0;
	proc_intf->size      = 37;
	
	printk(KERN_INFO "/proc/%s created\n", PROCFS_NAME);
	
        return 0;
}

static void __exit cpld_edit_exit(void)
{
	remove_proc_entry(PROCFS_NAME, NULL);
	printk(KERN_INFO "/proc/%s removed\n", PROCFS_NAME);
}


/*** Some more stuff ***/
module_init(cpld_edit_init);
module_exit(cpld_edit_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vladimir \"Farcaller\" Pouzanov <farcaller@gmail.com>");
MODULE_DESCRIPTION("CPLD editor for Loox 720, experimental!");

