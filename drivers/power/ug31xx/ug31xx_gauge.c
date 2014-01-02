/*
 * Copyright (c) 2012, uPI Semiconductor Corp. All Rights Reserved.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <asm/unaligned.h>
#include <linux/wakelock.h>

#include "ug31xx_ggb_data.h"
#include "uG31xx_Platform.h"
#include "ug31xx_gauge.h"
#include "uG31xx_API_Platform.h"
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h> //tan
#include <linux/init.h> 		    //tan
//#define	UPI_CALLBACK_FUNC	            ///< [AT-PM] : Used for removing callback function ; 04/15/2013
#define	UG31XX_DYNAMIC_POLLING	      ///< [AT-PM] : Used for dynamic polling time ; 04/30/2013
//#define UG31XX_WAIT_CHARGER_FC        ///< [AT-PM] : Used for full charge status decided by charger ; 07/25/2013
//#define UG31XX_DYNAMIC_TAPER_CURRENT  ///< [AT-PM] : Used for changing taper current threshold ; 09/01/2013
//#define UG31XX_REGISTER_I2C     ///< [AT-PM] : Used for register I2C inside module ; 09/03/2013
#define UG31XX_REGISTER_POWERSUPPLY   ///< [AT-PM] : Used for register powersupply class ; 09/05/2013
#define UG31XX_SHOW_EXT_TEMP    ///< [FC] : Set temperature reference by external ; 09/30/2013
#define UG31XX_MISC_DEV
#define UG31XX_PROBE_CHARGER_OFF      ///< [AT-PM] : Used for charger off at probe ; 12/06/2013
#define UG31XX_EARLY_SUSPEND			///< [AT-PM] : Used for early suspend instead of suspend ; 12/07/2013
#define UG31XX_PROC_DEV

//#include "../smb345_external_include.h"
//#include "../asus_ec_power.h"
//#include <linux/HWVersion.h>
#include <linux/switch.h>
#include <linux/earlysuspend.h>
#define ENABLE_BATTERY_SHIFTx
//tan extern int Read_HW_ID(void);
//tan extern void aicl_dete_worker(struct work_struct *dat);
extern int ec_power_changed_all();
static int hwid;
//tan extern int entry_mode;
struct switch_dev batt_dev;

struct ug31xx_gauge {
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND)
	struct early_suspend es;
#endif	///< end of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 
	struct i2c_client       *client;
	struct device           *dev;
	struct delayed_work     batt_info_update_work;
	struct delayed_work     batt_power_update_work;
	struct delayed_work     batt_probe_work;
	struct delayed_work     board_offset_cali_work;
	struct wake_lock        batt_wake_lock;
	struct mutex            info_update_lock;

	u32 cable_status;
	u32 polling_time;         ///< [AT-PM] : unit in second ; 12/10/2013
	u32 update_time;          ///< [AT-PM] : unit in second ; 12/10/2013
	u32 batt_volt;            ///< [AT-PM] : unit in mV ; 10/07/2013
	int batt_capacity;        ///< [AT-PM] : unit in % ; 10/07/2013
	int batt_capacity_shifted;///< [AT-PM] : unit in % ; 10/07/2013 chris add.
	int batt_capacity_last;   ///< [AT-PM] : unit in % ; 10/07/2013
	int batt_capacity_real;   ///< [AT-PM] : unit in % ; 11/27/2013
	u32 batt_charge_now;      ///< [AT-PM] : unit in mAh ; 10/07/2013
	u32 batt_charge_full;     ///< [AT-PM] : unit in mAh ; 10/07/2013
	int batt_current;         ///< [AT-PM] : unit in mA ; 10/07/2013
	int batt_current_last;    ///< [AT-PM] : unit in mA ; 10/07/2013
	int batt_temp;            ///< [AT-PM] : unit in 0.1oC ; 10/07/2013
	int batt_avg_temp;        ///< [AT-PM] : unit in 0.1oC ; 11/27/2013
	int batt_ready;
	int batt_remove;
	int batt_alarm_sts;
	int batt_status;
	int batt_cycle_count;
	int batt_ntc_sts;
	int board_offset;

	/// [AT-PM] : Add for version ; 01/30/2013
	char gauge_debug[UPI_DEBUG_STRING];
};

static void batt_info_update_work_func(struct work_struct *work);

#define CABLE_STATUS_CHANGE_RELEASE_COUNT (1)

#define GAUGE_err(...)        printk(KERN_ERR "<BATT> " __VA_ARGS__);
#define GAUGE_notice(...)     printk(KERN_NOTICE "<BATT> " __VA_ARGS__);
#define GAUGE_info(...)       printk(KERN_INFO "<BATT> " __VA_ARGS__);

#ifdef UG31XX_MISC_DEV

#define UG31XX_IOC_MAGIC 'U'
#define UG31XX_IOCTL_RESET              _IO(UG31XX_IOC_MAGIC, 1)
#define UG31XX_IOCTL_ENABLE_SUSPEND_LOG _IO(UG31XX_IOC_MAGIC, 2)
#define UG31XX_IOCTL_ENABLE_LOG         _IO(UG31XX_IOC_MAGIC, 3)
#define UG31XX_IOCTL_DISABLE_LOG        _IO(UG31XX_IOC_MAGIC, 4)
#define UG31XX_IOCTL_I2C_READ           _IOWR(UG31XX_IOC_MAGIC, 5, unsigned char *)
#define UG31XX_IOCTL_I2C_WRITE          _IOWR(UG31XX_IOC_MAGIC, 6, unsigned char *)
#define UG31XX_IOCTL_RESET_CYCLE_COUNT  _IO(UG31XX_IOC_MAGIC, 7)
#define UG31XX_IOCTL_BACKUP_SIZE        _IOWR(UG31XX_IOC_MAGIC, 8, unsigned char *)
#define UG31XX_IOCTL_BACKUP_READ        _IOWR(UG31XX_IOC_MAGIC, 9, unsigned char *)
#define UG31XX_IOCTL_BACKUP_WRITE       _IOWR(UG31XX_IOC_MAGIC, 10, unsigned char *)
#define UG31XX_IOCTL_DAEMON_GET_CNTL    _IOWR(UG31XX_IOC_MAGIC, 11, unsigned char *)
#define UG31XX_IOCTL_DAEMON_SET_CNTL    _IOWR(UG31XX_IOC_MAGIC, 12, unsigned char *)
#define UG31XX_IOCTL_DAEMON_DELAY       _IOWR(UG31XX_IOC_MAGIC, 13, unsigned char *)
#define UG31XX_IOCTL_DAEMON_FILENAME    _IOWR(UG31XX_IOC_MAGIC, 14, unsigned char *)

#endif	///< end of UG31XX_MISC_DEV

#define UG31XX_WAKE_LOCK_TIMEOUT      		(1)

/* Functions Declaration */
#ifdef	UG31XX_REGISTER_POWERSUPPLY

static int ug31xx_battery_get_property(struct power_supply *psy, 
				       enum power_supply_property psp,
				       union power_supply_propval *val);

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

#ifdef	UPI_CALLBACK_FUNC

static int ug31xx_power_get_property(struct power_supply *psy, 
				     enum power_supply_property psp,
				     union power_supply_propval *val);

#else	///< else of UPI_CALLBACK_FUNC

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static void ug31xx_battery_external_power_changed(struct power_supply *psy);

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

#endif	///< end of UPI_CALLBACK_FUNC

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static int ug31xx_update_psp(enum power_supply_property psp,
			     union power_supply_propval *val);

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

/* Extern Function */
extern char FactoryGGBXFile[];

/* Global Variables */
static struct ug31xx_gauge *ug31 = NULL;
#ifdef	UG31XX_REGISTER_POWERSUPPLY
  static char *chg_status[] = {"Unknown", "Charging", "Discharging", "Not charging", "Full"};
#endif	///< end of UG31XX_REGISTER_POWERSUPPLY
unsigned char cur_cable_status = UG31XX_NO_CABLE;
ug31xx_drv_status_t ug31xx_drv_status = UG31XX_DRV_NOT_READY;
static bool charger_detect_full = false;
static int charge_termination_current = 0;          ///< [AT-PM] : Set charging termination current (0 = from GGB settings) ; 09/05/2013
static int cable_status_changed = 0;
static bool force_power_supply_change = true;
static bool charger_full_status = false;
static bool charger_dc_in_before_suspend = false;
static unsigned char op_options = LKM_OPTIONS_DEBUG_ERROR;
static int rsense_value = 0;                        ///< [AT-PM] : Set R-Sense value (0 = from GGB settings) ; 09/05/2013
static int ug31xx_backup_file_status = 0;
static unsigned short design_capacity = 0;
static bool probe_with_cable = false;
static bool in_early_suspend = false;
static int delta_q_in_suspend = 0;
static int rsoc_before_suspend = 0;
static bool fix_time_60s = false;
/*-----------------------tan#--------------*/
extern enum bq24192_chrgr_stat {
	BQ24192_CHRGR_STAT_UNKNOWN =0,
	BQ24192_CHRGR_STAT_CHARGING,
	BQ24192_CHRGR_STAT_FAULT,
	BQ24192_CHRGR_STAT_LOW_SUPPLY_FAULT,
	BQ24192_CHRGR_STAT_BAT_FULL,
};
extern enum bq24192_chrgr_stat bq24192_is_charging();
/*-----------------------------------------*/
static bool is_charging_full(void)
{
  int charger_status;

  //tan charger_status = smb345_get_charging_status();
  charger_status = bq24192_is_charging();
  if (charger_status == POWER_SUPPLY_STATUS_FULL)
  {
    return (true);
  }
  else
  {
    return (false);
  }
}

static bool is_charging(void)
{
  int charger_status;

  //tan charger_status = smb345_get_charging_status();
  charger_status = bq24192_is_charging();
  if ((charger_status == POWER_SUPPLY_STATUS_FULL) ||
      (charger_status == POWER_SUPPLY_STATUS_CHARGING))
  {
    return (true);
  }
  else
  {
    return (false);
  }
}

#ifdef UG31XX_MISC_DEV

static int ug31xx_misc_open(struct inode *inode, struct file *file)
{
  GAUGE_notice("[%s]\n", __func__);
  return 0;
}

static int ug31xx_misc_release(struct inode *inode, struct file *file)
{
  GAUGE_notice("[%s]\n", __func__);
  return 0;
}

static long ug31xx_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  int rc = 0;
  unsigned char val[2];
  unsigned char data = 0;
  unsigned char *backup_buf;
  int backup_size;
  unsigned char backup_file[256];

  switch (cmd)
  {
    case UG31XX_IOCTL_RESET:
      GAUGE_notice("[%s] cmd -> RESET\n", __func__);
      op_options = op_options | LKM_OPTIONS_FORCE_RESET;
      break;
      
    case UG31XX_IOCTL_ENABLE_SUSPEND_LOG:
      GAUGE_notice("[%s] cmd -> ENABLE_SUSPEND_LOG\n", __func__);
      op_options = op_options | LKM_OPTIONS_ENABLE_SUSPEND_DATA_LOG;
      break;
      
    case UG31XX_IOCTL_ENABLE_LOG:
      GAUGE_notice("[%s] cmd -> ENABLE_LOG\n", __func__);
      op_options = op_options | LKM_OPTIONS_ENABLE_DEBUG_LOG;
      break;

    case UG31XX_IOCTL_DISABLE_LOG:
      GAUGE_notice("[%s] cmd -> DISABLE_LOG\n", __func__);
      op_options = op_options & (~LKM_OPTIONS_ENABLE_DEBUG_LOG);
      break;

    case UG31XX_IOCTL_I2C_READ:
      GAUGE_notice("[%s] cmd -> I2C_READ\n", __func__);
      if(copy_from_user((void *)&val[0], (void __user *)arg, sizeof(unsigned char)))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_from_user fail\n", __func__);
        break;
      }
      GAUGE_notice("[%s] read addr: 0x%x\n", __func__, val[0]);
      if(ug31_module.ug31xx_i2c_read(val[0], &data))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] I2C read fail\n", __func__);
        break;
      }
      GAUGE_notice("[%s] read data: 0x%x\n", __func__, data);
      if(copy_to_user((void __user *)arg, (void *)&data, sizeof(unsigned char)))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
        break;
      }
      break;

    case UG31XX_IOCTL_I2C_WRITE:
      GAUGE_notice("[%s] cmd -> I2C_WRITE\n", __func__);
      if(copy_from_user((void *)&val[0], (void __user *)arg, sizeof(val)))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_from_user fail\n", __func__);
        break;
      }
      GAUGE_notice("[%s] write addr: 0x%x, data: 0x%x\n", __func__, val[0], val[1]);
      if(ug31_module.ug31xx_i2c_write(val[0], &val[1]))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] I2C write fail\n", __func__);
        break;
      }
      break;

    case UG31XX_IOCTL_RESET_CYCLE_COUNT:
      GAUGE_notice("[%s] cmd -> RESET_CYCLE_COUNT\n", __func__);
      rc = ug31_module.reset_cycle_count();
      break;

    case UG31XX_IOCTL_BACKUP_SIZE:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_BACKUP_SIZE\n", __func__);
      backup_buf = ug31_module.get_backup_buffer(&backup_size);
      data = (unsigned char)backup_size;
      if(copy_to_user((void __user *)arg, (void *)&data, sizeof(unsigned char)))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
      }
      break;

    case UG31XX_IOCTL_BACKUP_READ:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_BACKUP_READ\n", __func__);
      backup_buf = ug31_module.get_backup_buffer(&backup_size);
      if(copy_to_user((void __user *)arg, (void *)backup_buf, backup_size))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
      }
      break;

    case UG31XX_IOCTL_BACKUP_WRITE:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_BACKUP_WRITE\n", __func__);
      backup_buf = ug31_module.get_backup_buffer(&backup_size);
      if(copy_from_user((void *)backup_buf, (void __user *)arg, backup_size))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_from_user fail\n", __func__);
      }
      break;

    case UG31XX_IOCTL_DAEMON_GET_CNTL:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_DAEMON_GET_CNTL\n", __func__);
      mutex_lock(&ug31->info_update_lock);
      data = ug31_module.get_backup_daemon_cntl();
      mutex_unlock(&ug31->info_update_lock);
      if(copy_to_user((void __user *)arg, (void *)&data, 1))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
      }
      break;

    case UG31XX_IOCTL_DAEMON_SET_CNTL:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_DAEMON_SET_CNTL\n", __func__);
      if(copy_from_user((void *)&data, (void __user *)arg, 1))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_from_user fail\n", __func__);
      }
      mutex_lock(&ug31->info_update_lock);
      ug31_module.set_backup_daemon_cntl(data);
      mutex_unlock(&ug31->info_update_lock);
      break;

    case UG31XX_IOCTL_DAEMON_DELAY:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_DAEMON_DELAY\n", __func__);
      mutex_lock(&ug31->info_update_lock);
      data = ug31_module.get_backup_daemon_period();
      mutex_unlock(&ug31->info_update_lock);
      data = data - 1;
      if(copy_to_user((void __user *)arg, (void *)&data, 1))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
      }
      break;

    case UG31XX_IOCTL_DAEMON_FILENAME:
      GAUGE_notice("[%s] cmd -> UG31XX_IOCTL_DAEMON_FILENAME\n", __func__);
      sprintf(backup_file, "%s", UPI_UG31XX_BACKUP_FILE);
      if(copy_to_user((void __user *)arg, (void *)backup_file, 256))
      {
        rc = -EINVAL;
        GAUGE_err("[%s] copy_to_user fail\n", __func__);
      }
      break;
      
    default:
      GAUGE_err("[%s] invalid cmd %d\n", __func__, _IOC_NR(cmd));
      rc = -EINVAL;
      break;
  }

  return (rc);
}

static struct file_operations ug31xx_fops = {
  .owner = THIS_MODULE,
  .open = ug31xx_misc_open,
  .release = ug31xx_misc_release,
  .unlocked_ioctl = ug31xx_misc_ioctl
};

struct miscdevice ug31xx_misc = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "ug31xx",
  .fops = &ug31xx_fops
};

#endif	///< end of UG31XX_MISC_DEV

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static enum power_supply_property ug31xx_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
};

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

#ifdef	UPI_CALLBACK_FUNC
static enum power_supply_property ug31xx_pwr_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};
#endif	///< end of UPI_CALLBACK_FUNC

#ifdef	UPI_CALLBACK_FUNC
static char *supply_list[] = {
	"battery",
	"ac",
	"usb",
};
#endif	///< end of UPI_CALLBACK_FUNC

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static struct power_supply ug31xx_supply[] = {
	{
		.name			= "battery",
		.type			= POWER_SUPPLY_TYPE_BATTERY,
		.properties		= ug31xx_batt_props,
		.num_properties 	= ARRAY_SIZE(ug31xx_batt_props),
		.get_property		= ug31xx_battery_get_property,

#ifndef	UPI_CALLBACK_FUNC
		.external_power_changed	= ug31xx_battery_external_power_changed,
#endif	///< end of UPI_CALLBACK_FUNC

	},
#ifdef	UPI_CALLBACK_FUNC
	{
		.name			= "ac",
		.type			= POWER_SUPPLY_TYPE_MAINS,
		.supplied_to		= supply_list,
		.num_supplicants	= ARRAY_SIZE(supply_list),
		.properties 		= ug31xx_pwr_props,
		.num_properties		= ARRAY_SIZE(ug31xx_pwr_props),
		.get_property		= ug31xx_power_get_property,
	},
	{
		.name			= "usb",
		.type			= POWER_SUPPLY_TYPE_USB,
		.supplied_to		= supply_list,
		.num_supplicants	= ARRAY_SIZE(supply_list),
		.properties 		= ug31xx_pwr_props,
		.num_properties 	= ARRAY_SIZE(ug31xx_pwr_props),
		.get_property 		= ug31xx_power_get_property,
	},
#endif	///< end of UPI_CALLBACK_FUNC
};

static int ug31xx_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (ug31xx_update_psp(psp, val))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

#ifdef	UPI_CALLBACK_FUNC

static int ug31xx_power_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;

	switch (psp)
	{
	case POWER_SUPPLY_PROP_ONLINE:			
		if((psy->type == POWER_SUPPLY_TYPE_MAINS) && (cur_cable_status == UG31XX_AC_ADAPTER_CABLE))
		{
			val->intval = 1;
		}
		else if((psy->type == POWER_SUPPLY_TYPE_USB) && (cur_cable_status == UG31XX_USB_PC_CABLE))
		{
			val->intval = 1;
		}
		else
		{
			val->intval = 0;
		}
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

#endif	///< end of UPI_CALLBACK_FUNC

static void check_backup_file_routine(void)
{
	if(ug31xx_drv_status != UG31XX_DRV_INIT_OK)
	{
		return;
	}
  
	/// [AT-PM] : Enable check backup file ; 05/14/2013
	if(op_options & LKM_OPTIONS_DISABLE_BACHUP_FILE)
	{
		ug31_module.set_backup_file(false);
	}
	else
	{
		ug31_module.set_backup_file(true);
		ug31xx_backup_file_status = ug31_module.chk_backup_file();
		if((ug31xx_backup_file_status > 0) && (!(op_options & LKM_OPTIONS_FORCE_RESET)))
		{
			GAUGE_err("[%s] Gauge driver version mismatched.\n", __func__);
			op_options = op_options | LKM_OPTIONS_FORCE_RESET;
			#ifdef  UG31XX_USE_DAEMON_AP_FOR_FILE_OP
				set_file_op_status_bit(UG31XX_KERNEL_FILE_VERSION);
			#endif  ///< end of UG31XX_USE_DAEMON_AP_FOR_FILE_OP
		}
		else
		{
			#ifdef  UG31XX_USE_DAEMON_AP_FOR_FILE_OP
				clear_file_op_status_bit(UG31XX_KERNEL_FILE_VERSION);
			#endif  ///< end of UG31XX_USE_DAEMON_AP_FOR_FILE_OP
		}
	}
}

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static int ug31xx_update_psp(enum power_supply_property psp,
			     union power_supply_propval *val)
{
	if (ug31xx_drv_status == UG31XX_DRV_NOT_READY) {
		GAUGE_err("[%s] Gauge driver not init finish\n", __func__);
		return -EINVAL;
	}

	if(psp == POWER_SUPPLY_PROP_TEMP)
	{
		val->intval = ug31->batt_avg_temp;
#ifdef CONFIG_PF400CG
/* FIXME: ER1 has serious battery connector issue
		, we need report Fake battery temperature
*/
		if (hwid==4)
			val->intval = 250;
#endif
	}
	if(psp == POWER_SUPPLY_PROP_TEMP_AMBIENT)
	{
		val->intval = ug31->batt_temp;
	}
	if(psp == POWER_SUPPLY_PROP_CAPACITY)
	{
		if (ug31->batt_status == POWER_SUPPLY_STATUS_FULL)
		{
			ug31->batt_capacity = 100;
		}
		val->intval = ug31->batt_capacity;
		val->intval = ug31->batt_capacity_shifted;
		if (ug31->batt_ntc_sts != UPI_UG31XX_NTC_NORMAL)
			val->intval = -99;
	}
	if(psp == POWER_SUPPLY_PROP_VOLTAGE_NOW)
	{
		mutex_lock(&ug31->info_update_lock);
		val->intval = ug31_module.get_voltage_now();
		mutex_unlock(&ug31->info_update_lock);
		val->intval = val->intval * 1000;
	}
	if(psp == POWER_SUPPLY_PROP_VOLTAGE_AVG)
	{
		val->intval = ug31->batt_volt * 1000;
	}
	if(psp == POWER_SUPPLY_PROP_CURRENT_NOW)
	{
		mutex_lock(&ug31->info_update_lock);
		val->intval = ug31_module.get_current_now();
		mutex_unlock(&ug31->info_update_lock);
		if((is_charging() == false) && (val->intval > 0))
		{
			val->intval = 0;
		}
		val->intval = val->intval * 1000;
	}
	if(psp == POWER_SUPPLY_PROP_CURRENT_AVG)
	{
		val->intval = ug31->batt_current * 1000;
	}
	if(psp == POWER_SUPPLY_PROP_STATUS)
	{
		if(cur_cable_status) 
		{
			if (ug31->batt_capacity_shifted == 100)
			{
				ug31->batt_status = POWER_SUPPLY_STATUS_FULL;
			}
			else
			{
				//tan ug31->batt_status = smb345_get_charging_status();
				ug31->batt_status = bq24192_is_charging();
				if (ug31->batt_status < 0)
					ug31->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
				else
					ug31->batt_status = POWER_SUPPLY_STATUS_CHARGING;
			}
			#if defined(ME372CG_ENG_BUILD)
			if (!get_sw_charging_toggle())
				ug31->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
			#endif
		}
		else
		{
			ug31->batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		val->intval = ug31->batt_status;
		if (ug31->batt_ntc_sts != UPI_UG31XX_NTC_NORMAL)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	if(psp == POWER_SUPPLY_PROP_CHARGE_NOW)
	{
		val->intval = ug31->batt_charge_now;
	}
	if (psp == POWER_SUPPLY_PROP_CHARGE_FULL)
	{
		val->intval =ug31->batt_charge_full;
	}

	return 0;
}
#else	///< else of UG31XX_REGISTER_POWERSUPPLY

#endif  ///< end of UG31XX_REGISTER_POWERSUPPLY

static int ug31xx_powersupply_init(struct i2c_client *client)
{
  #ifdef  UG31XX_REGISTER_POWERSUPPLY
	int i, ret;
	for (i = 0; i < ARRAY_SIZE(ug31xx_supply); i++) {
		ret = power_supply_register(&client->dev, &ug31xx_supply[i]);
		if (ret) {
			GAUGE_err("[%s] Failed to register power supply\n", __func__);
			do {
				power_supply_unregister(&ug31xx_supply[i]);
			} while ((--i) >= 0);
			return ret;
		}
	}
  #else   ///< else of UG31XX_REGISTER_POWERSUPPLY

  #endif  ///< end of UG31XX_REGISTER_POWERSUPPLY
  return 0;
}

#ifdef	UPI_CALLBACK_FUNC

int ug31xx_cable_callback(unsigned char usb_cable_state)
{
	int old_cable_status;

	if(ug31xx_drv_status == UG31XX_DRV_NOT_READY)
	{
		GAUGE_err("[%s] Gauge driver not init finish\n", __func__);
		cur_cable_status = usb_cable_state;
		return -EPERM;
	}
	printk("========================================================\n");
	printk("%s  usb_cable_state = %x\n", __func__, usb_cable_state) ;
	printk("========================================================\n");

	old_cable_status = cur_cable_status;
	cur_cable_status = usb_cable_state;

	if(cur_cable_status == UG31XX_NO_CABLE)
	{
		if(old_cable_status == UG31XX_AC_ADAPTER_CABLE)
		{
			power_supply_changed(&ug31xx_supply[PWR_SUPPLY_AC]);
		} 
		else if(old_cable_status == UG31XX_USB_PC_CABLE)
		{
			power_supply_changed(&ug31xx_supply[PWR_SUPPLY_USB]);
		}
	}
	else if(cur_cable_status == UG31XX_USB_PC_CABLE)
	{
		power_supply_changed(&ug31xx_supply[PWR_SUPPLY_USB]);
	}
	else if(cur_cable_status == UG31XX_AC_ADAPTER_CABLE)
	{
		power_supply_changed(&ug31xx_supply[PWR_SUPPLY_AC]);
	}
  
	cable_status_changed = CABLE_STATUS_CHANGE_RELEASE_COUNT;
	return 0;
}
EXPORT_SYMBOL(ug31xx_cable_callback);

#else	///< else of UPI_CALLBACK_FUNC

#ifdef	UG31XX_REGISTER_POWERSUPPLY

static void ug31xx_battery_external_power_changed(struct power_supply *psy)
{
	int old_cable_status = cur_cable_status;
    /*tan
    if (power_supply_am_i_supplied(psy)) {
        switch (get_charger_type()) {
        case USB_IN:
            cur_cable_status = UG31XX_USB_PC_CABLE;
            GAUGE_err("[%s] UG31XX_USB_PC_CABLE\n", __func__);
            break;
        case AC_IN:
            cur_cable_status = UG31XX_AC_ADAPTER_CABLE;
            GAUGE_err("[%s] UG31XX_AC_ADAPTER_CABLE\n", __func__);
            break;
        case CABLE_OUT:
            cur_cable_status = UG31XX_NO_CABLE;
            GAUGE_err("[%s] UG31XX_NO_CABLE\n", __func__);
            break;
        case PAD_SUPPLY:
            cur_cable_status = UG31XX_PAD_POWER;
            GAUGE_err("[%s] UG31XX_PAD_POWER\n", __func__);
            break;
        }
    }
    else {
        cur_cable_status = UG31XX_NO_CABLE;
        GAUGE_err("[%s] UG31XX_NO_CABLE\n", __func__);
    }
   
	if(ug31xx_drv_status == UG31XX_DRV_NOT_READY)
	{
		GAUGE_err("[%s] Gauge driver not init finish\n", __func__);
		return;
	}

	if(old_cable_status != cur_cable_status)
	{
		cable_status_changed = CABLE_STATUS_CHANGE_RELEASE_COUNT;
	}*/
}

#endif	///< end of UG31XX_REGISTER_POWERSUPPLY

#endif	///< end of UPI_CALLBACK_FUNC

static void set_default_batt_status(void)
{
	ug31->polling_time		= 5;
	ug31->update_time		= 5;
	ug31->batt_volt			= 3700;
	ug31->batt_capacity		= 50;
	ug31->batt_capacity_shifted	= ug31->batt_capacity;
	ug31->batt_capacity_real	= 50;
	ug31->batt_charge_now	= 1000;
	ug31->batt_charge_full	= 2000;
	ug31->batt_current		= 0;
	ug31->batt_temp			= 250;
	ug31->batt_avg_temp		= 250;
	ug31->batt_ready		= UPI_UG31XX_MODULE_NOT_READY;
	ug31->batt_alarm_sts		= 0;
	ug31->batt_remove		= UPI_UG31XX_BATTERY_REMOVED;
	ug31->batt_cycle_count	= 0;
	ug31->batt_ntc_sts		= UPI_UG31XX_NTC_NORMAL;

	ug31->batt_capacity_last	= ug31->batt_capacity;
	ug31->batt_current_last	= ug31->batt_current;

	ug31->board_offset		= 0;
	cable_status_changed		= 0;
}

static void show_abnormal_batt_status(void)
{
	set_default_batt_status();
  
	ug31->batt_capacity		= -99;

	GAUGE_notice("ABN STS -> V=%d(mV) C=%d(mA) T=%d-%d(0.1oC) RSOC=%d(%d)(%%) RM=%d(mAh) FCC=%d(mAh) STS=%d CYCLE=%d\n",
		ug31->batt_volt,
		ug31->batt_current,
		ug31->batt_temp,
		ug31->batt_avg_temp,
		ug31->batt_capacity,
		ug31->batt_capacity_real,
		ug31->batt_charge_now,
		ug31->batt_charge_full,
		ug31->batt_status,
		ug31->batt_cycle_count);
}

static void show_default_batt_status(void)
{
	set_default_batt_status();
  
	GAUGE_notice("DEF STS -> V=%d(mV) C=%d(mA) T=%d-%d(0.1oC) RSOC=%d(%d)(%%) RM=%d(mAh) FCC=%d(mAh) STS=%d CYCLE=%d\n",
		ug31->batt_volt,
		ug31->batt_current,
		ug31->batt_temp,
		ug31->batt_avg_temp,
		ug31->batt_capacity,
		ug31->batt_capacity_real,
		ug31->batt_charge_now,
		ug31->batt_charge_full,
		ug31->batt_status,
		ug31->batt_cycle_count);
}

static void asus_print_all(void)
{
    int temperature;
    int temperature10;

    /*  UNKN means UNKOWN, CHRG mean CHARGING, DISC means DISCHARGING,
        NOTC means NOT CHARGING
    */
    char batt_status_str[5][5] = {
        "UNKN",
        "CHRG",
        "DISC",
        "NOTC",
        "FULL"
    };
    char *negative_sign = "-";

    /* printk(...) does not support %f and %e (float point and double) */
    if (ug31->batt_temp >= 10 || ug31->batt_temp <= -10) {
        temperature10 = ug31->batt_temp/10;
        temperature = ug31->batt_temp - (temperature10 * 10);
        if (temperature < 0) {
            temperature = -temperature;
        }
        negative_sign = "";
    }
    else {
        temperature10 = 0;
        temperature = ug31->batt_temp < 0 ?
                    -ug31->batt_temp : ug31->batt_temp;
        if (ug31->batt_temp >= 0)
            negative_sign = "";
    }

    GAUGE_notice("P:%d(%d-%d)%%, V:%dmV, C:%dmA, T:%s%d.%dC(%d), "
        "S:%s, R:%dmAh, F:%dmAh, CC:%d, P:%d(%d)s, BO:%d, %s\n",
        ug31->batt_capacity_shifted,
        ug31->batt_capacity,
        ug31->batt_capacity_real,
        ug31->batt_volt,
        ug31->batt_current,
        negative_sign,
        temperature10,
        temperature,
        ug31->batt_avg_temp,
        batt_status_str[ug31->batt_status],
        ug31->batt_charge_now,
        ug31->batt_charge_full,
        ug31->batt_cycle_count,
        ug31->polling_time,
	ug31->update_time,
	ug31->board_offset,
        ug31_module.get_version());
}

static int asus_battery_cap_shifted(int rsoc)
{
#ifdef ENABLE_BATTERY_SHIFT
    int ret;

    ret = ((ret >= 100) ? 100 : rsoc);

    /* start percentage shift:
        battery percentage remapping according
        to battery discharging curve
    */

    if (ret == 99) {
        ret = 100;
        return ret;
    }
    else if (ret >= 84 && ret <= 98) {
        ret++;
        return ret;
    }

    /* missing 26%, 47%, 58%, 69%, 79% */
    if (ret > 70 && ret < 80)
        ret -= 1;
    else if (ret > 60 && ret <= 70)
        ret -= 2;
    else if (ret > 50 && ret <= 60)
        ret -= 3;
    else if (ret > 30 && ret <= 50)
        ret -= 4;
    else if (ret >= 0 && ret <= 30)
        ret -= 5;

    /* final check to avoid negative value */
    if (ret < 0)
        ret = 0;

    return ret;
#else
    return rsoc;
#endif
}

#define MAX_BATTERY_UPDATE_INTERVAL     (600)

static void show_update_batt_status(void)
{
	ug31->batt_capacity_last	= ug31->batt_capacity;
	ug31->batt_current_last	= ug31->batt_current;

#ifdef  UG31XX_DYNAMIC_POLLING
	ug31->polling_time		= (u32)ug31_module.get_polling_time();
	ug31->update_time		= (u32)ug31_module.get_update_time();
#else   ///< else of UG31XX_DYNAMIC_POLLING
	ug31->polling_time		= 5;
	ug31->update_time		= 5;
#endif  ///< end of UG31XX_DYNAMIC_POLLING
	ug31->batt_volt			= (u32)ug31_module.get_voltage();
	ug31->batt_capacity		= (int)ug31_module.get_relative_state_of_charge();
	ug31->batt_capacity_shifted	= asus_battery_cap_shifted(ug31->batt_capacity);
	ug31->batt_capacity_real	= (int)ug31_module.get_predict_rsoc();
	ug31->batt_charge_now	= (u32)ug31_module.get_remaining_capacity();
	ug31->batt_charge_full	= (u32)ug31_module.get_full_charge_capacity();
	ug31->batt_current		= ug31_module.get_current();
#ifdef UG31XX_SHOW_EXT_TEMP
	ug31->batt_temp			= ug31_module.get_external_temperature();
#else   ///< else of UG31XX_SHOW_EXT_TEMP
	ug31->batt_temp			= ug31_module.get_internal_temperature();
#endif  ///< end of UG31XX_SHOW_EXT_TEMP
	ug31->batt_avg_temp		= ug31_module.get_avg_external_temperature();
	ug31->batt_ready		= ug31_module.get_module_ready();
	ug31->batt_alarm_sts		= ug31_module.get_alarm_status();
	ug31->batt_remove		= ug31_module.get_battery_removed();
	ug31->batt_cycle_count	= ug31_module.get_cycle_count();
	ug31->batt_ntc_sts		= ug31_module.get_ntc_status();
	ug31->board_offset		= ug31_module.get_board_offset();

	if(fix_time_60s == true)
	{
		ug31->polling_time = 60;
		ug31->update_time = 60;
	}

	if(cable_status_changed > 0)
	{
		ug31->update_time = 3;
		cable_status_changed = cable_status_changed - 1;
		force_power_supply_change = true;
	}

	asus_print_all();
}

static void adjust_cell_table(void)
{
	int rtn;
  
	if(op_options & LKM_OPTIONS_ADJUST_DESIGN_CAPACITY)
	{
		if(design_capacity >= 1)
		{
			rtn = ug31_module.adjust_cell_table(design_capacity);
			GAUGE_info("[%s] design capacity has been adjusted. (%d)\n", __func__, rtn);
		}
	}
	op_options = op_options & (~LKM_OPTIONS_ADJUST_DESIGN_CAPACITY);
}

#define	UG31_BATTERY_MAX_UPDATE_INTERVAL	(100)

static void batt_power_update_work_func(struct work_struct *work)
{
	struct ug31xx_gauge *ug31_dev;

	ug31_dev = container_of(work, struct ug31xx_gauge, batt_power_update_work.work);

	#ifdef  UG31XX_REGISTER_POWERSUPPLY

	if(wake_lock_active(&ug31_dev->batt_wake_lock) != 0)
	{
		wake_unlock(&ug31_dev->batt_wake_lock);
	}
	wake_lock_timeout(&ug31->batt_wake_lock, UG31XX_WAKE_LOCK_TIMEOUT*HZ);
	power_supply_changed(&ug31xx_supply[PWR_SUPPLY_BATTERY]);
  
	#endif  ///< end of UG31XX_REGISTER_POWERSUPPLY
  
	schedule_delayed_work(&ug31_dev->batt_power_update_work, ug31_dev->polling_time*HZ);
	GAUGE_info("[%s] issue power_supply_changed.\n", __func__);
}

/// =================================================================
/// Charger control for initial capacity prediction (Start)
/// =================================================================

static void start_charging(void)
{
	int ret;

	//tan ret = smb345_charging_toggle(true);
	if (ret < 0) {
		GAUGE_err("%s: fail to start charging\n", __func__);
    }
    else {
        GAUGE_info("ug31xx: start charging completely\n");
    }
}

static void stop_charging(void)
{
	int ret;

	//tan ret = smb345_charging_toggle(false);
	if (ret < 0) {
		GAUGE_err("%s: fail to stop charging\n", __func__);
    }
    else {
        GAUGE_info("ug31xx: stop charging completely\n");
    }
}

static ssize_t batt_switch_name(struct switch_dev *sdev, char *buf)
{
	const char* FAIL = "0xFFFF";

	if (ug31xx_drv_status == UG31XX_DRV_NOT_READY) {
		GAUGE_err("[%s] Gauge driver not init finish\n", __func__);
		return sprintf(buf, "%s\n", FAIL);
	}

	return sprintf(buf, "%s\n", ug31_module.get_version());
}

/// =================================================================
/// Charger control for initial capacity prediction (Stop)
/// =================================================================

#define UG31_BATTERY_WORK_MAX_INTERVAL      (30)
#define UG31XX_PROBE_CHARGER_OFF_DELAY      (1)

static void batt_info_update_work_func(struct work_struct *work)
{
	int gg_status;
	struct ug31xx_gauge *ug31_dev;
	int curr_direction_changed;
	int prev_fc_sts;
	int now_fc_sts;
	bool curr_charger_full_status;

	ug31_dev = container_of(work, struct ug31xx_gauge, batt_info_update_work.work);

	gg_status = 0;

	if(wake_lock_active(&ug31_dev->batt_wake_lock) != 0)
	{
		wake_unlock(&ug31_dev->batt_wake_lock);
	}
	wake_lock_timeout(&ug31_dev->batt_wake_lock, UG31XX_WAKE_LOCK_TIMEOUT*HZ);

	curr_charger_full_status = is_charging_full();

	if ((charger_full_status == false) && (curr_charger_full_status == true))
	{
		charger_detect_full = true;
		force_power_supply_change = true;
	}
	charger_full_status = curr_charger_full_status;

	/// [AT-PM] : Update gauge information ; 02/04/2013

	mutex_lock(&ug31_dev->info_update_lock); 
	ug31_module.set_options(op_options);  
	if(op_options & LKM_OPTIONS_FORCE_RESET)
	{
		ug31_module.reset(FactoryGGBXFile);
	}
	op_options = op_options & (~LKM_OPTIONS_FORCE_RESET);
  
	adjust_cell_table();
  
#ifdef  UG31XX_DYNAMIC_TAPER_CURRENT
  	ug31_module.set_charge_termination_current(charge_termination_current);
#endif  ///< end of UG31XX_DYNAMIC_TAPER_CURRENT

	prev_fc_sts = ug31_module.get_full_charge_status();
	gg_status = ug31_module.update();
	now_fc_sts = ug31_module.get_full_charge_status();
	if(gg_status == 0)
	{
		if(charger_detect_full == true)
		{
			charger_detect_full = false;
			gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
		}

		#ifndef UG31XX_WAIT_CHARGER_FC

			if((prev_fc_sts == 0) && (now_fc_sts == 1))
			{
				gg_status = ug31_module.set_charger_full(UG31XX_TAPER_REACHED);
			}

			if(curr_charger_full_status == true)
			{
				gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);

				if(is_charging() == true)
				{
					schedule_delayed_work(&ug31->board_offset_cali_work, UG31XX_PROBE_CHARGER_OFF_DELAY*HZ);
					GAUGE_info("[%s] Wait %d seconds to calibrate board offset\n", __func__, UG31XX_PROBE_CHARGER_OFF_DELAY);
					stop_charging();
				}
			}

		#endif  ///< end of UG31XX_WAIT_CHARGER_FC

		if(in_early_suspend == true)
		{
			delta_q_in_suspend = delta_q_in_suspend + ug31_module.get_delta_q();
		}
	}
  
	if(gg_status == 0)
	{
		show_update_batt_status();
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail to update gauge info.\n", __func__);
	}
	mutex_unlock(&ug31_dev->info_update_lock);

	curr_direction_changed = ug31_dev->batt_current_last*ug31_dev->batt_current;
	if((ug31_dev->batt_capacity != ug31_dev->batt_capacity_last) ||
	   (curr_direction_changed < 0) ||
	   (force_power_supply_change == true) ||
	   (ug31xx_backup_file_status != 0))
	{
		force_power_supply_change = false;

		cancel_delayed_work_sync(&ug31_dev->batt_power_update_work);
		schedule_delayed_work(&ug31_dev->batt_power_update_work, 0*HZ);
	}

	mutex_lock(&ug31_dev->info_update_lock);
	check_backup_file_routine();
	ug31_module.set_capacity_suspend_mode(_UPI_FALSE_);
	mutex_unlock(&ug31_dev->info_update_lock);

	if(op_options & LKM_OPTIONS_FORCE_RESET)
	{
		batt_info_update_work_func(&ug31_dev->batt_info_update_work.work);
	}
	else
	{
		schedule_delayed_work(&ug31_dev->batt_info_update_work, ug31_dev->update_time*HZ);
	}

	//tan aicl_dete_worker(NULL);
	#ifdef CONFIG_EC_POWER
	ec_power_changed_all();
	#endif
}

#ifdef  UG31XX_WAIT_CHARGER_FC

int ug31xx_charger_detect_full(bool is_charger_full)
{
	charger_detect_full = is_charger_full;

	if(ug31xx_drv_status != UG31XX_DRV_NOT_READY)
	{
		cancel_delayed_work_sync(&ug31->batt_info_update_work);
		batt_info_update_work_func(&ug31_dev->batt_info_update_work.work);
	}

	if(charger_detect_full == true)
	{
		GAUGE_info("[%s] charger_detect_full = true\n", __func__);
	}
	else
	{
		GAUGE_info("[%s] charger_detect_full = false\n", __func__);
	}
	return (0);
}
EXPORT_SYMBOL(ug31xx_charger_detect_full);

#endif  ///< end of UG31XX_WAIT_CHARGER_FC

#ifdef  UG31XX_DYNAMIC_TAPER_CURRENT

int ug31xx_set_charge_termination_current(int curr)
{
	charge_termination_current = curr;
	GAUGE_info("[%s] charge_termination_current = %d\n", __func__, charge_termination_current);
	return (0);
}
EXPORT_SYMBOL(ug31xx_set_charge_termination_current);

#endif  ///< end of UG31XX_DYNAMIC_TAPER_CURRENT

#ifdef UG31XX_PROC_DEV
struct proc_dir_entry *battery; 
int ug31xx_get_proc_rsoc(char *buf, char **start, off_t off, int count, 
                         int *eof, void *data )
{
  int len = 0;
  len += sprintf(buf+len, "\n%d\n", ug31_module.get_predict_rsoc());  
  len += sprintf(buf+len, "\n");
  return len;
}

int ug31xx_get_proc_psoc(char *buf, char **start, off_t off, int count, 
                         int *eof, void *data )
{
  int len = 0;
  len += sprintf(buf+len, "\n%d\n", ug31_module.get_relative_state_of_charge());  
  len += sprintf(buf+len, "\n");
  return len;
}

int ug31xx_get_proc_rm(char *buf, char **start, off_t off, int count, 
		       int *eof, void *data )
{
  int len = 0;
  len += sprintf(buf+len, "\n%d\n", ug31_module.get_remaining_capacity());  
  len += sprintf(buf+len, "\n");
  return len;
}

int ug31xx_get_proc_fcc(char *buf, char **start, off_t off, int count, 
			int *eof, void *data )
{
  int len = 0;
  len += sprintf(buf+len, "\n%d\n", ug31_module.get_full_charge_capacity());  
  len += sprintf(buf+len, "\n");
  return len;
}

int ug31xx_get_proc_curr(char *buf, char **start, off_t off, int count, 
                         int *eof, void *data )
{
  int len = 0;
  len += sprintf(buf+len, "\n%d\n", ug31_module.get_current());  
  len += sprintf(buf+len, "\n");
  return len;
}

int ug31xx_get_proc_upi_60(char *buf, char **start, off_t off, int count, int *eof, void *data)
{
	int len;

	fix_time_60s = true;

	len = 0;
	len += sprintf(buf + len, "\nFix polling time to 60 seconds\n");
	len += sprintf(buf + len, "\n");
	return (len);
}

int ug31xx_get_proc_upi_auto(char *buf, char **start, off_t off, int count, int *eof, void *data)
{
	int len;

	fix_time_60s = false;

	len = 0;
	len += sprintf(buf + len, "\nSet polling time to %d(%d) seconds\n", ug31->polling_time);
	len += sprintf(buf + len, "\n");
	return (len);
}
#endif  ///< end of UG31XX_PROC_DEV

#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND)

static void ug31xx_early_suspend(struct early_suspend *e)
{
	int gg_status;
	struct ug31xx_gauge *ug31_dev = container_of(e, struct ug31xx_gauge, es);

	dev_info(&ug31_dev->client->dev, "enter %s\n", __func__);
	GAUGE_info("[%s]\n", __func__);

	charger_dc_in_before_suspend = is_charging();

	mutex_lock(&ug31_dev->info_update_lock);
	gg_status = ug31_module.suspend((charger_dc_in_before_suspend == true) ? 1 : 0);
	if(gg_status == 0)
	{
		show_update_batt_status();
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail in early suspend.\n", __func__);
	}
	rsoc_before_suspend = ug31_module.get_relative_state_of_charge();
	mutex_unlock(&ug31_dev->info_update_lock);

	in_early_suspend = true;
	delta_q_in_suspend = 0;
	GAUGE_info("[%s] Driver early suspend.\n", __func__);
}

static void ug31xx_late_resume(struct early_suspend *e)
{
	struct ug31xx_gauge *ug31_dev = container_of(e, struct ug31xx_gauge, es);
	int gg_status;
	int prev_fc_sts;
	int now_fc_sts;
	bool charger_dc_in_after_resume;
	int prev_rsoc;

	dev_info(&ug31_dev->client->dev, "enter %s\n", __func__);
	GAUGE_info("[%s]\n", __func__);

	charger_dc_in_after_resume = is_charging();

	mutex_lock(&ug31_dev->info_update_lock);
	prev_fc_sts = ug31_module.get_full_charge_status();
	prev_rsoc = rsoc_before_suspend;	
	gg_status = ug31_module.resume();  
	delta_q_in_suspend = delta_q_in_suspend + ug31_module.get_delta_q();
	now_fc_sts = ug31_module.get_full_charge_status();
	if(gg_status == 0)
	{
		if(charger_detect_full == true)
		{
			charger_detect_full = false;
			gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
		}

		#ifndef UG31XX_WAIT_CHARGER_FC

		if(charger_dc_in_before_suspend == false)
		{
			GAUGE_info("[%s] Enter early suspend without charger\n", __func__);
		}
		else
		{
			GAUGE_info("[%s] Enter early suspend with charger\n", __func__);
			if((prev_fc_sts == 0) && (now_fc_sts == 1))
			{
				gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
				GAUGE_info("[%s] [FC] status changed (0->1)\n", __func__);
			}
			else
			{
				if(charger_dc_in_after_resume == false)
				{
					GAUGE_info("[%s] Exit early suspend without charger\n", __func__);         
					prev_rsoc = (100 - prev_rsoc)*(ug31_module.get_design_capacity())/100;
					if(delta_q_in_suspend > prev_rsoc)
					{
						GAUGE_info("[%s] Charged capacity is enouth to check full charged (%d) > (%d)\n", __func__, delta_q_in_suspend, prev_rsoc);

						if(ug31_module.get_predict_rsoc() > 90)
						{
							gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
							GAUGE_info("[%s] Set to full charge status\n", __func__);
						}
					}
				}
				else
				{
					GAUGE_info("[%s] Exit early suspend with charger\n", __func__);
					if(is_charging_full() == 1)
					{
						GAUGE_info("[%s] Charger detects full\n", __func__);
						gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
						GAUGE_info("[%s] Set to full charge status\n", __func__);
					}
					else
					{
						prev_rsoc = (100 - prev_rsoc)*(ug31_module.get_design_capacity())/100;
						if(delta_q_in_suspend > prev_rsoc)
						{
							GAUGE_info("[%s] Charged capacity is enouth to check full charged (%d) > (%d)\n", __func__, delta_q_in_suspend, prev_rsoc);
							if(ug31_module.get_predict_rsoc() > 95)
							{
								gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
								GAUGE_info("[%s] Set to full charge status\n", __func__);
							}
						}
					}
				}
			}
		}

		#endif  ///< end of UG31XX_WAIT_CHARGER_FC

		if(gg_status == 0)
		{
			show_update_batt_status();
		}
		else
		{
			show_abnormal_batt_status();
			GAUGE_err("[%s] Fail to set charger full.\n", __func__);
		}
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail to late resume.\n", __func__);
	}
	mutex_unlock(&ug31_dev->info_update_lock);

	cancel_delayed_work_sync(&ug31_dev->batt_info_update_work);
	batt_info_update_work_func(&ug31_dev->batt_info_update_work.work);

	force_power_supply_change = true;
	in_early_suspend = false;
	GAUGE_info("[%s] Gauge late resumed.\n", __func__);
}

static void ug31xx_config_earlysuspend(struct ug31xx_gauge *chip)
{
	chip->es.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 2;
	chip->es.suspend = ug31xx_early_suspend;
	chip->es.resume = ug31xx_late_resume;
	register_early_suspend(&chip->es);
}

#else	///< else of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 

#define ug31xx_early_suspend NULL
#define ug31xx_late_resume NULL

static void ug31xx_config_earlysuspend(struct ug31xx_gauge *chip) { return; }

#endif	///< end of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 

/**
 * @brief board_offset_cali_work_func
 *
 *  Work for calibrate board offset
 *
 * @para  work  address of struct work_struct
 * @return  NULL
 */
static void board_offset_cali_work_func(struct work_struct *work)
{
	ug31_module.calibrate_offset(UG31XX_BOARD_OFFSET_CALI_STEP);
	start_charging();
	GAUGE_info("[%s] Board offset calibration done\n", __func__);
}

/**
 * @brief batt_probe_work_func
 *
 *  Register uG31xx driver
 *
 * @para  work  address of struct work_struct
 * @return  NULL
 */
static void batt_probe_work_func(struct work_struct *work)
{
	int rtn;
	unsigned char org_op_options;
#ifdef UG31XX_PROC_DEV	
	struct proc_dir_entry *ent;
#endif  ///< end of UG31XX_PROC_DEV

#ifdef CONFIG_PF400CG
	if (!hwid) {
		op_options = op_options | LKM_OPTIONS_ENABLE_REVERSE_CURRENT;
		GAUGE_info("[%s] LKM_OPTIONS_ENABLE_REVERSE_CURRENT\n", __func__);
	}
#endif	///< end of CONFIG_PF400CG

	org_op_options = op_options & (~LKM_OPTIONS_FORCE_RESET);
	op_options = op_options | LKM_OPTIONS_DEBUG_INFO;
	ug31_module.set_options(op_options);
	rtn = ug31_module.initial(FactoryGGBXFile);
	op_options = org_op_options;
	ug31_module.set_options(op_options);
	if(rtn != 0)
	{
		goto pwr_supply_fail;
	}

	op_options = op_options | LKM_OPTIONS_ADJUST_DESIGN_CAPACITY;
	adjust_cell_table();

	ug31_module.set_shell_ap(UPI_UG31XX_SHELL_AP, strlen(UPI_UG31XX_SHELL_AP));
	ug31_module.set_backup_file_name(UPI_UG31XX_BACKUP_FILE, strlen(UPI_UG31XX_BACKUP_FILE));
	ug31_module.set_suspend_file_name(UPI_UG31XX_BACKUP_SUSPEND, strlen(UPI_UG31XX_BACKUP_SUSPEND));

	if(charge_termination_current == 0)
	{
		charge_termination_current = ug31_module.get_charge_termination_current();
	}
	ug31_module.set_charge_termination_current(charge_termination_current);
	GAUGE_info("[%s] rsense_value: %d\n", __func__, rsense_value);
	if(rsense_value == 0)
	{
		rsense_value = ug31_module.get_rsense();
	}
	ug31_module.set_rsense(rsense_value);  
	GAUGE_info("[%s] ug31_module initialized. (%s)\n", __func__, ug31_module.get_version());

#ifdef UG31XX_SHOW_EXT_TEMP
	ug31_module.set_battery_temp_external();
#else  ///< else of UG31XX_SHOW_EXT_TEMP
	ug31_module.set_battery_temp_internal();
#endif  ///< end of UG31XX_SHOW_EXT_TEMP
	GAUGE_info("[%s] set battery temperature from external\n", __func__);
	/*tan
	INIT_DELAYED_WORK_DEFERRABLE(&ug31->batt_info_update_work, batt_info_update_work_func);
	INIT_DELAYED_WORK_DEFERRABLE(&ug31->batt_power_update_work, batt_power_update_work_func);
	INIT_DELAYED_WORK_DEFERRABLE(&ug31->board_offset_cali_work, board_offset_cali_work_func);
	*/
	INIT_DELAYED_WORK(&ug31->batt_info_update_work, batt_info_update_work_func);
	INIT_DELAYED_WORK(&ug31->batt_power_update_work, batt_power_update_work_func);
	INIT_DELAYED_WORK(&ug31->board_offset_cali_work, board_offset_cali_work_func);
	if(ug31xx_powersupply_init(ug31->client))
	{
		goto pwr_supply_fail;
	}

	ug31xx_drv_status = UG31XX_DRV_INIT_OK;

#ifdef	UPI_CALLBACK_FUNC

	if (cur_cable_status)
	{
		ug31xx_cable_callback(cur_cable_status);
	} 

#endif	///< end of UPI_CALLBACK_FUNC

#ifdef UG31XX_MISC_DEV

	rtn = misc_register(&ug31xx_misc);
	if(rtn < 0)
	{
		GAUGE_err("[%s] Unable to register misc deive\n", __func__);
		misc_deregister(&ug31xx_misc);
	}

#endif	///< end of UG31XX_MISC_DEV
/*tan
#ifdef UG31XX_PROC_DEV
	ent = create_proc_read_entry("ROSC", 0744, NULL, ug31xx_get_proc_rsoc, NULL ); 
	if(!ent)
	{
		GAUGE_err("create /proc/ROSC fail\n");
	}
	ent = create_proc_read_entry("PSOC", 0744, NULL, ug31xx_get_proc_psoc, NULL );
	if(!ent)
	{
		GAUGE_err("create /proc/PSOC fail\n");
	}
	ent = create_proc_read_entry("RM", 0744, NULL, ug31xx_get_proc_rm, NULL );
	if(!ent)
	{
		GAUGE_err("create /proc/RM fail\n");
	}
	ent = create_proc_read_entry("FCC", 0744, NULL, ug31xx_get_proc_fcc, NULL );
	if(!ent)
	{
		GAUGE_err("create /proc/FCC fail\n");
	}
	ent = create_proc_read_entry("bat_current", 0744, NULL, ug31xx_get_proc_curr, NULL );
	if(!ent)
	{
		GAUGE_err("create /proc/bat_current fail\n");
	}
	ent = create_proc_read_entry("upi_60", 0744, NULL, ug31xx_get_proc_upi_60, NULL);
	if(!ent)
	{
		GAUGE_err("create /proc/upi_60 fail\n");
	}
	ent = create_proc_read_entry("upi_auto", 0744, NULL, ug31xx_get_proc_upi_auto, NULL);
	if(!ent)
	{
		GAUGE_err("create /proc/upi_auto fail\n");
	}
#endif	///< end of UG31XX_PROC_DEV
*/
	/* request charger driver to update "power supply changed" */
	//tan request_power_supply_changed();
	//power_supply_changed(&ug31xx_supply[PWR_SUPPLY_BATTERY]);//tan#

	/* register switch device for battery information versions report */
	batt_dev.name = "battery";
	batt_dev.print_name = batt_switch_name;
	/*tan
	if (switch_dev_register(&batt_dev) < 0) {
		GAUGE_err("%s: fail to register battery switch\n", __func__);
		goto pwr_supply_fail;
	}*/
	ug31xx_config_earlysuspend(ug31);

	if(probe_with_cable == true)
	{
		ug31_module.calibrate_offset(UG31XX_BOARD_OFFSET_CALI_FULL);
	}
	start_charging();

	schedule_delayed_work(&ug31->batt_info_update_work, 0*HZ);

	force_power_supply_change = true;
	GAUGE_info("[%s] Driver %s registered done\n", __func__, ug31->client->name);
	return;

pwr_supply_fail:
	start_charging();
	kfree(ug31);
}

//tan static __devinit int ug31xx_i2c_probe(struct i2c_client *client,
static  int ug31xx_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	pr_info("<BATT> ++++++++++++++++ %s ++++++++++++++++\n", __func__);
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
	{
		return -EIO;
	} 

	ug31 = kzalloc(sizeof(*ug31), GFP_KERNEL);
	if (!ug31)
	{
		return -ENOMEM;
	} 

	show_default_batt_status();
  
	ug31->client = client;
	ug31->dev = &client->dev;

	i2c_set_clientdata(client, ug31);
	API_I2C_Init(ug31->client);

	mutex_init(&ug31->info_update_lock);
	wake_lock_init(&ug31->batt_wake_lock, WAKE_LOCK_SUSPEND, "ug31xx_driver_update");

	//tan INIT_DELAYED_WORK_DEFERRABLE(&ug31->batt_probe_work, batt_probe_work_func);
	INIT_DELAYED_WORK(&ug31->batt_probe_work, batt_probe_work_func);//tan
#ifdef  UG31XX_PROBE_CHARGER_OFF
	//probe_with_cable = is_charging();
	if(probe_with_cable == true)
	{
		schedule_delayed_work(&ug31->batt_probe_work, UG31XX_PROBE_CHARGER_OFF_DELAY*HZ);
		GAUGE_info("[%s] Wait %d seconds to register driver %s\n", __func__, UG31XX_PROBE_CHARGER_OFF_DELAY, ug31->client->name);
		stop_charging();
	}
	else
	{
		schedule_delayed_work(&ug31->batt_probe_work, 0*HZ);
		GAUGE_info("[%s] No wait because not in charging\n", __func__);
	}

#else   ///< else of UG31XX_PROBE_CHARGER_OFF

	schedule_delayed_work(&ug31->batt_probe_work, 0*HZ);

#endif  ///< end of UG31XX_PROBE_CHARGER_OFF

	return 0;
}

//tan static __devexit int ug31xx_i2c_remove(struct i2c_client *client)
static  int ug31xx_i2c_remove(struct i2c_client *client)
{
	struct ug31xx_gauge *ug31_dev;

	#ifdef  UG31XX_REGISTER_POWERSUPPLY

  	int i;

	#endif  ///< end of UG31XX_REGISTER_POWERSUPPLY

	mutex_lock(&ug31->info_update_lock);
	wake_lock_destroy(&ug31->batt_wake_lock);

	#ifdef  UG31XX_REGISTER_POWERSUPPLY

	for (i = 0; i < ARRAY_SIZE(ug31xx_supply); i++)
	{
		power_supply_unregister(&ug31xx_supply[i]);
	}
    
	#endif  ///< end of UG31XX_REGISTER_POWERSUPPLY
  
	#ifdef UG31XX_MISC_DEV

	misc_deregister(&ug31xx_misc);

	#endif	///< end of UG31XX_MISC_DEV

	#ifdef UG31XX_PROC_DEV

	remove_proc_entry( "RSOC",NULL ); 
	remove_proc_entry( "PSOC",NULL ); 
	remove_proc_entry( "RM",NULL );
	remove_proc_entry( "FCC",NULL );
	remove_proc_entry( "bat_current",NULL );  
	remove_proc_entry("upi_60", NULL);
	remove_proc_entry("upi_auto", NULL);

	#endif	///< end of UG31XX_PROC_DEV

	cancel_delayed_work_sync(&ug31->batt_info_update_work);
	cancel_delayed_work_sync(&ug31->batt_power_update_work);

	ug31_module.uninitial();
	GAUGE_info("[%s] Driver removed.\n", __func__);

	ug31_dev = i2c_get_clientdata(client);
	if(ug31_dev)
	{
		kfree(ug31_dev);
	}

	mutex_unlock(&ug31->info_update_lock);
	mutex_destroy(&ug31->info_update_lock);

	if(ug31)
	{
		kfree(ug31);
	}
	return 0;
}

static int ug31xx_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	#if !(defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND))

	int gg_status;

	mutex_lock(&ug31->info_update_lock);
	ug31_module.set_backup_file(false);
	mutex_unlock(&ug31->info_update_lock);

	cancel_delayed_work_sync(&ug31->batt_info_update_work);

	charger_dc_in_before_suspend = is_charging();

	mutex_lock(&ug31->info_update_lock);
	gg_status = ug31_module.suspend((charger_dc_in_before_suspend == true) ? 1 : 0);
	if(gg_status == 0)
	{
		show_update_batt_status();
		GAUGE_info("[%s] Driver suspend.\n", __func__);
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail in suspend.\n", __func__);
	}
	rsoc_before_suspend = ug31_module.get_relative_state_of_charge();
	mutex_unlock(&ug31->info_update_lock);

	#endif	///< end of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 

	mutex_lock(&ug31->info_update_lock);
	ug31_module.set_capacity_suspend_mode(_UPI_TRUE_);
	mutex_unlock(&ug31->info_update_lock);

	ug31xx_drv_status = UG31XX_DRV_SUSPEND;
	GAUGE_info("[%s] ug31xx_i2c_suspend() end\n", __func__);
	return 0;
}

static int ug31xx_i2c_resume(struct i2c_client *client)
{
	#if !(defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND))

	int gg_status;
	int prev_fc_sts;
	int now_fc_sts;
	bool charger_dc_in_after_resume;
	int prev_rsoc;

	charger_dc_in_after_resume = is_charging();
  
	mutex_lock(&ug31->info_update_lock);
	prev_fc_sts = ug31_module.get_full_charge_status();
	prev_rsoc = rsoc_before_suspend;	
	gg_status = ug31_module.resume();
	delta_q_in_suspend = delta_q_in_suspend + ug31_module.get_delta_q();
	now_fc_sts = ug31_module.get_full_charge_status();
	if(gg_status == 0)
	{
		if(charger_detect_full == true)
		{
			charger_detect_full = false;
			gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
		}

		#ifndef UG31XX_WAIT_CHARGER_FC

		if(charger_dc_in_before_suspend == false)
		{
			GAUGE_info("[%s] Enter suspend without charger\n", __func__);
		}
		else
		{
			GAUGE_info("[%s] Enter suspend with charger\n", __func__);
			if((prev_fc_sts == 0) && (now_fc_sts == 1))
			{
				gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
				GAUGE_info("[%s] [FC] status changed (0->1)\n", __func__);
			}
			else
			{
				if(charger_dc_in_after_resume == false)
				{
					GAUGE_info("[%s] Exit suspend without charger\n", __func__);
					prev_rsoc = (100 - prev_rsoc)*(ug31_module.get_design_capacity())/100;
					if(delta_q_in_suspend > prev_rsoc)
					{
						GAUGE_info("[%s] Charged capacity is enouth to check full charged (%d) > (%d)\n", __func__, delta_q_in_suspend, prev_rsoc);

						if(ug31_module.get_predict_rsoc() > 90)
						{
							gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
							GAUGE_info("[%s] Set to full charge status\n", __func__);
						}
					}
				}
				else
				{
					GAUGE_info("[%s] Exit suspend with charger\n", __func__);
					if(is_charging_full() == 1)
					{
						GAUGE_info("[%s] Charger detects full\n", __func__);
						gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
						GAUGE_info("[%s] Set to full charge status\n", __func__);
					}
					else
					{
						prev_rsoc = (100 - prev_rsoc)*(ug31_module.get_design_capacity())/100;
						if(delta_q_in_suspend > prev_rsoc)
						{
							GAUGE_info("[%s] Charged capacity is enouth to check full charged (%d) > (%d)\n", __func__, delta_q_in_suspend, prev_rsoc);
							if(ug31_module.get_predict_rsoc() > 95)
							{
								gg_status = ug31_module.set_charger_full(UG31XX_CHARGER_DETECTS_FULL);
								GAUGE_info("[%s] Set to full charge status\n", __func__);
							}
						}
					}
				}
			}
		}

		#endif  ///< end of UG31XX_WAIT_CHARGER_FC

		if(gg_status == 0)
		{
			show_update_batt_status();
			GAUGE_info("[%s] Gauge resumed.\n", __func__);
		}
		else
		{
			show_abnormal_batt_status();
			GAUGE_err("[%s] Fail to set charger full.\n", __func__);
		}
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail to resume.\n", __func__);
	}
	mutex_unlock(&ug31->info_update_lock);

	schedule_delayed_work(&ug31->batt_info_update_work, 0*HZ);

	force_power_supply_change = true;

	#else   ///< else of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 

	int time_interval;

	mutex_lock(&ug31->info_update_lock);
	time_interval = ug31_module.get_update_interval();
	mutex_unlock(&ug31->info_update_lock);
	if(cable_status_changed > 0)
	{
		cable_status_changed = 0;
	}
	else
	{
		if(time_interval > MAX_BATTERY_UPDATE_INTERVAL)
		{
			batt_info_update_work_func(&ug31->batt_info_update_work.work);
			GAUGE_info("[%s]: time interval = %d > %d -> re-schedule batt_info_update_work.\n", __func__, time_interval, MAX_BATTERY_UPDATE_INTERVAL);
		}
	}
  
	mutex_lock(&ug31->info_update_lock);
	ug31_module.set_capacity_suspend_mode(_UPI_FALSE_);
	mutex_unlock(&ug31->info_update_lock);

	cancel_delayed_work_sync(&ug31->batt_info_update_work);
	schedule_delayed_work(&ug31->batt_info_update_work, ug31->update_time*HZ);
  
	#endif	///< end of defined(CONFIG_HAS_EARLYSUSPEND) && defined(UG31XX_EARLY_SUSPEND) 

	ug31xx_drv_status = UG31XX_DRV_INIT_OK;

	GAUGE_info("[%s] ug31xx_i2c_resume() end\n", __func__);
	return 0;
}

static void ug31xx_i2c_shutdown(struct i2c_client *client)
{
	int gg_status = 0;

	cancel_delayed_work_sync(&ug31->batt_info_update_work);
	cancel_delayed_work_sync(&ug31->batt_power_update_work);

	mutex_lock(&ug31->info_update_lock);
	gg_status = ug31_module.shutdown();
	if(gg_status == 0)
	{
		show_update_batt_status();
		GAUGE_info("[%s] Gauge driver shutdown.\n", __func__);
	}
	else
	{
		show_abnormal_batt_status();
		GAUGE_err("[%s] Fail to shutdown gauge.\n", __func__);
	}
	mutex_unlock(&ug31->info_update_lock);
	GAUGE_info("[%s] Driver shutdown. gg_status=0x%02x\n", __func__, gg_status);
}

static const struct i2c_device_id ug31xx_i2c_id[] = {
	{ UG31XX_DEV_NAME, 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ug31xx_i2c_id);

#ifndef CHRIS
static const struct dev_pm_ops ug31xx_pm_ops = {
	.suspend = ug31xx_i2c_suspend,
	.resume = ug31xx_i2c_resume,
};
#endif

static struct i2c_driver ug31xx_i2c_driver = {
	.driver    = {
		.name  = UG31XX_DEV_NAME,
		.owner = THIS_MODULE,
#ifndef CHRIS
		.pm	= &ug31xx_pm_ops,
#endif
	},
	.probe     = ug31xx_i2c_probe,
	//tan .remove    = __devexit_p(ug31xx_i2c_remove),
	.remove    = ug31xx_i2c_remove,
#ifdef CHRIS
	.suspend   = ug31xx_i2c_suspend,
	.resume    = ug31xx_i2c_resume,
#endif
	.shutdown  = ug31xx_i2c_shutdown,
	.id_table  = ug31xx_i2c_id,
};

#ifdef	UG31XX_REGISTER_I2C

static struct i2c_board_info ug31xx_i2c_board_info = {
	.type          = "ug31xx-gauge",
	.flags         = 0x00,
	.addr          = 0x70,
	.platform_data = NULL,
	.archdata      = NULL,
	.irq           = -1,
};


#define	UG31XX_I2C_ADAPTER	(1)

static struct i2c_client *i2c_client;
static struct i2c_adapter *i2c_adap;

#endif	///< end of UG31XX_REGISTER_I2C

static int __init ug31xx_i2c_init(void)
{
	int ret = 0;

	//tan if (entry_mode == 5) return -1;
	//tan hwid = Read_HW_ID();	ok
	pr_info("<BATT> ++++++++++++++++ %s ++++++++++++++++\n", __func__);

	#ifdef	UG31XX_REGISTER_I2C
		i2c_adap = i2c_get_adapter(UG31XX_I2C_ADAPTER);
		if (!i2c_adap) 
		{
			GAUGE_err("[%s] Cannot get i2c adapter %d\n", __func__, UG31XX_I2C_ADAPTER);
			ret = -ENODEV;
			goto err1;
		}


		i2c_client = i2c_new_device(i2c_adap, &ug31xx_i2c_board_info);
		if (!i2c_client) {
			GAUGE_err("[%s] Unable to add I2C device for 0x%x\n", __func__,
				   ug31xx_i2c_board_info.addr);
			ret = -ENODEV;
			goto err2;
		}
	#endif	///< end of UG31XX_REGISTER_I2C

	ret =  i2c_add_driver(&ug31xx_i2c_driver);
	if (ret)
	{
		#ifdef	UG31XX_REGISTER_I2C
			goto err3;
		#else	///< else of UG31XX_REGISTER_I2C
			goto err1;
		#endif	///< end of UG31XX_REGISTER_I2C
	}

	return 0;

#ifdef	UG31XX_REGISTER_I2C
err3:
	i2c_unregister_device(i2c_client);
err2:
	i2c_put_adapter(i2c_adap);
#endif	///< end of UG31XX_REGISTER_I2C
err1:
	return ret;
}
//tan module_init(ug31xx_i2c_init); 
late_initcall(ug31xx_i2c_init);

static void __exit ug31xx_i2c_exit(void)
{
	#ifdef	UG31XX_REGISTER_I2C
		i2c_put_adapter(i2c_adap);
	#endif	///< end of UG31XX_REGISTER_I2C
	i2c_del_driver(&ug31xx_i2c_driver);
	#ifdef	UG31XX_REGISTER_I2C
		i2c_unregister_device(i2c_client);
	#endif	///< end of UG31XX_REGISTER_I2C
}
module_exit(ug31xx_i2c_exit);

MODULE_DESCRIPTION("ug31xx gauge driver");
MODULE_LICENSE("GPL");

module_param(op_options, byte, 0644);
MODULE_PARM_DESC(op_options, "Set operation options for uG31xx driver.");
module_param(design_capacity, ushort, 0644);
MODULE_PARM_DESC(design_capacity, "Set new design capacity for uG31xx driver.");

