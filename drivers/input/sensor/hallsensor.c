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

#include <linux/mfd/intel_mid_pmic.h>
#include <asm/intel_scu_pmic.h>
#include <asm/intel_vlv2.h>
#define DEVICE_NAME "gpio_hall"

#define MIRQLVL1 0x0e
#define GPIO1P0CTLO 0x3b
#define GPIO1P0CTLI 0x43
#define MGPIO1P0IRQS0 0x1A

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

static struct workqueue_struct *hall_sensor_irq_wq;
static struct workqueue_struct *hall_sensor_wq;
static struct kobject *hall_sensor_kobj;
static struct platform_device *pdev;
static bool suspending = false;
static struct input_device_id mID[] = {
        { .driver_info = 1 },		//scan all device to match hall sensor
        { },
};
static int lid_probe(struct platform_device *pdev);
static struct hall_sensor_str {

 	int irq;
	int status;
	int gpio;
	int enable; 
	int delay ;
	spinlock_t mHallSensorLock;
	struct wake_lock wake_lock;
	struct input_dev *lid_indev;
	struct input_handler lid_handler;
	struct input_handle lid_handle;
 	struct delayed_work hall_sensor_irq_work;
 	struct delayed_work hall_sensor_work;
	
}* hall_sensor_dev;
static struct resource hall_resources[] = {
	{
		.name  = "HALL",
		.start = VV_PMIC_GPIO_IRQBASE+GPIO_HALL,
		.end   = VV_PMIC_GPIO_IRQBASE+GPIO_HALL,
		.flags = IORESOURCE_IRQ,
	},
};
static ssize_t hall_delay_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
        return sprintf(buf, "%d\n", hall_sensor_dev->delay);
}

static ssize_t hall_delay_store(struct device *dev,
                                        struct device_attribute *attr,
                                                const char *buf, size_t count)
{
        hall_sensor_dev->delay = simple_strtoul(buf, NULL, 10);
        
        return count;
}
 
 
 static void hall_sensor_early_suspend(struct early_suspend *h)
 {
	 p_debug("\n[%s]hall_sensor_early_suspend! suspending=true\n",DRIVER_NAME);
	 p_debug("\n[%s]hall_sensor_early_suspend! delay++++end\n",DRIVER_NAME);
	 suspending = true;
 }
 
static void hall_sensor_late_resume(struct early_suspend *h)
{
	p_debug("\n[%s]hall_sensor_late_resume2!suspending=false\n",DRIVER_NAME);
	p_debug("\n[%s]hall_sensor_late_resume2!delay++++end\n",DRIVER_NAME);
	suspending = false;
}


int lid_connect(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id){
	p_debug("\n[%s]lid_connect!\n",DRIVER_NAME);
	return 0;
}

void lid_event(struct input_handle *handle, unsigned int type, unsigned int code, int value){
	p_debug("\n[%s]lid_event!\n",DRIVER_NAME);
	
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

static ssize_t show_hall_sensor_enable(struct device *dev, 
	struct device_attribute *attr, char *buf)
{
	p_debug("\n[%s]show_hall_sensor_enable!\n",DRIVER_NAME);
	return sprintf(buf, "%d\n",hall_sensor_dev->enable);
}

static ssize_t store_hall_sensor_enable(struct device *dev, 
	struct device_attribute *attr, const char *buf, size_t count)
{
	int request;
	p_debug("\n[%s]store_hall_sensor_enable!\n",DRIVER_NAME);
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


static SENSOR_DEVICE_ATTR_2(action_status, S_IRUGO, 
	show_action_status, NULL, 0, 0);
static SENSOR_DEVICE_ATTR_2(activity, S_IRUGO|S_IWUSR|S_IWGRP,
	show_hall_sensor_enable, store_hall_sensor_enable, 0, 0);
static SENSOR_DEVICE_ATTR_2(delay, S_IRUGO|S_IWUSR, 
	hall_delay_show, hall_delay_store, 0, 0);


static struct attribute *hall_sensor_attrs[] = {
	&sensor_dev_attr_action_status.dev_attr.attr,
	&sensor_dev_attr_activity.dev_attr.attr,
	&sensor_dev_attr_delay.dev_attr.attr,
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
	input_set_capability(hall_sensor_dev->lid_indev, EV_KEY, KEY_POWER);

	err = input_register_device(hall_sensor_dev->lid_indev);
	if (err) {
		p_debug("[%s] input registration fails\n", DRIVER_NAME);
		err = -1;
		goto exit_input_free;
	}
	hall_sensor_dev->lid_handler.match=lid_match;
	hall_sensor_dev->lid_handler.connect=lid_connect;
	hall_sensor_dev->lid_handler.event=lid_event;
	hall_sensor_dev->lid_handler.name="lid_input_handler";
	hall_sensor_dev->lid_handler.id_table = mID;	
	err=input_register_handler(& hall_sensor_dev->lid_handler);
	if(err){
		p_debug("[%s] handler registration fails\n", DRIVER_NAME);
		err = -1;
		goto exit_unregister_input_dev;
	}
	hall_sensor_dev->lid_handle.name="lid_handle";
	hall_sensor_dev->lid_handle.open=1;         //receive any event from hall sensor
	hall_sensor_dev->lid_handle.dev=hall_sensor_dev->lid_indev;
	hall_sensor_dev->lid_handle.handler=&hall_sensor_dev->lid_handler;
	err=input_register_handle(& hall_sensor_dev->lid_handle);
	if(err){
		p_debug("[%s] handle registration fails\n", DRIVER_NAME);
		err = -1;
		goto exit_unregister_handler;
	}
	return 0;

exit_unregister_handler:
       input_unregister_handler(& hall_sensor_dev->lid_handler);
exit_unregister_input_dev:
       input_unregister_device(hall_sensor_dev->lid_indev);
exit_input_free:
       input_free_device(hall_sensor_dev->lid_indev);
       hall_sensor_dev->lid_indev = NULL;
exit:
       return err;
}

static void lid_report_function(struct work_struct *dat)
{
    unsigned long flags;
	p_debug("[%s] lid_report_function\n", DRIVER_NAME);
//	msleep(50);
//	spin_lock_irqsave(&hall_sensor_dev->mHallSensorLock, flags);
	p_debug("[%s] lid_report_function->after spin_lock_irqsave\n", DRIVER_NAME);
	if(hall_get_din()>0){	
	hall_sensor_dev->status = 1;
	}
	else{
			hall_sensor_dev->status = 0;
	}
	p_debug("[%s] lid_report_function->before spin_unlock_irqrestore\n", DRIVER_NAME);
//	spin_unlock_irqrestore(&hall_sensor_dev->mHallSensorLock, flags);
	p_debug("[%s] lid_report_function->after spin_unlock_irqrestore\n", DRIVER_NAME);
	p_debug("[%s] hall_sensor suspending = %d, GPIO report value = %d\n",
		DRIVER_NAME,suspending, hall_sensor_dev->status);
	if((suspending==true && hall_sensor_dev->status == 1)
		|| (suspending==false && hall_sensor_dev->status == 0)) {
			p_debug("[%s] hall_sensor suspending = %d, GPIO report value = %d._key_power 1>0\n",
			DRIVER_NAME,suspending, hall_sensor_dev->status);
	        input_report_key(hall_sensor_dev->lid_indev, KEY_POWER, 1);
	        input_sync(hall_sensor_dev->lid_indev);
	        input_report_key(hall_sensor_dev->lid_indev, KEY_POWER, 0);
	        input_sync(hall_sensor_dev->lid_indev);
	        p_debug("[%s] ====hall_sensor send KEY_POWER event====\n", DRIVER_NAME);
			p_debug("[%s]lid_report_function->delay++++begin\n", DRIVER_NAME);
	}
	p_debug("[%s]  lid_report_function->after queue_work\n", DRIVER_NAME);
}


static void do_report_function(struct work_struct *dat)
{
	p_debug("[%s] do_report_function\n", DRIVER_NAME);
// * cancel_delayed_work_sync - cancel a delayed work and wait for it to finish
     cancel_delayed_work_sync(&hall_sensor_dev->hall_sensor_work);
	//flush_work_sync - wait until a work has finished execution
    // @work: the work to flush
    
	flush_work_sync(&hall_sensor_dev->hall_sensor_work);
// * queue_delayed_work - queue work on a workqueue after delay
queue_delayed_work(hall_sensor_wq,
	&hall_sensor_dev->hall_sensor_work,msecs_to_jiffies(hall_sensor_dev->delay));

}

static irqreturn_t hall_sensor_interrupt_handler(int irq, void *dev_id)
{
	p_debug("[%s]  hall_sensor_interrupt_handler->before queue_work\n", 
		DRIVER_NAME);
	p_debug("[%s]  hall_sensor_interrupt_handler->hall_sensor_interrupt = %d\n", DRIVER_NAME,hall_sensor_dev->irq);
//	queue_delayed_work(hall_sensor_irq_wq, 
//		&hall_sensor_dev->hall_sensor_irq_work, 0);
p_debug("[%s] hal_sensor_interrupt_handler->hall_sensor suspending = %d, GPIO report value = %d\n",
	DRIVER_NAME,suspending, hall_get_din());
	do_report_function(NULL);
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


static struct early_suspend hall_suspend_desc = {
//	.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN ,
	.suspend = hall_sensor_early_suspend,
};
static struct early_suspend hall_suspend_desc2 = {
	.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.resume  = hall_sensor_late_resume,
};



//+++++++++++++for pm_ops callback+++++++++++++++

static const struct platform_device_id lid_id_table[] = {
        {DRIVER_NAME, 1},
};

static struct platform_driver lid_platform_driver = {
	// for non-;interrupt control descade mode'
	.driver.name    = DRIVER_NAME,
	.driver.owner	= THIS_MODULE,
	//.driver.pm      = &lid_dev_pm_ops,
	.probe          = lid_probe,
	.id_table	= lid_id_table,
};

void _set_pmic_hall_ioctlreg()
{
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
 	switch(state){
	case MASK :
	intel_scu_ipc_update_register(MGPIO1P0IRQS0,mask,0);
		break;
	case UNMASK :
	intel_scu_ipc_update_register(MGPIO1P0IRQS0,0,mask);
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


static int lid_probe(struct platform_device *pdev){
	int ret =0;
	register_early_suspend(&hall_suspend_desc);
	register_early_suspend(&hall_suspend_desc2);
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
		hall_sensor_dev->delay = 500 ;
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
		hall_sensor_irq_wq = 
			alloc_workqueue("hall_sensor_irq_wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
		INIT_DELAYED_WORK(
					&hall_sensor_dev->hall_sensor_irq_work, 
					do_report_function);
		hall_sensor_wq = 
			alloc_workqueue("hall_sensor_wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
		INIT_DELAYED_WORK(
			&hall_sensor_dev->hall_sensor_work, 
			lid_report_function);
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
	destroy_workqueue(&hall_sensor_dev->hall_sensor_irq_work);
	destroy_workqueue(&hall_sensor_dev->hall_sensor_work);
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
