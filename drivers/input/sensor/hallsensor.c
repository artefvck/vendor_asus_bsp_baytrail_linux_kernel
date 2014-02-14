#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/hwmon-sysfs.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <asm/intel-mid.h>
#include <linux/earlysuspend.h>
#include <linux/timer.h>
#include <linux/mfd/intel_mid_pmic.h>
#include <asm/intel_scu_pmic.h>
#include <asm/intel_vlv2.h>
#define DEVICE_NAME "gpio_hall"

#define MIRQLVL1 0x0e
#define GPIO1P0CTLO 0x3b
#define GPIO1P0CTLI 0x43
#define MGPIO1P0IRQS0 0x1A
#define MGPIO1P0IRQSX 0x1C
#define MASK 0
#define UNMASK 1

//#define HALL_DEBUG
#ifdef HALL_DEBUG
#define p_debug(format, ...) printk(format, ## __VA_ARGS__)
#else
#define p_debug(format, ...) do {} while (0)
#endif


#define DRIVER_NAME "hall_sensor"
enum {
	GPIO0P0 = 0,
		GPIO0P1,
		GPIO0P2,
		GPIO0P3,
		GPIO0P4,
		GPIO0P5,
		GPIO0P6,
		GPIO0P7,
		GPIO_HALL,
		GPIO1P1,
		GPIO1P2,
		GPIO1P3,
		GPIO1P4,
		GPIO1P5,
		GPIO1P6,
		GPIO1P7
};

static struct kobject *hall_sensor_kobj;
static struct platform_device *pdev;
static bool suspending = false;
static struct input_device_id mID[] = {
        { .driver_info = 1 },		//scan all device to match hall sensor
        { },
};
static int lid_probe(struct platform_device *pdev);
static int lid_suspend(struct platform_device *pdev, pm_message_t state) ;
static struct hall_sensor_str {

 	int irq;
	int status;
	int gpio;
	int enable; 
	spinlock_t mHallSensorLock;
	struct wake_lock wake_lock;
	struct input_dev *lid_indev;
}* hall_sensor_dev;
static struct resource hall_resources[] = {
	{
		.name  = "HALL",
		.start = VV_PMIC_GPIO_IRQBASE+GPIO_HALL,
		.end   = VV_PMIC_GPIO_IRQBASE+GPIO_HALL,
		.flags = IORESOURCE_IRQ,
	},
};

int lid_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id){
	p_debug("\n[%s]lid_connect!\n",DRIVER_NAME);
	return 0;
}
//used when reporting
void lid_event(struct input_handle *handle, unsigned int type, unsigned int code, int value){
	p_debug("\n[%s]lid_event!type=%d,code=%d,value=%d\n",
		DRIVER_NAME,type,code,value);
	if(type == EV_SW && code == SW_LID)
	{
		p_debug("\n[%s]lid_event!type== ev_sw && code ==sw_lid\n",
			DRIVER_NAME);
		//if sw ==1 && status==1 OR sw==0 && status == 0
		p_debug("\n[%s]lid_event!lid_indev->sw=%lx h,hall_sensor_dev->status=%lx h,test_bit(code, hall_sensor_dev->lid_indev->sw)=%x h,!!test_bit(code, hall_sensor_dev->lid_indev->sw)=%x h,\n",
			DRIVER_NAME,*(hall_sensor_dev->lid_indev->sw),
			hall_sensor_dev->status,test_bit(code, hall_sensor_dev->lid_indev->sw),
			!!test_bit(code, hall_sensor_dev->lid_indev->sw));
	if(!!test_bit(code, hall_sensor_dev->lid_indev->sw) != !hall_sensor_dev->status){
		p_debug("\n[%s]lid_event!lid_indev->sw=%lx h,\n",
			DRIVER_NAME,hall_sensor_dev->lid_indev->sw);
       __change_bit(code,  hall_sensor_dev->lid_indev->sw);
	   p_debug("\n[%s]lid_event!lid_indev->sw=%lx h,\n",
		   DRIVER_NAME,hall_sensor_dev->lid_indev->sw);
    	p_debug("[%s] reset dev->sw(!hall_sensor_dev->status)=%d \n", DRIVER_NAME,!hall_sensor_dev->status);
	}//end if
	}//end if
}

bool lid_match(struct input_handler *handler, struct input_dev *dev){
	p_debug("\n[%s]lid_match!\n",DRIVER_NAME);
	if(dev->name && handler->name)
		if(!strcmp(dev->name,"lid_input") && !strcmp(handler->name,"lid_input_handler"))
		        return true;
		
	return false;
}
//@return DIN ,1(high) or 0(low)
int hall_get_din()
{
	int result = intel_mid_pmic_readb(GPIO1P0CTLI)&0x01 ;
	return result ;
}

static ssize_t show_action_status(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	p_debug("\n[%s]show_action_status!\n",DRIVER_NAME);
	if(!hall_sensor_dev)
		return sprintf(buf, "Hall sensor does not exist!\n");
	//if (gpio_get_value(hall_sensor_dev->gpio) > 0)
	if(hall_get_din()>0)
		hall_sensor_dev->status = 1;
	else
		hall_sensor_dev->status = 0;
	return sprintf(buf, "%d\n", hall_sensor_dev->status);
}
static ssize_t store_action_status(struct device *dev, 
	struct device_attribute *attr, const char *buf, size_t count)
{
	int request;
	unsigned long flags;
	p_debug("\n[%s]store_action_status!\n",DRIVER_NAME);
	if(!hall_sensor_dev)
			 return sprintf(buf, "Hall sensor does not exist!\n");
	sscanf(buf, "%du", &request);
	spin_lock_irqsave(&hall_sensor_dev->mHallSensorLock, flags);
	if (!request)
		 hall_sensor_dev->status = 0;
	else
		 hall_sensor_dev->status = 1;
	spin_unlock_irqrestore(&hall_sensor_dev->mHallSensorLock, flags);
	pr_info("[%s] SW_LID rewite value( !hall_sensor_dev->status ) = %d\n", DRIVER_NAME,!hall_sensor_dev->status);
	return count;
}
static ssize_t show_hall_sensor_enable(struct device *dev, 
	struct device_attribute *attr, char *buf)
{
	p_debug("\n[%s]show_hall_sensor_enable!\n",DRIVER_NAME);
	if(!hall_sensor_dev)
			 return sprintf(buf, "Hall sensor does not exist!\n");
	return sprintf(buf, "%d\n",hall_sensor_dev->enable);
}

static ssize_t store_hall_sensor_enable(struct device *dev, 
	struct device_attribute *attr, const char *buf, size_t count)
{
	int request;
	p_debug("\n[%s]store_hall_sensor_enable!\n",DRIVER_NAME);
	if(!hall_sensor_dev)
			 return sprintf(buf, "Hall sensor does not exist!\n");
	sscanf(buf, "%du", &request);
	if(request==hall_sensor_dev->enable){
		return count;
	}
	else {
		unsigned long flags;
		spin_lock_irqsave(&hall_sensor_dev->mHallSensorLock, flags);
		p_debug("[%s] store_hall_sensor_enable->after spin_lock_irqsave\n", DRIVER_NAME);
		if (hall_sensor_dev->enable==0){
			enable_irq(hall_sensor_dev->irq);
			hall_sensor_dev->enable=1;
			p_debug("\n[%s]store_hall_sensor_enable!hall_sensor_Dev->enable=%d\n",
				DRIVER_NAME,hall_sensor_dev->enable);
		}
		else if (hall_sensor_dev->enable==1){		
			disable_irq(hall_sensor_dev->irq);
			hall_sensor_dev->enable=0;
			p_debug("\n[%s]store_hall_sensor_enable!hall_sensor_Dev->enable=%d\n",
				DRIVER_NAME,hall_sensor_dev->enable);
		}
		p_debug("[%s] store_hall_sensor_enable->before spin_unlock_irqrestore\n", DRIVER_NAME);
		spin_unlock_irqrestore(&hall_sensor_dev->mHallSensorLock, flags);
		p_debug("[%s] store_hall_sensor_enable->after spin_unlock_irqrestore\n", DRIVER_NAME);
	}
	return count;
}


static SENSOR_DEVICE_ATTR_2(action_status, S_IRUGO|S_IWUSR, 
	show_action_status, store_action_status, 0, 0);
static SENSOR_DEVICE_ATTR_2(activity, S_IRUGO|S_IWUSR|S_IWGRP,
	show_hall_sensor_enable, store_hall_sensor_enable, 0, 0);


static struct attribute *hall_sensor_attrs[] = {
	&sensor_dev_attr_action_status.dev_attr.attr,
	&sensor_dev_attr_activity.dev_attr.attr,
	NULL
	//*need to NULL terminate the list of attributes 
};

static struct attribute_group hall_sensor_group = {
	.name = "hall_sensor",
	.attrs = hall_sensor_attrs
};
static int lid_input_device_create(void)
{
	int err = 0;
	p_debug("\n[%s]lid_input_device_create!\n",DRIVER_NAME);

	hall_sensor_dev->lid_indev = input_allocate_device();     
	if(!hall_sensor_dev->lid_indev){
		p_debug("[%s] lid_indev allocation fails\n", DRIVER_NAME);
		err = -ENOMEM;
		goto exit;
	}

	hall_sensor_dev->lid_indev->name = "lid_input";
	hall_sensor_dev->lid_indev->phys= "/dev/input/lid_indev";
	hall_sensor_dev->lid_indev->dev.parent= NULL;

	//mark device as capable of a certain event
//	input_set_capability(hall_sensor_dev->lid_indev, EV_KEY, KEY_POWER);
	input_set_capability(hall_sensor_dev->lid_indev, EV_SW, SW_LID);
	err = input_register_device(hall_sensor_dev->lid_indev);
	if (err) {
		p_debug("[%s] input registration fails\n", DRIVER_NAME);
		err = -1;
		goto exit_input_free;
	}
	return 0;



exit_input_free:
       input_free_device(hall_sensor_dev->lid_indev);
       hall_sensor_dev->lid_indev = NULL;
exit:
       return err;
}

static void lid_report_function(void)
{
    unsigned long flags;
	p_debug("[%s] lid_report_function\n", DRIVER_NAME);
//	msleep(50);
//	spin_lock_irqsave(&hall_sensor_dev->mHallSensorLock, flags);
	if(hall_get_din()>0){	
		hall_sensor_dev->status = 1;
	}
	else{
		hall_sensor_dev->status = 0;
	}
	input_report_switch(hall_sensor_dev->lid_indev,
		SW_LID,!(hall_sensor_dev->status)) ;
	input_sync(hall_sensor_dev->lid_indev) ;
	//wake_unlock(&hall_sensor_dev->wake_lock) ;
}
static irqreturn_t hall_sensor_interrupt_handler(int irq, void *dev_id)
{
	p_debug("[%s] hal_sensor_interrupt_handler->hall_sensor suspending = %d, GPIO report value = %d\n", DRIVER_NAME,suspending, hall_get_din());
	//wake_lock(&hall_sensor_dev->wake_lock) ;
	lid_report_function();
	return IRQ_HANDLED;
}

static int set_irq_hall_sensor(void)
{
	int rc = 0 ;	
	p_debug("[%s] set_irq_hall_sensor\n", DRIVER_NAME);
	p_debug("[%s] hall_sensor irq = %d\n", DRIVER_NAME,hall_sensor_dev->irq);
	//apply for irq,register interrupt handler function
	rc = request_threaded_irq(hall_sensor_dev->irq,
	NULL,
	hall_sensor_interrupt_handler,
	//IRQF_TRIGGER_RISING,
	IRQF_ONESHOT,
	"hall_sensor_irq",
	hall_sensor_dev);
	if(rc){
		p_debug("[%s] Could not register for hall sensor interrupt, irq = %d, rc = %d\n", DRIVER_NAME,hall_sensor_dev->irq,rc);
		rc = -EIO;
		goto err_gpio_request_irq_fail ;
	}

	enable_irq_wake(hall_sensor_dev->irq);

	return 0;

err_gpio_request_irq_fail:
	return rc;
}



static int lid_suspend_noirq(struct device *dev){
	p_debug("[%s] lid_suspend_noirq\n", DRIVER_NAME);
	return 0;
}
static int lid_suspend_prepare(struct device *dev){
	p_debug("[%s]lid_suspend_prepare\n", DRIVER_NAME);
      return 0;
}
static int lid_suspend_suspend(struct device *dev){
	p_debug("[%s]lid_suspend_suspend\n", DRIVER_NAME);
      return 0;
}
static void lid_resume_complete(struct device *dev){
	p_debug("[%s]lid_resume_complete\n", DRIVER_NAME);
}

static struct dev_pm_ops lid_dev_pm_ops ={
	.prepare = lid_suspend_prepare ,
	.complete = lid_resume_complete,
	.suspend = lid_suspend_suspend,
	.suspend_noirq = lid_suspend_noirq ,
};


//+++++++++++++for pm_ops callback+++++++++++++++

static const struct platform_device_id lid_id_table[] = {
        {DRIVER_NAME, 1},
};

static struct platform_driver lid_platform_driver = {
	// for non-;interrupt control descade mode'
	.driver.name    = DRIVER_NAME,
	.driver.owner	= THIS_MODULE,
	.driver.pm      = &lid_dev_pm_ops,
	.probe          = lid_probe ,
	.suspend  		=lid_suspend ,
	.id_table	= lid_id_table,
};

void _set_pmic_hall_ioctlreg()
{
//	GPIO1P0CTLO WR
//	GPIO1P0CTLI WR
	intel_mid_pmic_writeb(GPIO1P0CTLO,0x54) ;
	intel_mid_pmic_writeb(GPIO1P0CTLI,0x0e) ;
}
void _set_irq_mask_gpio(int state)
{
	int mask =0x20 ;
 	switch(state){
	case MASK :
		intel_scu_ipc_update_register(MIRQLVL1,mask,0);
		break;
	case UNMASK :
		intel_scu_ipc_update_register(MIRQLVL1,0,mask);
		break;
 	}
}
void _set_irq_mask_hall(int state)
{
	int mask =0x01 ;
	//	MGPIO1IRQS0  WR
	int data =0;
	
 	switch(state){
	case MASK :
		intel_scu_ipc_update_register(MGPIO1P0IRQS0,mask,0);
		intel_scu_ipc_update_register(MGPIO1P0IRQSX,mask,0);
		break;
	case UNMASK :
		intel_scu_ipc_update_register(MGPIO1P0IRQS0,0,mask);
		intel_scu_ipc_update_register(MGPIO1P0IRQSX,0,mask);
		break;
 	}
}
//set first level irq mask,gpio1p0ctlo ,gpio1p0ctli,2nd level irq mask
void hall_set_pmic_reg()
{
	_set_irq_mask_gpio(UNMASK) ;
	_set_pmic_hall_ioctlreg();
	_set_irq_mask_hall(UNMASK);
}

static int lid_suspend(struct platform_device *pdev, pm_message_t state)
{
	p_debug("\n[%s]lid_suspend!\n",DRIVER_NAME);
	return 0 ;
}
static int lid_probe(struct platform_device *pdev){
	int ret =0;
	int data=0 ;
	p_debug("\n[%s]lid_probe!\n",DRIVER_NAME);
	//set file node
	//kernel_kobj ( /sys/kernel/hall_sensor_kobject) 
	hall_sensor_kobj = kobject_create_and_add("hall_sensor_kobject", kernel_kobj);
	if (!hall_sensor_kobj){
		p_debug("[%s] hall_sensor_kobject fails for hall sensor\n", DRIVER_NAME);
		platform_device_unregister(pdev);
		return -ENOMEM;
	}
	else{
		p_debug("\n[%s]hall_sensor_init!hall_sensor_kobj create successfully\n",DRIVER_NAME);
	}
	//create file 'action_status' and 'activity' and 'delay' in /sys/kernel/hall_sensor_kobject
	ret = sysfs_create_group(hall_sensor_kobj, &hall_sensor_group);
	if (ret){
		p_debug("\n[%s]hall_sensor_init!sysfs_create_group() failed\n",DRIVER_NAME);
		goto fail_for_hall_sensor;
	}
	else{
		p_debug("\n[%s]hall_sensor_init!sys_create_group() succeed\n",DRIVER_NAME);
		}
	
			//Memory allocation
		hall_sensor_dev = kzalloc(sizeof (struct hall_sensor_str), GFP_KERNEL);
		if (!hall_sensor_dev) {
				p_debug("[%s] Memory allocation fails for hall sensor\n", DRIVER_NAME);
				ret = -ENOMEM;
				goto fail_for_hall_sensor;
		}
		else{
				p_debug("\n[%s]hall_sensor_init!kzalloc succeed\n",DRIVER_NAME);
		}
	
		spin_lock_init(&hall_sensor_dev->mHallSensorLock);
		hall_sensor_dev->enable = 1;	
		hall_sensor_dev->irq = platform_get_irq(pdev,0);
		p_debug("\n[%s]hall_sensor_init!hall_sensor_dev->irq:%d\n",
					DRIVER_NAME,hall_sensor_dev->irq);	
		hall_set_pmic_reg();

		//set irq
		ret = set_irq_hall_sensor();
		if (ret < 0){
			p_debug("[%s]set_ire_hall_sensor->failed\n", DRIVER_NAME);
			goto fail_for_irq_hall_sensor;
			}
		else{
			p_debug("[%s]set_ire_hall_sensor->successfully.\n", DRIVER_NAME);
			}
		//create input_dev
		hall_sensor_dev->lid_indev = NULL;
		ret = lid_input_device_create();
		if (ret < 0)
		goto fail_for_create_input_dev;
		wake_lock_init(&hall_sensor_dev->wake_lock,WAKE_LOCK_SUSPEND,
			"lid_suspend_blocker") ;
		return 0;
		
	fail_for_create_input_dev:		
		free_irq(hall_sensor_dev->irq, hall_sensor_dev);	
	
	fail_for_irq_hall_sensor:
		
	fail_for_set_gpio_hall_sensor:
		kfree(hall_sensor_dev);
		hall_sensor_dev=NULL;
	
	fail_for_hall_sensor:
		kobject_put(hall_sensor_kobj);
	return 0;
}

//----------------for pm_ops callback----------------

static int __init hall_sensor_init(void)
{	
	int ret =0 ;
	p_debug("\n[%s]hall_sensor_init!\n",DRIVER_NAME);
	//insert pm_ops
	pdev= platform_device_alloc(DRIVER_NAME,-1);
	
	if (!pdev)
		return -1;
	ret = platform_device_add_resources(pdev,
		hall_resources,ARRAY_SIZE(hall_resources));
	ret = platform_device_add(pdev);
      if (ret) 
	  	return -1 ;
	return  platform_driver_register(&lid_platform_driver);
}

static void __exit hall_sensor_exit(void)
{
	p_debug("[%s]hall_sensor_exit\n", DRIVER_NAME);
	free_irq(hall_sensor_dev->irq, hall_sensor_dev);
	input_free_device(hall_sensor_dev->lid_indev);
	hall_sensor_dev->lid_indev=NULL;
	kfree(hall_sensor_dev);
	hall_sensor_dev=NULL;

	kobject_put(hall_sensor_kobj);
	platform_driver_unregister(&lid_platform_driver);
	platform_device_unregister(pdev);
	wake_lock_destroy(&hall_sensor_dev->wake_lock);
}
late_initcall(hall_sensor_init);
//module_init(hall_sensor_init);

module_exit(hall_sensor_exit);


MODULE_DESCRIPTION("Intel Hall sensor Driver");
MODULE_LICENSE("GPL v2");
