#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/irq.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/semaphore.h> 
#include <asm/intel-mid.h>
#include <asm/intel_mid_hsu.h>

#include <linux/acpi.h>
#include <linux/acpi_gpio.h>

#define GPIO_S0_71 71
#define WL_DEV_EN 150
#define S_IWUGO         (S_IWUSR|S_IWGRP|S_IWOTH)   /* 全部用户写 */

struct att_dev{  
    struct platform_device *pdev;  
    struct kobject *kobj;
    int bt_on;
	int wifi_on;
	int gps_on;
}; 

static struct att_dev *my_dev = NULL;

int is_bt_off()
{
	my_dev->wifi_on = gpio_get_value(WL_DEV_EN);
	if(my_dev && !my_dev->bt_on && !my_dev->wifi_on && !my_dev->gps_on)
		return 1;
	else
		return 0;
}


static ssize_t bt_show(struct device *ddev,
  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	printk(KERN_ALERT"%s\n",__func__);
	ret = my_dev->bt_on;
	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t bt_store(struct device *ddev,
  struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long state = simple_strtoul(buf, NULL, 10);
	printk(KERN_ALERT"%s:%d\n",__func__,state);
	my_dev->bt_on = state;
		
	return size;
}

static ssize_t gps_show(struct device *dev,
  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ret = my_dev->gps_on;
	printk(KERN_ALERT"%s : %d\n",__func__,ret);
	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t gps_store(struct device *dev,
  struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned long state = simple_strtoul(buf, NULL, 10);
	printk(KERN_ALERT"%s:%d\n",__func__,state);
	my_dev->gps_on = state;
		
	return size;
}
 
static DEVICE_ATTR(bt_on,S_IRUGO | S_IWUGO,bt_show,bt_store);
static DEVICE_ATTR(gps_on,S_IRUGO | S_IWUGO,gps_show,gps_store);  
  
static int att_probe(struct platform_device *ppdev){  
    int ret;  

	my_dev->bt_on = 0;
    my_dev->wifi_on = 0;
    my_dev->gps_on = 0;
      
    my_dev->kobj = kobject_create_and_add("attkobj", NULL);   
    if(my_dev->kobj == NULL){  
        ret = -ENOMEM;  
        goto kobj_err;  
    } 

    ret = sysfs_create_file(&my_dev->pdev->dev.kobj,&dev_attr_bt_on.attr);  
    if(ret < 0){  
        goto file_err;  
    }
    ret = sysfs_create_file(&my_dev->pdev->dev.kobj,&dev_attr_gps_on.attr);  
    if(ret < 0){  
        goto file_err;  
    }
 
    return 0;  
  
file_err:  
     kobject_del(my_dev->kobj);    
kobj_err:  
    return ret;  
}  

int att_suspend(struct platform_device *pdev, pm_message_t state)
{
	if(is_bt_off()){
		printk("%s read gpio gpio 71: %d\n", __func__, gpio_get_value(GPIO_S0_71));
//		gpio_direction_output(GPIO_S0_71, 0);
		gpio_set_value(GPIO_S0_71, 0);			
	}
	return 0;
}

int att_resume(struct platform_device *pdev)
{
	if(is_bt_off()){
		printk("%s read gpio gpio 71: %d\n", __func__, gpio_get_value(GPIO_S0_71));
//		gpio_direction_output(GPIO_S0_71, 1);
		gpio_set_value(GPIO_S0_71, 1);				
	}
	
	return 0;
}
  
static struct platform_driver att_driver = {  
    .probe = att_probe,
	.suspend = att_suspend,
	.resume = att_resume,  
    .driver = {  
        .owner = THIS_MODULE,  
        .name = "att_test",  
    },  
};  
  
static int __init att_init(void)  
{  
    int ret;  
    my_dev = kzalloc(sizeof(struct att_dev),GFP_KERNEL);
    printk(KERN_ALERT"%s : kzalloc dev\n",__func__);  
    if(my_dev == NULL){  
        printk("%s get dev memory error\n",__func__);  
        return -ENOMEM;  
    }  
    memset(my_dev,0,sizeof(struct att_dev));
    my_dev->pdev = platform_device_register_simple("att_test", -1, NULL, 0);
    if(IS_ERR(my_dev->pdev)){  
        PTR_ERR(my_dev->pdev);   
        printk("%s pdev error\n",__func__);  
        return -1;  
    }  
  
    ret = platform_driver_register(&att_driver);  
    if(ret < 0){  
        printk("%s register driver error\n",__func__);  
        return ret;  
    }  
  
    return 0;  
}  
  
static void __exit att_exit(void)  
{  
      
} 

module_init(att_init);
module_exit(att_exit);

MODULE_AUTHOR("vector");
MODULE_LICENSE("GPL");
