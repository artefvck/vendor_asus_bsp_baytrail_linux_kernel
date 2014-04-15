#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/gpio.h>

#define FAC_INT "FAC_INT"

long fac_int_ioctl(struct file *filp, unsigned int cmd, unsigned long data)
{
	switch(cmd)
	{
		case 0	:	return gpio_get_value(data);
		default	:	printk("no default cmd\n");
					break;
	}
	return 0;
}

struct file_operations fac_int_fops= {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fac_int_ioctl,
};

struct miscdevice fac_int_misc_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= FAC_INT,
	.fops	= &fac_int_fops,
};

static int fac_int_init(void)
{
	int rv;
	rv = misc_register(&fac_int_misc_dev);
	if(rv != 0)
	{
		printk(KERN_ERR "Error registering device\n");
	}
	return rv;
}

static void fac_int_exit(void)
{
	printk("exit factory interface\n");
}

module_init(fac_int_init);
module_exit(fac_int_exit);
