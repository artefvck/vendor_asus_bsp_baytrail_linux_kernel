/*
 * bq24192_charger.c - Charger driver for TI BQ24192,BQ24191 and BQ24190
 *
 * Copyright (C) 2011 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 * Author: Raj Pandey <raj.pandey@intel.com>
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/power/bq24192_charger.h>
#include <linux/sfi.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/version.h>
#include <linux/usb/otg.h>
#include <linux/platform_data/intel_mid_remoteproc.h>
#include <linux/rpmsg.h>

#include <asm/intel_mid_gpadc.h>
#include <asm/intel_scu_ipc.h>
#include <asm/intel_scu_pmic.h>
#include <asm/intel_mid_rpmsg.h>

#define DRV_NAME "bq24192_charger"
#define DEV_NAME "bq24192"
#include <linux/acpi_gpio.h>
#include <linux/mfd/intel_mid_pmic.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>


#define REGULATOR_V3P3S		"v3p3s"

#define R_PMIC_CHGRIRQ 0x0A 
#define R_PMIC_MIRQS0  0x17
#define R_PMIC_MIRQSX  0x18


/*
 * D0, D1, D2 can be used to set current limits
 * and D3, D4, D5, D6 can be used to voltage limits
 */
#define BQ24192_INPUT_SRC_CNTL_REG		0x0
#define INPUT_SRC_CNTL_EN_HIZ			(1 << 7)
#define BATTERY_NEAR_FULL(a)			((a * 98)/100)
/*
 * set input voltage lim to 4.68V. This will help in charger
 * instability issue when duty cycle reaches 100%.
 */
#define INPUT_SRC_VOLT_LMT_DEF                 (3 << 4)
#define INPUT_SRC_VOLT_LMT_444                 (7 << 3)
#define INPUT_SRC_VOLT_LMT_468                 (5 << 4)
#define INPUT_SRC_VOLT_LMT_476                 (0xB << 3)

#define INPUT_SRC_VINDPM_MASK                  (0xF << 3)
#define INPUT_SRC_LOW_VBAT_LIMIT               3600
#define INPUT_SRC_MIDL_VBAT_LIMIT              4000
#define INPUT_SRC_MIDH_VBAT_LIMIT              4200
#define INPUT_SRC_HIGH_VBAT_LIMIT              4350

/* D0, D1, D2 represent the input current limit */
#define INPUT_SRC_CUR_LMT0		0x0	/* 100mA */
#define INPUT_SRC_CUR_LMT1		0x1	/* 150mA */
#define INPUT_SRC_CUR_LMT2		0x2	/* 500mA */
#define INPUT_SRC_CUR_LMT3		0x3	/* 900mA */
#define INPUT_SRC_CUR_LMT4		0x4	/* 1000mA */
#define INPUT_SRC_CUR_LMT5		0x5	/* 1200mA */
#define INPUT_SRC_CUR_LMT6		0x6	/* 2000mA */
#define INPUT_SRC_CUR_LMT7		0x7	/* 3000mA */

/*
 * D1, D2, D3 can be used to set min sys voltage limit
 * and D4, D5 can be used to control the charger
 */
#define BQ24192_POWER_ON_CFG_REG		0x1
#define POWER_ON_CFG_RESET			(1 << 7)
#define POWER_ON_CFG_I2C_WDTTMR_RESET		(1 << 6)
#define CHR_CFG_BIT_POS				4
#define CHR_CFG_BIT_LEN				2
#define POWER_ON_CFG_CHRG_CFG_DIS		(0 << 4)
#define POWER_ON_CFG_CHRG_CFG_EN		(1 << 4)
#define POWER_ON_CFG_CHRG_CFG_OTG		(3 << 4)
#define POWER_ON_CFG_BOOST_LIM			(1 << 0)

/*
 * Charge Current control register
 * with range from 500 - 4532mA
 */
#define BQ24192_CHRG_CUR_CNTL_REG		0x2
#define BQ24192_CHRG_CUR_OFFSET		512	/* 500 mA */
#define BQ24192_CHRG_CUR_LSB_TO_CUR	64	/* 64 mA */
#define BQ24192_GET_CHRG_CUR(reg) ((reg>>2)*BQ24192_CHRG_CUR_LSB_TO_CUR\
			+ BQ24192_CHRG_CUR_OFFSET) /* in mA */
#define BQ24192_CHRG_ITERM_OFFSET       128
#define BQ24192_CHRG_CUR_LSB_TO_ITERM   128

/* Pre charge and termination current limit reg */
#define BQ24192_PRECHRG_TERM_CUR_CNTL_REG	0x3
#define BQ24192_TERM_CURR_LIMIT_128		0	/* 128mA */
#define BQ24192_PRE_CHRG_CURR_256		(1 << 4)  /* 256mA */

/* Charge voltage control reg */
#define BQ24192_CHRG_VOLT_CNTL_REG	0x4
#define BQ24192_CHRG_VOLT_OFFSET	3504	/* 3504 mV */
#define BQ24192_CHRG_VOLT_LSB_TO_VOLT	16	/* 16 mV */
/* Low voltage setting 0 - 2.8V and 1 - 3.0V */
#define CHRG_VOLT_CNTL_BATTLOWV		(1 << 1)
/* Battery Recharge threshold 0 - 100mV and 1 - 300mV */
#define CHRG_VOLT_CNTL_VRECHRG		(0 << 0)
#define BQ24192_GET_CHRG_VOLT(reg) ((reg>>2)*BQ24192_CHRG_VOLT_LSB_TO_VOLT\
			+ BQ24192_CHRG_VOLT_OFFSET) /* in mV */

/* Charge termination and Timer control reg */
#define BQ24192_CHRG_TIMER_EXP_CNTL_REG		0x5
#define CHRG_TIMER_EXP_CNTL_EN_TERM		(1 << 7)
#define CHRG_TIMER_EXP_CNTL_TERM_STAT		(1 << 6)
/* WDT Timer uses 2 bits */
#define WDT_TIMER_BIT_POS			4
#define WDT_TIMER_BIT_LEN			2
#define CHRG_TIMER_EXP_CNTL_WDTDISABLE		(0 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT40SEC		(1 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT80SEC		(2 << 4)
#define CHRG_TIMER_EXP_CNTL_WDT160SEC		(3 << 4)
#define WDTIMER_RESET_MASK			0x40
/* Safety Timer Enable bit */
#define CHRG_TIMER_EXP_CNTL_EN_TIMER		(1 << 3)
/* Charge Timer uses 2bits(20 hrs) */
#define SFT_TIMER_BIT_POS			1
#define SFT_TIMER_BIT_LEN			2
#define CHRG_TIMER_EXP_CNTL_SFT_TIMER		(3 << 1)

#define BQ24192_CHRG_THRM_REGL_REG		0x6

#define BQ24192_MISC_OP_CNTL_REG		0x7
#define MISC_OP_CNTL_DPDM_EN			(1 << 7)
#define MISC_OP_CNTL_TMR2X_EN			(1 << 6)
#define MISC_OP_CNTL_BATFET_DIS			(1 << 5)
#define MISC_OP_CNTL_BATGOOD_EN			(1 << 4)
/* To mask INT's write 0 to the bit */
#define MISC_OP_CNTL_MINT_CHRG			(1 << 1)
#define MISC_OP_CNTL_MINT_BATT			(1 << 0)

#define BQ24192_SYSTEM_STAT_REG			0x8
/* D6, D7 show VBUS status */
#define SYSTEM_STAT_VBUS_BITS			(3 << 6)
#define SYSTEM_STAT_VBUS_UNKNOWN		0
#define SYSTEM_STAT_VBUS_HOST			(1 << 6)
#define SYSTEM_STAT_VBUS_ADP			(2 << 6)
#define SYSTEM_STAT_VBUS_OTG			(3 << 6)
/* D4, D5 show charger status */
#define SYSTEM_STAT_NOT_CHRG			(0 << 4)
#define SYSTEM_STAT_PRE_CHRG			(1 << 4)
#define SYSTEM_STAT_FAST_CHRG			(2 << 4)
#define SYSTEM_STAT_CHRG_DONE			(3 << 4)
#define SYSTEM_STAT_DPM				(1 << 3)
#define SYSTEM_STAT_PWR_GOOD			(1 << 2)
#define SYSTEM_STAT_THERM_REG			(1 << 1)
#define SYSTEM_STAT_VSYS_LOW			(1 << 0)
#define SYSTEM_STAT_CHRG_MASK			(3 << 4)

#define BQ24192_FAULT_STAT_REG			0x9
#define FAULT_STAT_WDT_TMR_EXP			(1 << 7)
#define FAULT_STAT_OTG_FLT			(1 << 6)
/* D4, D5 show charger fault status */
#define FAULT_STAT_CHRG_BITS			(3 << 4)
#define FAULT_STAT_CHRG_NORMAL			(0 << 4)
#define FAULT_STAT_CHRG_IN_FLT			(1 << 4)
#define FAULT_STAT_CHRG_THRM_FLT		(2 << 4)
#define FAULT_STAT_CHRG_TMR_FLT			(3 << 4)
#define FAULT_STAT_BATT_FLT			(1 << 3)
#define FAULT_STAT_BATT_TEMP_BITS		(3 << 0)

#define BQ24192_VENDER_REV_REG			0xA
/* D3, D4, D5 indicates the chip model number */
#define BQ24190_IC_VERSION			0x0
#define BQ24191_IC_VERSION			0x1
#define BQ24192_IC_VERSION			0x2
#define BQ24192I_IC_VERSION			0x3
#define BQ2419x_IC_VERSION			0x4

#define BQ24192_MAX_MEM		12
#define NR_RETRY_CNT		3

#define CHARGER_PS_NAME				"bq24192_charger"

#define CHARGER_TASK_JIFFIES		(HZ * 150)/* 150sec */
#define CHARGER_HOST_JIFFIES		(HZ * 60) /* 60sec */
#define FULL_THREAD_JIFFIES		(HZ * 30) /* 30sec */
#define TEMP_THREAD_JIFFIES		(HZ * 30) /* 30sec */

#define BATT_TEMP_MAX_DEF	60	/* 60 degrees */
#define BATT_TEMP_MIN_DEF	0

/* Max no. of tries to clear the charger from Hi-Z mode */
#define MAX_TRY		3

/* Max no. of tries to reset the bq24192i WDT */
#define MAX_RESET_WDT_RETRY 8

/*
 * usb notify callback
 */
#define USB_NOTIFY_CALLBACK

static struct power_supply *fg_psy;
struct bq24192_chip *chip_extern=NULL;
extern int entry_mode;

//...........................................................................
static int bq2415x_sysfs_flag=0; //lambert
static int suspend_flag=1;  //lambert,1:normal charger,0:mode for aging
static unsigned char WakeLockFlag=0;//lambert,0:already unlock,1:already lock
static int g_charger_mode = -1; //lambert,0:dc,1:usb,2:ac

//.......................................................................................................
struct bq24192_otg_event {
	struct list_head node;
	bool is_enable;
};

static enum bq24192_chrgr_stat {
	BQ24192_CHRGR_STAT_UNKNOWN =0,
	BQ24192_CHRGR_STAT_CHARGING,
	BQ24192_CHRGR_STAT_FAULT,
	BQ24192_CHRGR_STAT_LOW_SUPPLY_FAULT,
	BQ24192_CHRGR_STAT_BAT_FULL,
};

struct bq24192_chip {
	struct i2c_client *client;
	struct bq24192_platform_data *pdata;
	struct power_supply usb;
	struct delayed_work power_state_task_wrkr;
	struct delayed_work chrg_task_wrkr;
	struct delayed_work chrg_full_wrkr;
	struct delayed_work jeita_wrkr;
	struct delayed_work chrg_temp_wrkr;
	struct delayed_work otg_disable_wrkr;
	struct delayed_work otg_wrkr;
	struct work_struct otg_evt_work;
	struct notifier_block	otg_nb;
	struct list_head	otg_queue;
	struct mutex event_lock;
	struct power_supply_cable_props cap;
	struct power_supply_cable_props cached_cap;
	struct usb_phy *transceiver;
	/* Wake lock to prevent platform from going to S3 when charging */
	struct wake_lock wakelock;
	spinlock_t otg_queue_lock;


	enum bq24192_chrgr_stat chgr_stat;
	enum power_supply_charger_cable_type cable_type;
	int cc;
	int cv;
	int inlmt;
	int max_cc;
	int max_cv;
	int max_temp;
	int min_temp;
	int iterm;
	int batt_status;
	int bat_health;
	int cntl_state;
	int irq;
	bool is_charger_enabled;
	bool is_charging_enabled;
	bool votg;
	bool is_pwr_good;
	bool boost_mode;
	bool online;
	bool present;
	bool sfttmr_expired;
};

void charger_enabled_poweron();
int bq24192_chargeric_status(void);

#ifdef CONFIG_DEBUG_FS
static struct dentry *bq24192_dbgfs_root;
static char bq24192_dbg_regs[BQ24192_MAX_MEM][4];
#endif

static int bq24192_reg_read_modify(struct i2c_client *client, u8 reg,
							u8 val, bool bit_set);

static struct i2c_client *bq24192_client;

static enum power_supply_property bq24192_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MAX_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_MAX_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INLMT,
	POWER_SUPPLY_PROP_ENABLE_CHARGING,
	POWER_SUPPLY_PROP_ENABLE_CHARGER,
	POWER_SUPPLY_PROP_CHARGE_TERM_CUR,
	POWER_SUPPLY_PROP_CABLE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_MAX_TEMP,
	POWER_SUPPLY_PROP_MIN_TEMP
};

static enum power_supply_type get_power_supply_type(
		enum power_supply_charger_cable_type cable)
{

	switch (cable) {

	case POWER_SUPPLY_CHARGER_TYPE_USB_DCP:
		return POWER_SUPPLY_TYPE_USB_DCP;
	case POWER_SUPPLY_CHARGER_TYPE_USB_CDP:
		return POWER_SUPPLY_TYPE_USB_CDP;
	case POWER_SUPPLY_CHARGER_TYPE_USB_ACA:
		return POWER_SUPPLY_TYPE_USB_ACA;
	case POWER_SUPPLY_CHARGER_TYPE_AC:
		return POWER_SUPPLY_TYPE_MAINS;
	case POWER_SUPPLY_CHARGER_TYPE_NONE:
	case POWER_SUPPLY_CHARGER_TYPE_USB_SDP:
	default:
		return POWER_SUPPLY_TYPE_USB;
	}

	return POWER_SUPPLY_TYPE_USB;
}

#ifdef ASUS_ENG_BUILD
bool eng_charging_limit = true;

int asus_charging_toggle_write(struct file *file, const char *buffer, size_t count, loff_t *data) {

    if (buffer[0] == '1') {
        /* turn on charging limit in eng mode */
        eng_charging_limit = true;
    } else if (buffer[0] == '0') {
        /* turn off charging limit in eng mode */
        eng_charging_limit = false;
        charger_enabled_poweron();
    }
    printk(" %s: %s\n", __func__, eng_charging_limit ? "enable charger limit" : "disable charger limit");

    cancel_delayed_work_sync(&chip_extern->jeita_wrkr);
    schedule_delayed_work(&chip_extern->jeita_wrkr, 0*HZ);

    return count;
}

static int asus_charging_toggle_read(struct seq_file *m, void *p) {
        int len = 0;
        printk(" %s: %s\n", __func__, eng_charging_limit ? "enable charger limit" : "disable charger limit");
        seq_printf(m,"%s: %s\n", __func__, eng_charging_limit ? "enable charger limit" : "disable charger limit");
        return len;
}

static int proc_asus_charging_toggle_open(struct inode *inode, struct file *file) {
	return single_open(file, asus_charging_toggle_read, NULL);
}

static const struct file_operations asus_eng_charging_limit_ops = {
        .open		= proc_asus_charging_toggle_open,
	.read		= seq_read,
        .write          = asus_charging_toggle_write,
	.llseek		= seq_lseek,
	.release	= seq_release
};

int init_asus_charging_limit_toggle(void) {
    struct proc_dir_entry *entry = NULL;

    entry = proc_create("driver/charger_limit_enable", 0666, NULL, &asus_eng_charging_limit_ops);
    if (!entry) {
        printk("Unable to create asus_charging_toggle\n");
        return -EINVAL;
    }
    return 0;
}
#endif

unsigned char volatile VbusDetach = 1; 
unsigned char vbus_Event = 0;
#ifndef USB_NOTIFY_CALLBACK
extern volatile unsigned char PowerStateOk;
#endif
extern volatile int Power_State;

static void bq24192_ac_lock(void)
{
	unsigned long		flags;
	local_irq_save(flags);
	if(chip_extern){
		printk("bq24192_ac_lock\n");
		wake_lock(&chip_extern->wakelock);
	}
	local_irq_restore(flags);

}
static void bq24192_ac_unlock()
{
	unsigned long		flags;
	local_irq_save(flags);
	if(chip_extern){
		printk("bq24192_ac_unlock\n");
		wake_unlock(&chip_extern->wakelock);
	}
	local_irq_restore(flags);
}
#ifndef USB_NOTIFY_CALLBACK
extern enum power_supply_charger_cable_type
	GetPowerState(void);
#endif
static int bq24192_write_reg(struct i2c_client *client, u8 reg, u8 value);
extern unsigned char cur_cable_status;
unsigned char OtgOk = 0; 
static inline int bq24192_set_inlmt(struct bq24192_chip *chip, int inlmt);
/*-------------------------------------------------------------------------*/
//--------------2014-1-1----distinction sdp/dcp----------------
static int USB_STATE,AC_STATE;

extern enum {
	UG31XX_NO_CABLE = 0,
	UG31XX_USB_PC_CABLE = 1,
	UG31XX_PAD_POWER = 2,
	UG31XX_AC_ADAPTER_CABLE = 3
};

enum {
	PWR_SUPPLY_AC = 0,
	PWR_SUPPLY_USB,
	PWR_SUPPLY_bq21455
};

static enum power_supply_property bq24192_pwr_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
        POWER_SUPPLY_PROP_PRESENT
};

static char *supply_list[] = {
	"ac",
	"usb",
};

static int bq24192_power_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	int ret = 0;

	switch (psp)
	{
	case POWER_SUPPLY_PROP_ONLINE:			
		if((psy->type == POWER_SUPPLY_TYPE_MAINS))
		{
			val->intval = AC_STATE;
		}
		else if((psy->type == POWER_SUPPLY_TYPE_USB))
		{
			val->intval = USB_STATE;
		}
		else
		{
			val->intval = 0;
		}
		break;
        case POWER_SUPPLY_PROP_PRESENT:
            if (psy->type == POWER_SUPPLY_TYPE_USB) {
                /* for ATD test to acquire the status about charger ic */
                ret = bq24192_chargeric_status();
                if (ret >= 0) {
                   val->intval = 1;
                } else
                   val->intval = 0;
            } else if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
                ret = bq24192_chargeric_status();
                if (ret >= 0) {
                   val->intval = 1;
                } else
                   val->intval = 0;
            } else
               ret = -EINVAL;
        break;
	default:
		return -EINVAL;
	}
	return ret;
}

static struct power_supply bq24192_supply[] = {
	{
		.name			= "ac",
		.type			= POWER_SUPPLY_TYPE_MAINS,
		.supplied_to		= supply_list,
		.num_supplicants	= ARRAY_SIZE(supply_list),
		.properties 		= bq24192_pwr_props,
		.num_properties	= ARRAY_SIZE(bq24192_pwr_props),
		.get_property		= bq24192_power_get_property,
	},
	{
		.name			= "usb",
		.type			= POWER_SUPPLY_TYPE_USB,
		.supplied_to		= supply_list,
		.num_supplicants	= ARRAY_SIZE(supply_list),
		.properties 		= bq24192_pwr_props,
		.num_properties 	= ARRAY_SIZE(bq24192_pwr_props),
		.get_property 		= bq24192_power_get_property,
	},
	{
		.name = "bq24155",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to		= supply_list,
		.num_supplicants	= ARRAY_SIZE(supply_list),
		.properties 		= bq24192_pwr_props,
		.num_properties 	= ARRAY_SIZE(bq24192_pwr_props),
		.get_property = bq24192_power_get_property,
	},
};

static unsigned char cable_status = POWER_SUPPLY_CHARGER_TYPE_NONE;
int bq24192_cable_callback(unsigned char usb_cable_state)
{
	if(OtgOk){
		cur_cable_status = UG31XX_NO_CABLE;
		cable_status = UG31XX_NO_CABLE;
		AC_STATE   = 0;
		USB_STATE = 0;
		power_supply_changed(&bq24192_supply[PWR_SUPPLY_USB]);
		power_supply_changed(&bq24192_supply[PWR_SUPPLY_AC]);
		return 0;
	}
	printk("%s  usb_cable_state = %x\n", __func__, usb_cable_state) ;
	
	if(usb_cable_state != cable_status)
	{
		cable_status = usb_cable_state;
		if(cable_status == POWER_SUPPLY_CHARGER_TYPE_USB_SDP)
		{
                        charger_enabled_poweron();
			if(WakeLockFlag==0){
				 bq24192_ac_lock();
				 WakeLockFlag=1;
			}
			cur_cable_status = UG31XX_USB_PC_CABLE;
			AC_STATE   = 0;
			USB_STATE = 1;
			bq24192_set_inlmt(chip_extern,500);
		}else
		{
			if((cable_status == POWER_SUPPLY_CHARGER_TYPE_USB_DCP))
			{
                                charger_enabled_poweron();
				if(WakeLockFlag==0){
				      bq24192_ac_lock();
				      WakeLockFlag=1;
				}
				cur_cable_status = UG31XX_AC_ADAPTER_CABLE;
				AC_STATE   = 1;
				USB_STATE = 0;
                                bq24192_set_inlmt(chip_extern,1200);
			}else
			{
				if(WakeLockFlag==1){
					 WakeLockFlag=0;
					 bq24192_ac_unlock();
				}
				cur_cable_status = UG31XX_NO_CABLE;
				AC_STATE   = 0;
				USB_STATE = 0;
				//bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
			}
		}
		power_supply_changed(&bq24192_supply[PWR_SUPPLY_USB]);
		power_supply_changed(&bq24192_supply[PWR_SUPPLY_AC]);
		power_supply_changed(&bq24192_supply[PWR_SUPPLY_bq21455]);
	} 
	return 0;
}
EXPORT_SYMBOL(bq24192_cable_callback);

static int bq24192_powersupply_init(struct i2c_client *client)
{
	int i, ret;
	for (i = 0; i < ARRAY_SIZE(bq24192_supply); i++) {
		ret = power_supply_register(&client->dev, &bq24192_supply[i]);
		if (ret) {
			printk("[%s] Failed to register power supply\n", __func__);
			do {
				power_supply_unregister(&bq24192_supply[i]);
			} while ((--i) >= 0);
			return ret;
		}
	}
  return 0;
}
//--------------2014-1-1----distinction sdp/dcp----------------ends

//Charge Portint Guide 2014-01-20------------------------------start
static inline int bq24192_set_inlmt(struct bq24192_chip *chip, int inlmt);
static inline int bq24192_set_iterm(struct bq24192_chip *chip, int iterm);
static inline int bq24192_set_cc(struct bq24192_chip *chip, int cc);
static inline int bq24192_set_cv(struct bq24192_chip *chip, int cv);
static int bq24192_read_reg(struct i2c_client *client, u8 reg);
static inline int bq24192_get_inlmt(struct bq24192_chip *chip);

static u8 chrg_vindpm_to_reg(int vindpm)
{
	u8 reg,reg1,value;

	value = bq24192_read_reg(chip_extern->client, BQ24192_INPUT_SRC_CNTL_REG);
	reg	= (vindpm-3880)/80;
	reg1 = (vindpm-3880)%80;

	if((reg1 > 0) && (reg1 < 80))
		reg += 1;
	
	reg = reg<<3;
	value = ((value&0x87)|reg);
	return value;
}

static inline int bq24192_set_vindpm(struct bq24192_chip *chip, int vindpm)
{
	u8 regval;

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, vindpm);
	regval = chrg_vindpm_to_reg(vindpm);

	return bq24192_write_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG,
				regval);
}

static inline int bq24192_set_boostlim(u8 lim)
{
	u8 ret,value;
	
	dev_warn(&chip_extern->client->dev, "%s\n", __func__);

	value = bq24192_read_reg(chip_extern->client, BQ24192_POWER_ON_CFG_REG);
	if(lim == 0)
		value = (value&0xFE);
	else
		value = (value|0x01);
	return bq24192_write_reg(chip_extern->client, BQ24192_POWER_ON_CFG_REG,value);
}

static inline int bq24192_set_sysmin(int sysmin)
{
	int ret,value,regval;
	
	dev_warn(&chip_extern->client->dev, "%s:%d %d\n", __func__, __LINE__, sysmin);

	value = bq24192_read_reg(chip_extern->client, BQ24192_POWER_ON_CFG_REG);

	regval = (sysmin - 3000) / 100;
	if(((sysmin - 3000) % 100) > 0)
		regval += 1;
	regval = regval<<1;
	value = (value&0xF1)|regval;
		
	return bq24192_write_reg(chip_extern->client, BQ24192_POWER_ON_CFG_REG,value);
}

static int bq24192_clear_hiz(struct bq24192_chip *chip);
static int program_timers(struct bq24192_chip *chip, int wdt_duration,
				bool sfttmr_enable);
/*SDP 32 1d 54 11 b3 b4 73, 4b 6c 90 60*/
static void ChargeInit(unsigned int power_type)
{
	int ret,value;
	
	/* Clear the charger from Hi-Z */
	if(AC_STATE||USB_STATE)
	{
	  ret = bq24192_clear_hiz(chip_extern);
	  if (ret < 0)
		dev_warn(&chip_extern->client->dev, "HiZ clear failed:\n");
	}
	/*Set boost_limit 1.5a*/
	bq24192_set_boostlim(1);

	/*Set minimum system voltage limit 3.6v*/
	bq24192_set_sysmin(3600);

	/*BCOLD:0 , ICHG as reg02[7:2] programmed*/
	value = bq24192_read_reg(chip_extern->client, BQ24192_CHRG_CUR_CNTL_REG);
	value &= 0xFC;
	bq24192_write_reg(chip_extern->client, BQ24192_CHRG_CUR_CNTL_REG,value);

	/*boost voltage 4.998v,thermal regulation 120c*/
	bq24192_write_reg(chip_extern->client, BQ24192_CHRG_THRM_REGL_REG,0x73);

        value = bq24192_get_inlmt(chip_extern);
	if (power_type == POWER_SUPPLY_CHARGER_TYPE_USB_DCP || value >= 1200){
		bq24192_set_inlmt(chip_extern,1200);	//00	inlim:1200mA
	} else { //POWER_SUPPLY_CHARGER_TYPE_USB_SDP
		bq24192_set_inlmt(chip_extern,500);	//00	inlim:500mA
	}
		
	bq24192_set_vindpm(chip_extern,4360);	//00	vindpm:4.36v
	bq24192_set_iterm(chip_extern,256);		//03	BQ24192_PRE_CHRG_CURR_256 & termination current limit
	bq24192_set_cc(chip_extern,2048);		//02	fast charge current linit 1856ma
	bq24192_set_cv(chip_extern,4208);		//04	charge voltage 4.208v & pre-charge to fast charge 3.0v & 300mv

	/* Enable the WDT and Disable Safety timer */
	ret = program_timers(chip_extern, CHRG_TIMER_EXP_CNTL_WDT160SEC, false);
	if (ret < 0)
		dev_warn(&chip_extern->client->dev, "TIMER enable failed\n");

	/*Enable termination*/
	value = bq24192_read_reg(chip_extern->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG);
	value |= 0x80;
	bq24192_write_reg(chip_extern->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG,value);

	/*Mask INT*/
	value = bq24192_read_reg(chip_extern->client, BQ24192_MISC_OP_CNTL_REG);
	value &= 0xFC;
	bq24192_write_reg(chip_extern->client, BQ24192_MISC_OP_CNTL_REG,value);

	/*Clear fault register*/
	bq24192_read_reg(chip_extern->client, BQ24192_FAULT_STAT_REG);
	
}



static void otg_worker(struct work_struct *work)
{
	int gpio_handle,gpio_data;

	msleep(300);
	gpio_handle = acpi_get_gpio("\\_SB.GPO2", 22);
	gpio_data    = __gpio_get_value(gpio_handle);
	if((gpio_data == 0)&&(OtgOk == 0)){
		printk("Otg plug in\n");
		OtgOk = 1;
		cur_cable_status = UG31XX_NO_CABLE;
		cable_status = UG31XX_NO_CABLE;
		bq24192_reg_read_modify(chip_extern->client,BQ24192_POWER_ON_CFG_REG,0x20, true);	
	}
	else{
		if((gpio_data == 1)&&(OtgOk == 1))
		{
		printk("Otg plug out\n");
		OtgOk = 0;
		bq24192_reg_read_modify(chip_extern->client,BQ24192_POWER_ON_CFG_REG,0x20, false);	
		}
	}
}
static irqreturn_t otg_irq_thread(int irq, void *devid)
{
        // callback from USB notify
	//schedule_delayed_work(&chip_extern->otg_wrkr, 0*HZ);
}
static irqreturn_t otg_irq_isr(int irq, void *devid)
{
	return IRQ_WAKE_THREAD;
}

//Charge Portint Guide 2014-01-20------------------------------end

static int bq24192_reg_read_modify(struct i2c_client *client, u8 reg,
							u8 val, bool bit_set);
extern bool is_upi_do_disablecharger;
void bq24192_charge_enable(int enable) {

        if (is_upi_do_disablecharger) {
           printk("=========== UPI gauge do stop charging, not do JETIA ==================\n");     
           return;
        }

	if (enable) {
		printk("Start charging\n");
		bq24192_reg_read_modify(chip_extern->client,BQ24192_POWER_ON_CFG_REG,0x10, true);
	} else {
		printk("Stop charging\n");
		bq24192_reg_read_modify(chip_extern->client,BQ24192_POWER_ON_CFG_REG,0x10, false);
	}
}
EXPORT_SYMBOL(bq24192_charge_enable);

//Charge  jeita 2014-02-18------------------------------start
static int fg_chip_get_property(enum power_supply_property psp);
static struct power_supply *get_fg_chip_psy(void);
static int check_batt_psy(struct device *dev, void *data);
static int TempArry[5][2] = {{1000,500},{550,400},{450,100},{150,0},{50,-200}};
static void JetiaWork(struct work_struct *work) 
{
	static int temp,oldtemp=0,voltage;
	static u8 index=2;
        int capacity;
        capacity = fg_chip_get_property(POWER_SUPPLY_PROP_CAPACITY);

        if (cur_cable_status == UG31XX_NO_CABLE) {
            printk("## No charging source, no need do JEITA !!\n");
            goto End;
        }

        if (entry_mode == 4 && capacity == 100) {
            printk("## in charging mode, battery capacity is 100%% and full charger, no need do JEITA !!\n");
            schedule_delayed_work(&chip_extern->jeita_wrkr, 120*HZ);
            return;
        }
#ifdef ASUS_ENG_BUILD
        if (capacity > 59 && eng_charging_limit) {
                printk(" %s: In eng mode, Disable charger on capacity(%d%%) is more than 60 %% \n", __func__, capacity);
                bq24192_charge_enable(0);
                goto Done;
        }
#endif

	temp = fg_chip_get_property(POWER_SUPPLY_PROP_TEMP);
	if((temp-oldtemp) > 0){
		if(temp > TempArry[index][0])
			index-= 1;
	}else{
		if(temp < TempArry[index][1])
			index+= 1;
	}
	printk("## ASUS JEITA rule, temp:%d,oldtemp:%d %d\n",temp,oldtemp,index);
	oldtemp = temp;
	if(index >= 4)
		index = 4;
	if(index <= 0)
		index = 0;	

	switch (index)
	{
		case 0:
			bq24192_charge_enable(0);
			bq24192_set_cc(chip_extern,2048);
			bq24192_set_cv(chip_extern,4000);
			break;
		case 1:
			voltage = fg_chip_get_property(POWER_SUPPLY_PROP_VOLTAGE_NOW);
			voltage = voltage/1000;
			if(voltage <= 4000)
				bq24192_charge_enable(1);
			else 
				bq24192_charge_enable(0);
			bq24192_set_cc(chip_extern,2048);
			bq24192_set_cv(chip_extern,4000);
			break;
		case 2:
			bq24192_charge_enable(1);
			bq24192_set_cc(chip_extern,2048);
			bq24192_set_cv(chip_extern,4208);
			break;
		case 3:
			bq24192_charge_enable(1);
			bq24192_set_cc(chip_extern,1024);
			bq24192_set_cv(chip_extern,4208);
			break;
		case 4:
			bq24192_charge_enable(0);
			bq24192_set_cc(chip_extern,1024);
			bq24192_set_cv(chip_extern,4208);
			break;
	}
#ifdef ASUS_ENG_BUILD
Done:
        power_supply_changed(&bq24192_supply[PWR_SUPPLY_USB]);
        power_supply_changed(&bq24192_supply[PWR_SUPPLY_AC]);
#endif
End:
	schedule_delayed_work(&chip_extern->jeita_wrkr, 30*HZ); 
}
//Charge  jeita 2014-02-18------------------------------end
/*
 * Genenric register read/write interfaces to access registers in charger ic
 */

static int bq24192_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret, i;

	for (i = 0; i < NR_RETRY_CNT; i++) {
		ret = i2c_smbus_write_byte_data(client, reg, value);
		if (ret == -EAGAIN || ret == -ETIMEDOUT)
			continue;
		else
			break;
	}

	if (ret < 0)
		dev_err(&client->dev, "I2C SMbus Write error:%d\n", ret);

	return ret;
}

static int bq24192_read_reg(struct i2c_client *client, u8 reg)
{
	int ret, i;

	for (i = 0; i < NR_RETRY_CNT; i++) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret == -EAGAIN || ret == -ETIMEDOUT)
			continue;
		else
			break;
	}

	if (ret < 0)
		dev_err(&client->dev, "I2C SMbus Read error:%d\n", ret);

	return ret;
}

#ifdef DEBUG
/*
 * This function dumps the bq24192 registers
 */
static void bq24192_dump_registers(struct bq24192_chip *chip)
{
	int ret;

	dev_info(&chip->client->dev, "%s\n", __func__);

	/* Input Src Ctrl register */
	ret = bq24192_read_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Input Src Ctrl reg read fail\n");
	dev_info(&chip->client->dev, "REG00 %x\n", ret);

	/* Pwr On Cfg register */
	ret = bq24192_read_reg(chip->client, BQ24192_POWER_ON_CFG_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Pwr On Cfg reg read fail\n");
	dev_info(&chip->client->dev, "REG01 %x\n", ret);

	/* Chrg Curr Ctrl register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_CUR_CNTL_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Chrg Curr Ctrl reg read fail\n");
	dev_info(&chip->client->dev, "REG02 %x\n", ret);

	/* Pre-Chrg Term register */
	ret = bq24192_read_reg(chip->client,
					BQ24192_PRECHRG_TERM_CUR_CNTL_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Pre-Chrg Term reg read fail\n");
	dev_info(&chip->client->dev, "REG03 %x\n", ret);

	/* Chrg Volt Ctrl register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_VOLT_CNTL_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Chrg Volt Ctrl reg read fail\n");
	dev_info(&chip->client->dev, "REG04 %x\n", ret);

	/* Chrg Term and Timer Ctrl register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG);
	if (ret < 0) {
		dev_warn(&chip->client->dev,
			"Chrg Term and Timer Ctrl reg read fail\n");
	}
	dev_info(&chip->client->dev, "REG05 %x\n", ret);

	/* Thermal Regulation register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_THRM_REGL_REG);
	if (ret < 0) {
		dev_warn(&chip->client->dev,
				"Thermal Regulation reg read fail\n");
	}
	dev_info(&chip->client->dev, "REG06 %x\n", ret);

	/* Misc Operations Ctrl register */
	ret = bq24192_read_reg(chip->client, BQ24192_MISC_OP_CNTL_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Misc Op Ctrl reg read fail\n");
	dev_info(&chip->client->dev, "REG07 %x\n", ret);

	/* System Status register */
	ret = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "System Status reg read fail\n");
	dev_info(&chip->client->dev, "REG08 %x\n", ret);

	/* Fault Status register */
	ret = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Fault Status reg read fail\n");
	dev_info(&chip->client->dev, "REG09 %x\n", ret);

	/* Vendor Revision register */
	ret = bq24192_read_reg(chip->client, BQ24192_VENDER_REV_REG);
	if (ret < 0)
		dev_warn(&chip->client->dev, "Vendor Rev reg read fail\n");
	dev_info(&chip->client->dev, "REG0A %x\n", ret);
}
#endif

/*
 * If the bit_set is TRUE then val 1s will be SET in the reg else val 1s will
 * be CLEARED
 */
static int bq24192_reg_read_modify(struct i2c_client *client, u8 reg,
							u8 val, bool bit_set)
{
	int ret;

	ret = bq24192_read_reg(client, reg);

	if (bit_set)
		ret |= val;
	else
		ret &= (~val);

	ret = bq24192_write_reg(client, reg, ret);

	return ret;
}

static int bq24192_reg_multi_bitset(struct i2c_client *client, u8 reg,
						u8 val, u8 pos, u8 len)
{
	int ret;
	u8 data;

	ret = bq24192_read_reg(client, reg);
	if (ret < 0) {
		dev_warn(&client->dev, "I2C SMbus Read error:%d\n", ret);
		return ret;
	}

	data = (1 << len) - 1;
	ret = (ret & ~(data << pos)) | val;
	ret = bq24192_write_reg(client, reg, ret);

	return ret;
}

/*
 * This function verifies if the bq24192i charger chip is in Hi-Z
 * If yes, then clear the Hi-Z to resume the charger operations
 */
static int bq24192_clear_hiz(struct bq24192_chip *chip)
{
	int ret, count;

	dev_info(&chip->client->dev, "%s\n", __func__);

	for (count = 0; count < MAX_TRY; count++) {
		/*
		 * Read the bq24192i REG00 register for charger Hi-Z mode.
		 * If it is in Hi-Z, then clear the Hi-Z to resume the charging
		 * operations.
		 */
		ret = bq24192_read_reg(chip->client,
				BQ24192_INPUT_SRC_CNTL_REG);
		if (ret < 0) {
			dev_warn(&chip->client->dev,
					"Input src cntl read failed\n");
			goto i2c_error;
		}

		if (ret & INPUT_SRC_CNTL_EN_HIZ) {
			dev_warn(&chip->client->dev,
						"Charger IC in Hi-Z mode\n");
#ifdef DEBUG
			bq24192_dump_registers(chip);
#endif
			/* Clear the Charger from Hi-Z mode */
			ret = (chip->inlmt & ~INPUT_SRC_CNTL_EN_HIZ);

			/* Write the values back */
			ret = bq24192_write_reg(chip->client,
					BQ24192_INPUT_SRC_CNTL_REG, ret);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"Input src cntl write failed\n");
				goto i2c_error;
			}
			msleep(150);
		} else {
			dev_info(&chip->client->dev,
						"Charger is not in Hi-Z\n");
			break;
		}
	}
	return ret;
i2c_error:
	dev_err(&chip->client->dev, "%s\n", __func__);
	return ret;
}

/* check_batt_psy -check for whether power supply type is battery
 * @dev : Power Supply dev structure
 * @data : Power Supply Driver Data
 * Context: can sleep
 *
 * Return true if power supply type is battery
 *
 */
static int check_batt_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);

	/* check for whether power supply type is battery */
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY) {
		fg_psy = psy;
		return 1;
	}
	return 0;
}

/**
 * get_fg_chip_psy - identify the Fuel Gauge Power Supply device
 * Context: can sleep
 *
 * Return Fuel Gauge power supply structure
 */
static struct power_supply *get_fg_chip_psy(void)
{
	if (fg_psy)
		return fg_psy;

	/* loop through power supply class */
	class_for_each_device(power_supply_class, NULL, NULL,
			check_batt_psy);
	return fg_psy;
}

/**
 * fg_chip_get_property - read a power supply property from Fuel Gauge driver
 * @psp : Power Supply property
 *
 * Return power supply property value
 *
 */
static int fg_chip_get_property(enum power_supply_property psp)
{
	union power_supply_propval val;
	int ret = -ENODEV;

	if (!fg_psy)
		fg_psy = get_fg_chip_psy();
	if (fg_psy) {
		ret = fg_psy->get_property(fg_psy, psp, &val);
		if (!ret)
			return val.intval;
	}
	return ret;
}

/**
 * bq24192_get_charger_health - to get the charger health status
 *
 * Returns charger health status
 */
int bq24192_get_charger_health(void)
{
	int ret_status, ret_fault;
	struct bq24192_chip *chip =
		i2c_get_clientdata(bq24192_client);

	dev_dbg(&chip->client->dev, "%s\n", __func__);

	/* If we do not have any cable connected, return health as UNKNOWN */
	if (chip->cable_type == POWER_SUPPLY_CHARGER_TYPE_NONE)
		return POWER_SUPPLY_HEALTH_UNKNOWN;

	ret_fault = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (ret_fault < 0) {
		dev_warn(&chip->client->dev,
			"read reg failed %s\n", __func__);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	/* Check if the error VIN condition occured */
	ret_status = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
	if (ret_status < 0) {
		dev_warn(&chip->client->dev,
			"read reg failed %s\n", __func__);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (!(ret_status & SYSTEM_STAT_PWR_GOOD) &&
	((ret_fault & FAULT_STAT_CHRG_BITS) == FAULT_STAT_CHRG_IN_FLT))
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;

	if (!(ret_status & SYSTEM_STAT_PWR_GOOD) &&
	((ret_status & SYSTEM_STAT_VBUS_BITS) == SYSTEM_STAT_VBUS_UNKNOWN))
		return POWER_SUPPLY_HEALTH_DEAD;

	return POWER_SUPPLY_HEALTH_GOOD;
}

/**
 * bq24192_get_battery_health_tf103c - to get the battery health status
 *
 * Returns battery health status
 */
int bq24192_get_battery_health_tf103c(void)
{
	int  temp,vnow;
	struct bq24192_chip *chip;
	if (!bq24192_client)
		return POWER_SUPPLY_HEALTH_UNKNOWN;

	chip = i2c_get_clientdata(bq24192_client);

	dev_info(&chip->client->dev, "+%s\n", __func__);

	/* If power supply is emulating as battery, return health as good */
	if (!chip->pdata->sfi_tabl_present)
		return POWER_SUPPLY_HEALTH_GOOD;

	/* Report the battery health w.r.t battery temperature from FG */
	temp = fg_chip_get_property(POWER_SUPPLY_PROP_TEMP);
	if (temp == -ENODEV || temp == -EINVAL) {
		dev_err(&chip->client->dev,
				"Failed to read batt profile\n");
		return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	}

	temp /= 10;

	if ((temp <= chip->min_temp) ||
		(temp > chip->max_temp))
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	/* read the battery voltage */
	vnow = fg_chip_get_property(POWER_SUPPLY_PROP_VOLTAGE_NOW);
	if (vnow == -ENODEV || vnow == -EINVAL) {
		dev_err(&chip->client->dev, "Can't read voltage from FG\n");
		return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	}

	/* convert voltage into millivolts */
	vnow /= 1000;
	dev_warn(&chip->client->dev, "vnow = %d\n", vnow);

	if (vnow > chip->max_cv)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;

	dev_dbg(&chip->client->dev, "-%s\n", __func__);
	return POWER_SUPPLY_HEALTH_GOOD;
}
EXPORT_SYMBOL(bq24192_get_battery_health_tf103c);

/***********************************************************************/

/* convert the input current limit value
 * into equivalent register setting.
 * Note: ilim must be in mA.
 */
static u8 chrg_ilim_to_reg(int ilim)
{
	u8 reg;

	/* Set the input source current limit
	 * between 100 to 1500mA */
	if (ilim <= 100)
		reg |= INPUT_SRC_CUR_LMT0;
	else if (ilim <= 150)
		reg |= INPUT_SRC_CUR_LMT1;
	else if (ilim <= 500)
		reg |= INPUT_SRC_CUR_LMT2;
	else if (ilim <= 900)
		reg |= INPUT_SRC_CUR_LMT3;
	else if (ilim <= 1000)
		reg |= INPUT_SRC_CUR_LMT4;
	else if (ilim <= 1200)
		reg |= INPUT_SRC_CUR_LMT5;
	else if (ilim <= 2000)
		reg |= INPUT_SRC_CUR_LMT6;
	else
		reg |= INPUT_SRC_CUR_LMT7;

	return reg;
}

static u8 chrg_iterm_to_reg(int iterm)
{
	u8 reg;

	if (iterm <= BQ24192_CHRG_ITERM_OFFSET)
		reg = 0;
	else
		reg = ((iterm - BQ24192_CHRG_ITERM_OFFSET) /
			BQ24192_CHRG_CUR_LSB_TO_ITERM);
	return reg;
}

/* convert the charge current value
 * into equivalent register setting
 */
static u8 chrg_cur_to_reg(int cur)
{
	u8 reg;

	if (cur <= BQ24192_CHRG_CUR_OFFSET)
		reg = 0x0;
	else
		reg = ((cur - BQ24192_CHRG_CUR_OFFSET) /
				BQ24192_CHRG_CUR_LSB_TO_CUR);

	if(((cur - BQ24192_CHRG_CUR_OFFSET) % BQ24192_CHRG_CUR_LSB_TO_CUR) > 0)
		reg += 1;
	
	/* D0, D1 bits of Charge Current
	 * register are not used */
	reg = reg << 2;
	return reg;
}

/* convert the charge voltage value
 * into equivalent register setting
 */
static u8 chrg_volt_to_reg(int volt)
{
	u8 reg,reg1;

	if (volt <= BQ24192_CHRG_VOLT_OFFSET)
		reg = 0x0;
	else
		reg = (volt - BQ24192_CHRG_VOLT_OFFSET) /
				BQ24192_CHRG_VOLT_LSB_TO_VOLT;

	reg1 = (volt - BQ24192_CHRG_VOLT_OFFSET) %
				BQ24192_CHRG_VOLT_LSB_TO_VOLT; 
	if((reg1 >0)&&(reg1 < 16))
		reg += 1;
	
	reg = (reg << 2) | CHRG_VOLT_CNTL_BATTLOWV;
	return reg;
}

static int bq24192_enable_hw_term(struct bq24192_chip *chip, bool hw_term_en)
{
	int ret = 0;

	dev_info(&chip->client->dev, "%s\n", __func__);

	/* Disable and enable charging to restart the charging */
	ret = bq24192_reg_multi_bitset(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_CHRG_CFG_DIS,
					CHR_CFG_BIT_POS,
					CHR_CFG_BIT_LEN);
	if (ret < 0) {
		dev_warn(&chip->client->dev,
			"i2c reg write failed: reg: %d, ret: %d\n",
			BQ24192_POWER_ON_CFG_REG, ret);
		return ret;
	}

	/* Read the timer control register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "TIMER CTRL reg read failed\n");
		return ret;
	}

	/*
	 * Enable the HW termination. When disabled the HW termination, battery
	 * was taking too long to go from charging to full state. HW based
	 * termination could cause the battery capacity to drop but it would
	 * result in good battery life.
	 */
	if (hw_term_en)
		ret |= CHRG_TIMER_EXP_CNTL_EN_TERM;
	else
		ret &= ~CHRG_TIMER_EXP_CNTL_EN_TERM;

	/* Program the TIMER CTRL register */
	ret = bq24192_write_reg(chip->client,
				BQ24192_CHRG_TIMER_EXP_CNTL_REG,
				ret);
	if (ret < 0)
		dev_warn(&chip->client->dev, "TIMER CTRL I2C write failed\n");

	return ret;
}

/*
 * chip->event_lock need to be acquired before calling this function
 * to avoid the race condition
 */
static int program_timers(struct bq24192_chip *chip, int wdt_duration,
				bool sfttmr_enable)
{
	int ret;

	/* Read the timer control register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "TIMER CTRL reg read failed\n");
		return ret;
	}

	/* First disable watchdog */
	ret &= 0xCF;
	bq24192_write_reg(chip->client,
				BQ24192_CHRG_TIMER_EXP_CNTL_REG,
				ret);

	/* Read the timer control register */
	ret = bq24192_read_reg(chip->client, BQ24192_CHRG_TIMER_EXP_CNTL_REG);
	if (ret < 0) {
		dev_warn(&chip->client->dev, "TIMER CTRL reg read failed\n");
		return ret;
	}

	/* Program the time with duration passed */
	ret &= 0xCF;
	ret |=  wdt_duration;

	/* Enable/Disable the safety timer */
	if (sfttmr_enable)
		ret |= CHRG_TIMER_EXP_CNTL_EN_TIMER;
	else
		ret &= ~CHRG_TIMER_EXP_CNTL_EN_TIMER;

	/* Program the TIMER CTRL register */
	ret = bq24192_write_reg(chip->client,
				BQ24192_CHRG_TIMER_EXP_CNTL_REG,
				ret);
	if (ret < 0)
		dev_warn(&chip->client->dev, "TIMER CTRL I2C write failed\n");

	return ret;
}

/* This function should be called with the mutex held */
static int reset_wdt_timer(struct bq24192_chip *chip)
{
	int ret = 0, i;

	/* reset WDT timer */
	for (i = 0; i < MAX_RESET_WDT_RETRY; i++) {
		ret = bq24192_reg_read_modify(chip->client,
						BQ24192_POWER_ON_CFG_REG,
						WDTIMER_RESET_MASK, true);
		if (ret < 0)
			dev_warn(&chip->client->dev, "I2C write failed:%s\n",
							__func__);
	}
	return ret;
}

/*
 *This function will modify the VINDPM as per the battery voltage
 */
static int bq24192_modify_vindpm(u8 vindpm)
{
	int ret;
	u8 vindpm_prev;
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);

	dev_info(&chip->client->dev, "%s\n", __func__);

	/* Get the input src ctrl values programmed */
	ret = bq24192_read_reg(chip->client,
				BQ24192_INPUT_SRC_CNTL_REG);

	if (ret < 0) {
		dev_warn(&chip->client->dev, "INPUT CTRL reg read failed\n");
		return ret;
	}

	/* Assign the return value of REG00 to vindpm_prev */
	vindpm_prev = ret & INPUT_SRC_VINDPM_MASK;
	ret &= ~INPUT_SRC_VINDPM_MASK;

	/*
	 * If both the previous and current values are same do not program
	 * the register.
	*/
	if (vindpm_prev != vindpm) {
		vindpm |= ret;
		ret = bq24192_write_reg(chip->client,
					BQ24192_INPUT_SRC_CNTL_REG, vindpm);
		if (ret < 0) {
			dev_info(&chip->client->dev, "VINDPM failed\n");
			return ret;
		}
	}
	return ret;
}

/* This function should be called with the mutex held */
static int bq24192_turn_otg_vbus(struct bq24192_chip *chip, bool votg_on)
{
	int ret = 0;

	dev_info(&chip->client->dev, "%s %d\n", __func__, votg_on);

	if (votg_on) {
			/* Program the timers */
			ret = program_timers(chip,
						CHRG_TIMER_EXP_CNTL_WDT80SEC,
						false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
					"TIMER enable failed %s\n", __func__);
				goto i2c_write_fail;
			}
			/* Configure the charger in OTG mode */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_CHRG_CFG_OTG, true);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}

			/* Put the charger IC in reverse boost mode. Since
			 * SDP charger can supply max 500mA charging current
			 * Setting the boost current to 500mA
			 */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_BOOST_LIM, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			chip->boost_mode = true;
			/* Schedule the charger task worker now */
			schedule_delayed_work(&chip->chrg_task_wrkr,
						0);
	} else {
			/* Clear the charger from the OTG mode */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_CHRG_CFG_OTG, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}

			/* Put the charger IC out of reverse boost mode 500mA */
			ret = bq24192_reg_read_modify(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					POWER_ON_CFG_BOOST_LIM, false);
			if (ret < 0) {
				dev_warn(&chip->client->dev,
						"read reg modify failed\n");
				goto i2c_write_fail;
			}
			chip->boost_mode = false;
			/* Cancel the charger task worker now */
			cancel_delayed_work_sync(&chip->chrg_task_wrkr);
	}

	/*
	 *  Drive the gpio to turn ON/OFF the VBUS
	 */
	if (chip->pdata->drive_vbus)
		chip->pdata->drive_vbus(votg_on);

	return ret;
i2c_write_fail:
	dev_err(&chip->client->dev, "%s: Failed\n", __func__);
	return ret;
}

#ifdef CONFIG_DEBUG_FS
#define DBGFS_REG_BUF_LEN	3

static int bq24192_show(struct seq_file *seq, void *unused)
{
	u16 val;
	long addr;

	if (kstrtol((char *)seq->private, 16, &addr))
		return -EINVAL;

	val = bq24192_read_reg(bq24192_client, addr);
	seq_printf(seq, "%x\n", val);

	return 0;
}

static int bq24192_dbgfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, bq24192_show, inode->i_private);
}

static ssize_t bq24192_dbgfs_reg_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[DBGFS_REG_BUF_LEN];
	long addr;
	unsigned long value;
	int ret;
	struct seq_file *seq = file->private_data;

	if (!seq || kstrtol((char *)seq->private, 16, &addr))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, DBGFS_REG_BUF_LEN-1))
		return -EFAULT;

	buf[DBGFS_REG_BUF_LEN-1] = '\0';
	if (kstrtoul(buf, 16, &value))
		return -EINVAL;

	dev_info(&bq24192_client->dev,
			"[dbgfs write] Addr:0x%x Val:0x%x\n",
			(u32)addr, (u32)value);


	ret = bq24192_write_reg(bq24192_client, addr, value);
	if (ret < 0)
		dev_warn(&bq24192_client->dev, "I2C write failed\n");

	return count;
}

static const struct file_operations bq24192_dbgfs_fops = {
	.owner		= THIS_MODULE,
	.open		= bq24192_dbgfs_open,
	.read		= seq_read,
	.write		= bq24192_dbgfs_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bq24192_create_debugfs(struct bq24192_chip *chip)
{
	int i;
	struct dentry *entry;

	bq24192_dbgfs_root = debugfs_create_dir(DEV_NAME, NULL);
	if (IS_ERR(bq24192_dbgfs_root)) {
		dev_warn(&chip->client->dev, "DEBUGFS DIR create failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < BQ24192_MAX_MEM; i++) {
		sprintf((char *)&bq24192_dbg_regs[i], "%x", i);
		entry = debugfs_create_file(
					(const char *)&bq24192_dbg_regs[i],
					S_IRUGO,
					bq24192_dbgfs_root,
					&bq24192_dbg_regs[i],
					&bq24192_dbgfs_fops);
		if (IS_ERR(entry)) {
			debugfs_remove_recursive(bq24192_dbgfs_root);
			bq24192_dbgfs_root = NULL;
			dev_warn(&chip->client->dev,
					"DEBUGFS entry Create failed\n");
			return -ENOMEM;
		}
	}

	return 0;
}
static inline void bq24192_remove_debugfs(struct bq24192_chip *chip)
{
	if (bq24192_dbgfs_root)
		debugfs_remove_recursive(bq24192_dbgfs_root);
}
#else
static inline int bq24192_create_debugfs(struct bq24192_chip *chip)
{
	return 0;
}
static inline void bq24192_remove_debugfs(struct bq24192_chip *chip)
{
}
#endif

static inline int bq24192_enable_charging(
			struct bq24192_chip *chip, bool val)
{
	int ret;
	u8 regval;

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, val);

	ret = program_timers(chip, CHRG_TIMER_EXP_CNTL_WDT160SEC, true);
	if (ret < 0) {
		dev_err(&chip->client->dev,
				"program_timers failed: %d\n", ret);
		return ret;
	}

	/*
	 * Program the inlmt here in case we are asked to resume the charging
	 * framework would send only set CC/CV commands and not the inlmt. This
	 * would make sure that we program the last set inlmt into the register
	 * in case for some reasons WDT expires
	 */
	regval = chrg_ilim_to_reg(chip->inlmt);

	ret = bq24192_reg_read_modify(chip->client, BQ24192_INPUT_SRC_CNTL_REG,
				regval, true);
	if (ret < 0) {
		dev_err(&chip->client->dev,
				"inlmt programming failed: %d\n", ret);
		return ret;
	}

	/*
	 * check if we have the battery emulator connected. We do not start
	 * charging if the emulator is connected. Disable the charging
	 * explicitly.
	 */
	if (!chip->pdata->sfi_tabl_present) {
		ret = bq24192_reg_multi_bitset(chip->client,
						BQ24192_POWER_ON_CFG_REG,
						POWER_ON_CFG_CHRG_CFG_DIS,
						CHR_CFG_BIT_POS,
						CHR_CFG_BIT_LEN);
		/* Schedule the charger task worker now */
		schedule_delayed_work(&chip->chrg_task_wrkr,
						0);
		return ret;
	}

	if (val) {
		/* Schedule the charger task worker now */
		schedule_delayed_work(&chip->chrg_task_wrkr, 0);

		/*
		 * Prevent system from entering s3 while charger is connected
		 */
		if (!wake_lock_active(&chip->wakelock))
			wake_lock(&chip->wakelock);
	} else {
		/* Release the wake lock */
		if (wake_lock_active(&chip->wakelock))
			wake_unlock(&chip->wakelock);

		/*
		 * Cancel the worker since it need not run when charging is not
		 * happening
		 */
		cancel_delayed_work_sync(&chip->chrg_full_wrkr);

		/* Read the status to know about input supply */
		ret = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
		if (ret < 0) {
			dev_warn(&chip->client->dev,
				"read reg failed %s\n", __func__);
		}

		/* If no charger connected, cancel the workers */
		if (!(ret & SYSTEM_STAT_VBUS_OTG)) {
			dev_info(&chip->client->dev, "NO charger connected\n");
			chip->sfttmr_expired = false;
			cancel_delayed_work_sync(&chip->chrg_task_wrkr);
		}
	}

	ret = val ? POWER_ON_CFG_CHRG_CFG_EN : POWER_ON_CFG_CHRG_CFG_DIS;

	if (!val && chip->sfttmr_expired)
		return ret;

	ret = bq24192_reg_multi_bitset(chip->client,
					BQ24192_POWER_ON_CFG_REG,
					ret, CHR_CFG_BIT_POS,
					CHR_CFG_BIT_LEN);

	return ret;
}

static inline int bq24192_enable_charger(
			struct bq24192_chip *chip, int val)
{
	int ret;

	/*stop charger for throttle state 3, by putting it in HiZ mode*/
	if (chip->cntl_state == 0x3) {
		ret = bq24192_reg_read_modify(chip->client,
			BQ24192_INPUT_SRC_CNTL_REG,
				INPUT_SRC_CNTL_EN_HIZ, true);

		if (ret < 0)
			dev_warn(&chip->client->dev,
				"Input src cntl write failed\n");
	}

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, val);

	return bq24192_enable_charging(chip, val);
}

static inline int bq24192_set_cc(struct bq24192_chip *chip, int cc)
{
	u8 regval,value;

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, cc);
	regval = chrg_cur_to_reg(cc);
	value = bq24192_read_reg(chip->client, BQ24192_CHRG_CUR_CNTL_REG);
	value = ((value&0x03)|regval);
	
	return bq24192_write_reg(chip->client, BQ24192_CHRG_CUR_CNTL_REG,
				value);
}

static inline int bq24192_set_cv(struct bq24192_chip *chip, int cv)
{
	u8 regval;

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, cv);
	regval = chrg_volt_to_reg(cv);
	
	return bq24192_write_reg(chip->client, BQ24192_CHRG_VOLT_CNTL_REG,
					(regval | CHRG_VOLT_CNTL_VRECHRG)|CHRG_VOLT_CNTL_BATTLOWV);
}

static inline int bq24192_set_inlmt(struct bq24192_chip *chip, int inlmt)
{
	u8 regval,value;

	dev_warn(&chip->client->dev, "%s:%d %d\n", __func__, __LINE__, inlmt);
	chip->inlmt = inlmt;
	regval = chrg_ilim_to_reg(inlmt);
	value = bq24192_read_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG);
	regval = ((value&0xF8)|regval);
	
	return bq24192_write_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG,
				regval);
}

static inline int bq24192_set_iterm(struct bq24192_chip *chip, int iterm)
{
	u8 reg_val;

	if (iterm > BQ24192_CHRG_ITERM_OFFSET)
		dev_warn(&chip->client->dev,
			"%s ITERM set for >128mA", __func__);

	reg_val = chrg_iterm_to_reg(iterm);
	//msleep(500);

	return bq24192_write_reg(chip->client,
			BQ24192_PRECHRG_TERM_CUR_CNTL_REG,
				(BQ24192_PRE_CHRG_CURR_256 | reg_val));
}

static inline int bq24192_get_inlmt(struct bq24192_chip *chip) {
	u8 regval;
        int value = 100;

        regval = bq24192_read_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG);
	regval &= 0x07;
        switch (regval) {
            case 0:
               value = 100;
               break;
            case 1:
               value = 150;
               break;
            case 2:
               value = 500;
               break;
            case 3:
               value = 900;
               break;
            case 4:
               value = 1000;
               break;
            case 5:
               value = 1200;
               break;
            case 6:
               value = 2000;
               break;
            case 7:
               value = 3000;
               break;
        }
        dev_warn(&chip->client->dev, "%s:%d ,input current = %d mA\n", __func__, __LINE__, value);

	return value;
}

int bq24192_chargeric_status (void) {
	int ret;
	struct bq24192_chip *chip=NULL;

	chip = chip_extern;
	if((chip->client == NULL)||(chip == NULL)) {
		printk("chip->client == null\n");
		return -1;
	}

        mutex_lock(&chip_extern->event_lock);
	ret = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
        mutex_unlock(&chip_extern->event_lock);

	if (ret < 0)
		dev_err(&chip->client->dev, "STATUS register read failed\n");

	return ret;
}

int bq24192_is_charging(void)
{
	int ret;
	struct bq24192_chip *chip=NULL;
	
        if(chip_extern == NULL)
	{
		printk("chip_extern == null\n");
		return BQ24192_CHRGR_STAT_FAULT;
	}

	chip = chip_extern;
	ret = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
	if (ret < 0)
		dev_err(&chip->client->dev, "STATUS register read failed\n");

	ret &= SYSTEM_STAT_CHRG_MASK;

	switch (ret) {
	case SYSTEM_STAT_NOT_CHRG:
		chip->chgr_stat = BQ24192_CHRGR_STAT_FAULT;
		break;
	case SYSTEM_STAT_CHRG_DONE:
		chip->chgr_stat = BQ24192_CHRGR_STAT_BAT_FULL;
		break;
	case SYSTEM_STAT_PRE_CHRG:
	case SYSTEM_STAT_FAST_CHRG:
		chip->chgr_stat = BQ24192_CHRGR_STAT_CHARGING;
		break;
	default:
		break;
	}

	return chip->chgr_stat;
}
EXPORT_SYMBOL(bq24192_is_charging);

static int bq24192_usb_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct bq24192_chip *chip = container_of(psy,
						struct bq24192_chip,
						usb);
	int ret = 0;

	dev_dbg(&chip->client->dev, "%s %d\n", __func__, psp);
	if (mutex_is_locked(&chip->event_lock)) {
		dev_dbg(&chip->client->dev,
			"%s: mutex is already acquired",
				__func__);
	}
	mutex_lock(&chip->event_lock);

	switch (psp) {

	case POWER_SUPPLY_PROP_PRESENT:
		chip->present = val->intval;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		chip->online = val->intval;
		break;
	case POWER_SUPPLY_PROP_ENABLE_CHARGING:
		bq24192_enable_hw_term(chip, val->intval);
		ret = bq24192_enable_charging(chip, val->intval);

		if (ret < 0)
			dev_err(&chip->client->dev,
				"Error(%d) in %s charging", ret,
				(val->intval ? "enable" : "disable"));
		else
			chip->is_charging_enabled = val->intval;

		if (!val->intval)
			cancel_delayed_work_sync(&chip->chrg_full_wrkr);
		break;
	case POWER_SUPPLY_PROP_ENABLE_CHARGER:
		ret = bq24192_enable_charger(chip, val->intval);

		if (ret < 0) {
			dev_err(&chip->client->dev,
			"Error(%d) in %s charger", ret,
			(val->intval ? "enable" : "disable"));
		} else
			chip->is_charger_enabled = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CURRENT:
		ret = bq24192_set_cc(chip, val->intval);
		if (!ret)
			chip->cc = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_VOLTAGE:
		ret = bq24192_set_cv(chip, val->intval);
		if (!ret)
			chip->cv = val->intval;
		break;
	case POWER_SUPPLY_PROP_MAX_CHARGE_CURRENT:
		chip->max_cc = val->intval;
		break;
	case POWER_SUPPLY_PROP_MAX_CHARGE_VOLTAGE:
		chip->max_cv = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CUR:
		ret = bq24192_set_iterm(chip, val->intval);
		if (!ret)
			chip->iterm = val->intval;
		break;
	case POWER_SUPPLY_PROP_CABLE_TYPE:
		chip->cable_type = val->intval;
		chip->usb.type = get_power_supply_type(chip->cable_type);
		break;
	case POWER_SUPPLY_PROP_INLMT:
		ret = bq24192_set_inlmt(chip, val->intval);
		if (!ret)
			chip->inlmt = val->intval;
		break;
	case POWER_SUPPLY_PROP_MAX_TEMP:
		chip->max_temp = val->intval;
		break;
	case POWER_SUPPLY_PROP_MIN_TEMP:
		chip->min_temp = val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		chip->cntl_state = val->intval;
		break;
	default:
		ret = -ENODATA;
	}

	mutex_unlock(&chip->event_lock);
	return ret;
}

static int bq24192_usb_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct bq24192_chip *chip = container_of(psy,
					struct bq24192_chip,
					usb);
	enum bq24192_chrgr_stat charging;

	dev_dbg(&chip->client->dev, "%s %d\n", __func__, psp);
	if (system_state != SYSTEM_RUNNING) {
		if (!mutex_trylock(&chip->event_lock))	
			return -EBUSY;
		} else
			mutex_lock(&chip->event_lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = chip->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bq24192_get_charger_health();
		break;
	case POWER_SUPPLY_PROP_MAX_CHARGE_CURRENT:
		val->intval = chip->max_cc;
		break;
	case POWER_SUPPLY_PROP_MAX_CHARGE_VOLTAGE:
		val->intval = chip->max_cv;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CURRENT:
		val->intval = chip->cc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_VOLTAGE:
		val->intval = chip->cv;
		break;
	case POWER_SUPPLY_PROP_INLMT:
		val->intval = chip->inlmt;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CUR:
		val->intval = chip->iterm;
		break;
	case POWER_SUPPLY_PROP_CABLE_TYPE:
		val->intval = chip->cable_type;
		break;
	case POWER_SUPPLY_PROP_ENABLE_CHARGING:
		if (chip->boost_mode)
			val->intval = false;
		else {
			charging = bq24192_is_charging();
			val->intval = (chip->is_charging_enabled && charging);
		}
		break;
	case POWER_SUPPLY_PROP_ENABLE_CHARGER:
		val->intval = chip->is_charger_enabled;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = chip->cntl_state;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = chip->pdata->num_throttle_states;
		break;
	case POWER_SUPPLY_PROP_MAX_TEMP:
		val->intval = chip->max_temp;
		break;
	case POWER_SUPPLY_PROP_MIN_TEMP:
		val->intval = chip->min_temp;
		break;
	default:
		mutex_unlock(&chip->event_lock);
		return -EINVAL;
	}

	mutex_unlock(&chip->event_lock);
	return 0;
}

static void bq24192_resume_charging(struct bq24192_chip *chip)
{
	if (chip->inlmt)
		bq24192_set_inlmt(chip, chip->inlmt);
	if (chip->cc)
		bq24192_set_cc(chip, chip->cc);
	if (chip->cv)
		bq24192_set_cv(chip, chip->cv);
	if (chip->is_charging_enabled)
		bq24192_enable_charging(chip, true);
	if (chip->is_charger_enabled)
		bq24192_enable_charger(chip, true);
}

static void bq24192_full_worker(struct work_struct *work)
{
	struct bq24192_chip *chip = container_of(work,
						    struct bq24192_chip,
						    chrg_full_wrkr.work);
	static int ret,count;

	count += 1;
	if(count == 4){
		count = 0;
		ret = bq24192_read_reg(chip_extern->client, BQ24192_INPUT_SRC_CNTL_REG);
		printk("Input limit:%x %d\n",ret,cable_status);
	}
	//power_supply_changed(NULL);
	reset_wdt_timer(chip_extern);
	/* schedule the thread to let the framework know about FULL */
	schedule_delayed_work(&chip->chrg_full_wrkr, 30*HZ);
}

static void otg_disable_worker(struct work_struct *work)
{
	printk("otg_resume_enable\n");
	int ret=0;
	ret=bq24192_reg_read_modify(chip_extern->client,
		                 BQ24192_POWER_ON_CFG_REG,0x20, true);        
    if (ret < 0) {
		 printk("Error in writing the control register BQ24192_POWER_ON_CFG_REG\n");
	}
}

/* IRQ handler for charger Interrupts configured to GPIO pin */
static irqreturn_t bq24192_irq_isr(int irq, void *devid)
{
	struct bq24192_chip *chip = (struct bq24192_chip *)devid;

	/**TODO: This hanlder will be used for charger Interrupts */
	dev_dbg(&chip->client->dev,
		"IRQ Handled for charger interrupt: %d\n", irq);

	return IRQ_WAKE_THREAD;
}

/* IRQ handler for charger Interrupts configured to GPIO pin */
static irqreturn_t bq24192_irq_thread(int irq, void *devid)
{
	struct bq24192_chip *chip = (struct bq24192_chip *)devid;
	int reg_status, reg_fault,pending;
    int ret=0;
	
	pending = intel_mid_pmic_readb(R_PMIC_CHGRIRQ);
	if(pending&0x01){
	  intel_mid_pmic_writeb(R_PMIC_CHGRIRQ,pending);
	}
	/* Check if battery fault condition occured. Reading the register
	   value two times to get reliable reg value, recommended by vendor*/
	reg_fault = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (reg_fault < 0)
		dev_err(&chip->client->dev, "FAULT register read failed:\n");

	
	if(reg_fault & FAULT_STAT_OTG_FLT){
        
        ret=bq24192_reg_read_modify(chip_extern->client,
		                 BQ24192_POWER_ON_CFG_REG,0x20, false);        
        if (ret < 0) {
		 printk("Error in writing the control register\n");
		}
	   //schedule_delayed_work(&chip->otg_disable_wrkr, 5);
       printk("bq24192 otg fault\n");
	}
	dev_info(&chip->client->dev, "FAULT reg %x\n", reg_fault);
	reg_fault = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (reg_fault < 0)
		dev_err(&chip->client->dev, "FAULT register read failed:\n");
	dev_info(&chip->client->dev, "FAULT reg %x\n", reg_fault);	
	/*
	 * check the bq24192 status/fault registers to see what is the
	 * source of the interrupt
	 */
	reg_status = bq24192_read_reg(chip->client, BQ24192_SYSTEM_STAT_REG);
	if (reg_status < 0)
		dev_err(&chip->client->dev, "STATUS register read failed:\n");
	dev_info(&chip->client->dev, "STATUS reg %x\n", reg_status);

	if( (reg_status&0x30) == 0x20){
		if(cable_status == POWER_SUPPLY_CHARGER_TYPE_USB_DCP){
			ret = bq24192_read_reg(chip->client, BQ24192_INPUT_SRC_CNTL_REG);
			printk("Input source: %x\n",ret);
			bq24192_set_inlmt(chip_extern,1200);			
		}
	}
	return IRQ_HANDLED;
}

static void bq24192_PowerState_worker(struct work_struct *work)
{
	int type;
	int ret=0;

#ifndef USB_NOTIFY_CALLBACK
	if(PowerStateOk){
		   mutex_lock(&chip_extern->event_lock);
		    ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_INPUT_SRC_CNTL_REG,0x80,false);
		    if (ret < 0) {
			     mutex_unlock(&chip_extern->event_lock);
			     printk("Error in writing the control register\n");
			   return;
		 
		     }
		   mutex_unlock(&chip_extern->event_lock);
			
	   	    mutex_lock(&chip_extern->event_lock);
		    ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_POWER_ON_CFG_REG,0x10, true);
		    if (ret < 0) {
			     mutex_unlock(&chip_extern->event_lock);
			     printk("Error in writing the control register\n");
			   return;
		 
		     }
		    chip_extern->is_charging_enabled=true;
		    mutex_unlock(&chip_extern->event_lock);
		 PowerStateOk = 0;
		 type = GetPowerState();

		 if(OtgOk)
			type = POWER_SUPPLY_CHARGER_TYPE_NONE;
		 bq24192_cable_callback(type);
	   }
	else{
		schedule_delayed_work(&chip_extern->power_state_task_wrkr, 1*HZ);
	}
#endif
}

static void bq24192_task_worker(struct work_struct *work)
{
	struct bq24192_chip *chip =
	    container_of(work, struct bq24192_chip, chrg_task_wrkr.work);
	int ret, jiffy = CHARGER_TASK_JIFFIES, vbatt;
	static int prev_health = POWER_SUPPLY_HEALTH_GOOD;
	int curr_health;
	u8 vindpm = INPUT_SRC_VOLT_LMT_DEF;

	dev_info(&chip->client->dev, "%s\n", __func__);

	/* Reset the WDT */
	mutex_lock(&chip->event_lock);
	ret = reset_wdt_timer(chip);
	mutex_unlock(&chip->event_lock);
	if (ret < 0)
		dev_warn(&chip->client->dev, "WDT reset failed:\n");

	/*
	 * If we have an OTG device connected, no need to modify the VINDPM
	 * check for Hi-Z
	 */
	if (chip->boost_mode) {
		jiffy = CHARGER_HOST_JIFFIES;
		goto sched_task_work;
	}

	if (!(chip->cntl_state == 0x3)) {
		/* Clear the charger from Hi-Z */
		ret = bq24192_clear_hiz(chip);
		if (ret < 0)
			dev_warn(&chip->client->dev, "HiZ clear failed:\n");
	}

	/* Modify the VINDPM */

	/* read the battery voltage */
	vbatt = fg_chip_get_property(POWER_SUPPLY_PROP_VOLTAGE_OCV);
	if (vbatt == -ENODEV || vbatt == -EINVAL) {
		dev_err(&chip->client->dev, "Can't read voltage from FG\n");
		goto sched_task_work;
	}

	/* convert voltage into millivolts */
	vbatt /= 1000;
	dev_warn(&chip->client->dev, "vbatt = %d\n", vbatt);

	/* If vbatt <= 3600mV, leave the VINDPM settings to default */
	if (vbatt <= INPUT_SRC_LOW_VBAT_LIMIT)
		goto sched_task_work;

	if (vbatt > INPUT_SRC_LOW_VBAT_LIMIT &&
		vbatt < INPUT_SRC_MIDL_VBAT_LIMIT)
		vindpm = INPUT_SRC_VOLT_LMT_444;
	else if (vbatt > INPUT_SRC_MIDL_VBAT_LIMIT &&
		vbatt < INPUT_SRC_MIDH_VBAT_LIMIT)
		vindpm = INPUT_SRC_VOLT_LMT_468;
	else if (vbatt > INPUT_SRC_MIDH_VBAT_LIMIT &&
		vbatt < INPUT_SRC_HIGH_VBAT_LIMIT)
		vindpm = INPUT_SRC_VOLT_LMT_476;

	if (!mutex_trylock(&chip->event_lock))
		goto sched_task_work;
	ret = bq24192_modify_vindpm(vindpm);
	if (ret < 0)
		dev_err(&chip->client->dev, "%s failed\n", __func__);
	mutex_unlock(&chip->event_lock);

	/*
	 * BQ driver depends upon the charger interrupt to send notification
	 * to the framework about the HW charge termination and then framework
	 * starts to poll the driver for declaring FULL. Presently the BQ
	 * interrupts are not coming properly, so the driver would notify the
	 * framework when battery is nearing FULL.
	*/
	if (vbatt >= BATTERY_NEAR_FULL(chip->max_cv))
		power_supply_changed(NULL);

sched_task_work:

	curr_health = bq24192_get_battery_health();
	if (prev_health != curr_health) {
		power_supply_changed(&chip->usb);
		dev_warn(&chip->client->dev,
			"%s health status  %d",
				__func__, prev_health);
	}
	prev_health = curr_health;

	if (BQ24192_CHRGR_STAT_BAT_FULL == bq24192_is_charging()) {
		power_supply_changed(&chip->usb);
		dev_warn(&chip->client->dev,
			"%s battery full", __func__);
	}

	schedule_delayed_work(&chip->chrg_task_wrkr, jiffy);
}

static void bq24192_otg_evt_worker(struct work_struct *work)
{
	struct bq24192_chip *chip =
	    container_of(work, struct bq24192_chip, otg_evt_work);
	struct bq24192_otg_event *evt, *tmp;
	unsigned long flags;
	int ret = 0;

	dev_info(&chip->client->dev, "%s\n", __func__);

	spin_lock_irqsave(&chip->otg_queue_lock, flags);
	list_for_each_entry_safe(evt, tmp, &chip->otg_queue, node) {
		list_del(&evt->node);
		spin_unlock_irqrestore(&chip->otg_queue_lock, flags);

		dev_info(&chip->client->dev,
			"%s:%d state=%d\n", __FILE__, __LINE__,
				evt->is_enable);

		ret = bq24192_turn_otg_vbus(chip, evt->is_enable);
		if (ret < 0)
			dev_err(&chip->client->dev, "VBUS ON FAILED:\n");

		spin_lock_irqsave(&chip->otg_queue_lock, flags);
		kfree(evt);

	}
	spin_unlock_irqrestore(&chip->otg_queue_lock, flags);
}


static void bq24192_temp_update_worker(struct work_struct *work)
{
	struct bq24192_chip *chip =
	    container_of(work, struct bq24192_chip, chrg_temp_wrkr.work);
	int fault_reg = 0, fg_temp = 0;
	static bool is_otp_notified;

	dev_info(&chip->client->dev, "%s\n", __func__);
	/* Check if battery fault condition occured. Reading the register
	   value two times to get reliable reg value, recommended by vendor*/
	fault_reg = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (fault_reg < 0) {
		dev_err(&chip->client->dev,
			"Fault status read failed: %d\n", fault_reg);
		goto temp_wrkr_error;
	}
	fault_reg = bq24192_read_reg(chip->client, BQ24192_FAULT_STAT_REG);
	if (fault_reg < 0) {
		dev_err(&chip->client->dev,
			"Fault status read failed: %d\n", fault_reg);
		goto temp_wrkr_error;
	}

	fg_temp = fg_chip_get_property(POWER_SUPPLY_PROP_TEMP);
	if (fg_temp == -ENODEV || fg_temp == -EINVAL) {
		dev_err(&chip->client->dev,
			"Failed to read FG temperature\n");
		/* If failed to read fg temperature, use charger fault
		 * status to identify the recovery */
		if (fault_reg & FAULT_STAT_BATT_TEMP_BITS) {
			schedule_delayed_work(&chip->chrg_temp_wrkr,
				TEMP_THREAD_JIFFIES);
		} else {
			power_supply_changed(&chip->usb);
		}
		goto temp_wrkr_error;
	}
	fg_temp = fg_temp/10;

	if (fg_temp >= chip->pdata->max_temp
		|| fg_temp <= chip->pdata->min_temp) {
		if (!is_otp_notified) {
			dev_info(&chip->client->dev,
				"Battery over temp occurred!!!!\n");
			power_supply_changed(&chip->usb);
			is_otp_notified = true;
		}
	} else if (!(fault_reg & FAULT_STAT_BATT_TEMP_BITS)) {
		/* over temperature is recovered if battery temp
		 * is between min_temp to max_temp and charger
		 * temperature fault bits are cleared */
		is_otp_notified = false;
		dev_info(&chip->client->dev,
			"Battery over temp recovered!!!!\n");
		power_supply_changed(&chip->usb);
		/*Return without reschedule as over temp recovered*/
		return;
	}
	schedule_delayed_work(&chip->chrg_temp_wrkr, TEMP_THREAD_JIFFIES);
	return;

temp_wrkr_error:
	is_otp_notified = false;
	return;
}


static int otg_handle_notification(struct notifier_block *nb,
				   unsigned long event, void *param)
{
	struct bq24192_chip *chip =
	    container_of(nb, struct bq24192_chip, otg_nb);
	struct bq24192_otg_event *evt;

	dev_info(&chip->client->dev, "OTG notification: %lu\n", event);

    	printk("wigman....%s...vbus_Event=%d\n",__func__,vbus_Event);
	if((!OtgOk)&&vbus_Event){
	    vbus_Event = 0;
	    printk("wigman.....clear vbus_Event%d\n",VbusDetach);
	    if(VbusDetach){
#ifndef USB_NOTIFY_CALLBACK
		   PowerStateOk = 0;
#endif
		   Power_State = POWER_SUPPLY_CHARGER_TYPE_NONE;
		   cancel_delayed_work(&chip_extern->power_state_task_wrkr);
		   bq24192_cable_callback(POWER_SUPPLY_CHARGER_TYPE_NONE);
		}else{
			schedule_delayed_work(&chip->power_state_task_wrkr, 1*HZ);	
		}
	}	
	if (!param || event != USB_EVENT_DRIVE_VBUS)
		return NOTIFY_DONE;

	evt = kzalloc(sizeof(*evt), GFP_ATOMIC);
	if (!evt) {
		dev_err(&chip->client->dev,
			"failed to allocate memory for OTG event\n");
		return NOTIFY_DONE;
	}

	evt->is_enable = *(bool *)param;
	dev_info(&chip->client->dev, "evt->is_enable is %d\n", evt->is_enable);
	INIT_LIST_HEAD(&evt->node);

	spin_lock(&chip->otg_queue_lock);
	list_add_tail(&evt->node, &chip->otg_queue);
	spin_unlock(&chip->otg_queue_lock);

	queue_work(system_nrt_wq, &chip->otg_evt_work);
	return NOTIFY_OK;
}

static inline int register_otg_notification(struct bq24192_chip *chip)
{

	int retval;

	INIT_LIST_HEAD(&chip->otg_queue);
	INIT_WORK(&chip->otg_evt_work, bq24192_otg_evt_worker);
	spin_lock_init(&chip->otg_queue_lock);

	chip->otg_nb.notifier_call = otg_handle_notification;

	/*
	 * Get the USB transceiver instance
	 */
	chip->transceiver = usb_get_phy(USB_PHY_TYPE_USB2);
	if (!chip->transceiver) {
		dev_err(&chip->client->dev, "Failed to get the USB transceiver\n");
		return -EINVAL;
	}
	retval = usb_register_notifier(chip->transceiver, &chip->otg_nb);
	if (retval) {
		dev_err(&chip->client->dev,
			"failed to register otg notifier\n");
		return -EINVAL;
	}

	return 0;
}

int bq24192_slave_mode_enable_charging(int volt, int cur, int ilim)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	int ret;

	mutex_lock(&chip->event_lock);
	chip->inlmt = chrg_ilim_to_reg(ilim);
	if (chip->inlmt >= 0)
		bq24192_set_inlmt(chip, chip->inlmt);
	mutex_unlock(&chip->event_lock);

	chip->cc = chrg_cur_to_reg(cur);
	if (chip->cc)
		bq24192_set_cc(chip, chip->cc);

	chip->cv = chrg_volt_to_reg(volt);
	if (chip->cv)
		bq24192_set_cv(chip, chip->cv);

	mutex_lock(&chip->event_lock);
	ret = bq24192_enable_charging(chip, true);
	if (ret < 0)
		dev_err(&chip->client->dev, "charge enable failed\n");

	mutex_unlock(&chip->event_lock);
	return ret;
}
EXPORT_SYMBOL(bq24192_slave_mode_enable_charging);

int bq24192_slave_mode_disable_charging(void)
{
	struct bq24192_chip *chip = i2c_get_clientdata(bq24192_client);
	int ret;

	mutex_lock(&chip->event_lock);
	ret = bq24192_enable_charging(chip, false);
	if (ret < 0)
		dev_err(&chip->client->dev, "charge enable failed\n");

	mutex_unlock(&chip->event_lock);
	return ret;
}
//--------------------------------------------------------------------
/* show mode entry (auto, none, host, dedicated or boost) */
static ssize_t bq2415x_sysfs_show_mode(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	ssize_t ret = 0;
	
	switch(g_charger_mode){
	case 0:
		ret += sprintf(buf+ret, "off");
		break;
	case 1:
	case 2:
		ret += sprintf(buf+ret, "dedicated");
		break;
	default: break;
		}

	ret += sprintf(buf+ret, "\n");
	return ret;
}
static ssize_t bq2415x_sysfs_set_mode(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	int ret = 0;
	if (strncmp(buf, "off", 3) == 0){
 	
		suspend_flag=0;
		g_charger_mode=0;
		
		//--register
		printk("Find: Stop charge 6\n");
		mutex_lock(&chip_extern->event_lock);
		    ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_INPUT_SRC_CNTL_REG,0x80,true);
		    if (ret < 0) {
			     mutex_unlock(&chip_extern->event_lock);
			     printk("Error in writing the control register\n");
			   return ret;
		 
		     }
		mutex_unlock(&chip_extern->event_lock);
		
		mutex_lock(&chip_extern->event_lock);
		ret=bq24192_reg_read_modify(chip_extern->client,
		                 BQ24192_POWER_ON_CFG_REG,0x10, false);
		if (ret < 0) {
			mutex_unlock(&chip_extern->event_lock);
			printk("xia:Error in writing the control register\n");
			return ret;
		}
		chip_extern->is_charging_enabled=false;
		mutex_unlock(&chip_extern->event_lock);
		if(WakeLockFlag==1){
			printk("y1\n");
			 WakeLockFlag=0;
			 bq24192_ac_unlock();
		}
		cur_cable_status = UG31XX_NO_CABLE;
		AC_STATE   = 0;
		USB_STATE = 0;
		//bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);

	    printk("xia0:g_charger_mode=%d,suspend_flag=%d\n",g_charger_mode,suspend_flag);
		
 	}
	 else if(strncmp(buf,"dedicated",9) == 0){
	
		suspend_flag=0;

		//--register
		printk("Find: Open charge 5\n");
		mutex_lock(&chip_extern->event_lock);
		    ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_INPUT_SRC_CNTL_REG,0x80,false);
		    if (ret < 0) {
			     mutex_unlock(&chip_extern->event_lock);
			     printk("Error in writing the control register\n");
			   return ret;
		 
		     }
		mutex_unlock(&chip_extern->event_lock);
		
		mutex_lock(&chip_extern->event_lock);
		ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_POWER_ON_CFG_REG,0x10, true);
		if (ret < 0) {
			mutex_unlock(&chip_extern->event_lock);
			printk("xia:Error in writing the control register\n");
			return ret;
		}
		chip_extern->is_charging_enabled=true;
		mutex_unlock(&chip_extern->event_lock);

		int type;
		if((!VbusDetach)&&(!OtgOk)){
#ifndef USB_NOTIFY_CALLBACK
			type = GetPowerState();
#endif
			if(type== POWER_SUPPLY_CHARGER_TYPE_USB_SDP){
		        if(WakeLockFlag==0){
					printk("y2\n");
			    bq24192_ac_lock();
			    WakeLockFlag=1;
		        }
			    cur_cable_status = UG31XX_USB_PC_CABLE;
			    AC_STATE   = 0;
			    USB_STATE = 1;
			   g_charger_mode=1;
			   bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
		    }
			else if(type== POWER_SUPPLY_CHARGER_TYPE_USB_DCP){
			
			    if(WakeLockFlag==0){
					printk("y3\n");
			      bq24192_ac_lock();
			      WakeLockFlag=1;
		         }
				cur_cable_status = UG31XX_AC_ADAPTER_CABLE;
				AC_STATE   = 1;
				USB_STATE = 0;
				g_charger_mode=2;
				bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x35);
		   }
		   else {
		   	    if(WakeLockFlag==1){
					printk("y4\n");
			      WakeLockFlag=0;
			      bq24192_ac_unlock();
		        }
		   	    cur_cable_status = UG31XX_NO_CABLE;
		        AC_STATE   = 0;
		        USB_STATE = 0;
		        bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
			   g_charger_mode=-1;
		   }
			   
		}
        else{
			if(WakeLockFlag==1){
				printk("y5\n");
			      WakeLockFlag=0;
			      bq24192_ac_unlock();
		        }
		   	    cur_cable_status = UG31XX_NO_CABLE;
		        AC_STATE   = 0;
		        USB_STATE = 0;
		        bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
            g_charger_mode=-1;
        }
		printk("xia1:g_charger_mode=%d,suspend_flag=%d\n",g_charger_mode,suspend_flag);
			
	}
	
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_USB]);
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_AC]);
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_bq21455]);

	return count;
}

/* close ac suspend mode */
static ssize_t bq2415x_sysfs_set_ac_suspend_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)//lambert
{
	int ret=0;
#ifndef USB_NOTIFY_CALLBACK
	if (strncmp(buf, "close", 5) == 0) 
	{
		suspend_flag=1;
		printk("xiahuang2:suspend-flag=%d\n",suspend_flag);
		printk("Find: Open charge 6\n");

		mutex_lock(&chip_extern->event_lock);
		    ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_INPUT_SRC_CNTL_REG,0x80,false);
		    if (ret < 0) {
			     mutex_unlock(&chip_extern->event_lock);
			     printk("Error in writing the control register\n");
			   return ret;
		 
		     }
		mutex_unlock(&chip_extern->event_lock);
		
		mutex_lock(&chip_extern->event_lock);
		ret=bq24192_reg_read_modify(chip_extern->client,
		              BQ24192_POWER_ON_CFG_REG,0x10, true);
		if (ret < 0) {
			mutex_unlock(&chip_extern->event_lock);
			printk("xia:Error in writing the control register\n");
			return ret;
		}
		chip_extern->is_charging_enabled=true;
		mutex_unlock(&chip_extern->event_lock);

		int type;
		if((!VbusDetach)&&(!OtgOk)){
			type = GetPowerState();
			if(type== POWER_SUPPLY_CHARGER_TYPE_USB_SDP){
		        if(WakeLockFlag==0){
					printk("y2\n");
			    bq24192_ac_lock();
			    WakeLockFlag=1;
		        }
			    cur_cable_status = UG31XX_USB_PC_CABLE;
			    AC_STATE   = 0;
			    USB_STATE = 1;
			   g_charger_mode=-1;
			   bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
		    }
			else if(type== POWER_SUPPLY_CHARGER_TYPE_USB_DCP){
			
			    if(WakeLockFlag==0){
					printk("y3\n");
			      bq24192_ac_lock();
			      WakeLockFlag=1;
		         }
				cur_cable_status = UG31XX_AC_ADAPTER_CABLE;
				AC_STATE   = 1;
				USB_STATE = 0;
				g_charger_mode=-1;
				bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x35);
		   }
		   else {
		   	    if(WakeLockFlag==1){
					printk("y4\n");
			      WakeLockFlag=0;
			      bq24192_ac_unlock();
		        }
		   	    cur_cable_status = UG31XX_NO_CABLE;
		        AC_STATE   = 0;
		        USB_STATE = 0;
		        bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
			   g_charger_mode=-1;
		   }
			   
		}
        else{
			if(WakeLockFlag==1){
				printk("y5\n");
			      WakeLockFlag=0;
			      bq24192_ac_unlock();
		        }
		   	    cur_cable_status = UG31XX_NO_CABLE;
		        AC_STATE   = 0;
		        USB_STATE = 0;
		        bq24192_write_reg(bq24192_client, BQ24192_INPUT_SRC_CNTL_REG, 0x32);
            g_charger_mode=-1;
        }
		printk("xia1:g_charger_mode=%d,suspend_flag=%d\n",g_charger_mode,suspend_flag);
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_USB]);
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_AC]);
	power_supply_changed(&bq24192_supply[PWR_SUPPLY_bq21455]);	
	} 
#endif
	return count;
}


static ssize_t bq2415x_sysfs_show_ac_suspend_mode(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t ret = 0;
    printk("xia2:g_charger_mode=%d,suspend_flag=%d\n",g_charger_mode,suspend_flag);
	if(suspend_flag==1) ret += sprintf(buf+ret, "close");
	else ret += sprintf(buf+ret, "open");
	
	ret += sprintf(buf+ret, "\n");
	return ret;
}

static DEVICE_ATTR(ac_suspend_mode, S_IRWXU | S_IRWXG ,
		bq2415x_sysfs_show_ac_suspend_mode, bq2415x_sysfs_set_ac_suspend_mode);
static DEVICE_ATTR(mode, S_IRWXU |S_IRWXG ,
		bq2415x_sysfs_show_mode, bq2415x_sysfs_set_mode);

static struct attribute *bq2415x_sysfs_attributes[] = {
	/*
	 * TODO: some (appropriate) of these attrs should be switched to
	 * use power supply class props.
	 */
	&dev_attr_ac_suspend_mode.attr,
	&dev_attr_mode.attr,
	NULL,
};

static const struct attribute_group bq2415x_sysfs_attr_group = {
	.attrs = bq2415x_sysfs_attributes,
};

static int bq2415x_sysfs_init(void)
{
	return sysfs_create_group(&bq24192_supply[2].dev->kobj,
			&bq2415x_sysfs_attr_group);
}

static void bq2415x_sysfs_exit(void)
{
	sysfs_remove_group(&bq24192_supply[2].dev->kobj, &bq2415x_sysfs_attr_group);
}
//--------------------------------------------------------------------
extern int intel_mid_pmic_writeb(int reg, u8 val);
static int g_usb_state;
struct workqueue_struct *charger_work_queue = NULL;
struct delayed_work charger_work;

void charger_enabled_poweron() {
       int ret = 0;

        mutex_lock(&chip_extern->event_lock);
	ret=bq24192_reg_read_modify(chip_extern->client, BQ24192_INPUT_SRC_CNTL_REG,0x80,false);
	if (ret < 0) {
		mutex_unlock(&chip_extern->event_lock);
		printk("Error in writing the control register\n");
		return; 
	}
	mutex_unlock(&chip_extern->event_lock);
			
	mutex_lock(&chip_extern->event_lock);
	ret=bq24192_reg_read_modify(chip_extern->client,
	BQ24192_POWER_ON_CFG_REG,0x10, true);
	if (ret < 0) {
		mutex_unlock(&chip_extern->event_lock);
		printk("Error in writing the control register\n");
		 return; 
	}
	chip_extern->is_charging_enabled = true;
	mutex_unlock(&chip_extern->event_lock);
}

static void do_charger(struct work_struct *work) { 
        bq24192_cable_callback(g_usb_state);
}

int setCharger(int usb_state) {
        g_usb_state = usb_state;
        queue_delayed_work(charger_work_queue, &charger_work, 0);

	return 0;
}
#ifdef USB_NOTIFY_CALLBACK
extern unsigned int query_cable_status(void);


static int cable_status_notify(struct notifier_block *self, unsigned long action, void *dev) {

/*
   if (ischargerSuspend) {
       printk(KERN_INFO "%s chager is suspend but USB still notify !!!\n", __func__);
       wake_lock(&wakelock_smb347);
       isUSBSuspendNotify = true;
       return NOTIFY_OK;
   }
*/

   switch (action) {
      case POWER_SUPPLY_CHARGER_TYPE_USB_SDP:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_USB_SDP !!!\n", __func__);
          setCharger(action);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_USB_CDP:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_USB_CDP !!!\n", __func__);
          setCharger(action);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_USB_DCP:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_USB_DCP !!!\n", __func__);
          setCharger(action);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_ACA_DOCK:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_ACA_DOCK !!!\n", __func__);
          setCharger(action);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_SE1:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_SE1 !!!\n", __func__);
          setCharger(action);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_USB_OTG_CONNECTED:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_USB_OTG_CONNECTED !!!\n", __func__);
          schedule_delayed_work(&chip_extern->otg_wrkr, 0*HZ);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_USB_OTG_DISCONNECTED:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_USB_OTG_DISCONNECTED !!!\n", __func__);
          schedule_delayed_work(&chip_extern->otg_wrkr, 0*HZ);
          break;

      case POWER_SUPPLY_CHARGER_TYPE_NONE:
          printk(KERN_INFO "%s POWER_SUPPLY_CHARGER_TYPE_NONE !!!\n", __func__);
          setCharger(action);
	  break;

      default:
          printk(KERN_INFO "%s no status = %d !!!\n", __func__, (int)action);
	  break;
   }
   return NOTIFY_OK;
}

static struct notifier_block cable_status_notifier = {
	.notifier_call = cable_status_notify,
};

extern int cable_status_register_client(struct notifier_block *nb);
extern int cable_status_unregister_client(struct notifier_block *nb);
#endif

static int bq24192_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct bq24192_chip *chip;
	int ret,gpio_handle,gpio_data,irq,rgrt;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "platform Data is NULL");
		return -EFAULT;
	}
	
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
				"SMBus doesn't support BYTE transactions\n");
		return -EIO;
	}

	chip = kzalloc(sizeof(struct bq24192_chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "mem alloc failed\n");
		return -ENOMEM;
	}

	chip->pdata = client->dev.platform_data;
	/*assigning default value for min and max temp*/
	chip->min_temp = BATT_TEMP_MIN_DEF;
	chip->max_temp = BATT_TEMP_MAX_DEF;
	i2c_set_clientdata(client, chip);

	ret = bq24192_read_reg(client, BQ24192_VENDER_REV_REG);
	if (ret < 0) {
		dev_err(&client->dev, "i2c read err:%d\n", ret);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		return -EIO;
	}

	/* D3, D4, D5 indicates the chip model number */
	ret = (ret >> 3) & 0x07;
	if (ret != BQ2419x_IC_VERSION) {
		dev_err(&client->dev, "device version mismatch: %x\n", ret);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		return -EIO;
	}

        bq24192_client = client;
	chip_extern = chip;
	chip->client = client;

	/*
	 * Initialize the platform data
	 */
	if (chip->pdata->init_platform_data) {
		ret = chip->pdata->init_platform_data();
		if (ret < 0) {
			dev_err(&chip->client->dev,
					"FAILED: init_platform_data\n");
		}
	}


	/*
	 * Request for charger chip gpio.This will be used to
	 * register for an interrupt handler for servicing charger
	 * interrupts
	 */
	 
	if (chip->pdata->get_irq_number) {
		chip->irq = chip->pdata->get_irq_number();
		if (chip->irq < 0) {
			dev_err(&chip->client->dev,
				"chgr_int_n GPIO is not available\n");
		} else {
			ret = request_threaded_irq(chip->irq,
					bq24192_irq_isr, bq24192_irq_thread,
					IRQF_TRIGGER_FALLING, "BQ24192", chip);
			if (ret) {
				dev_warn(&bq24192_client->dev,
					"failed to register irq for pin %d\n",
					chip->irq);
			} else {
				dev_warn(&bq24192_client->dev,
					"registered charger irq for pin %d\n",
					chip->irq);
			}
		}
	}

        enable_irq_wake(chip->irq);
	intel_mid_pmic_writeb(R_PMIC_MIRQS0,0x00);
	intel_mid_pmic_writeb(R_PMIC_MIRQSX,0x00);	
	
	INIT_DELAYED_WORK(&chip->otg_wrkr, otg_worker);

#ifndef USB_NOTIFY_CALLBACK
	gpio_request(152, "CHRGE_OTG");
	gpio_handle = acpi_get_gpio("\\_SB.GPO2", 22);
	gpio_direction_input(gpio_handle);
	irq = gpio_to_irq(152);
	ret = request_threaded_irq(irq,
	otg_irq_isr, otg_irq_thread,
	IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING|IRQF_DISABLED|IRQF_ONESHOT, "OtgTrip", NULL);
	if(ret)
		printk("failed to register irq for pin %d\n",irq);
#endif

	ChargeInit(POWER_SUPPLY_CHARGER_TYPE_USB_SDP);

	gpio_handle = acpi_get_gpio("\\_SB.GPO2", 22);//OTG function
	gpio_data = __gpio_get_value(gpio_handle);
	printk("OTG_ID:%d\n",gpio_data);
	if(gpio_data == 0){
		 OtgOk = 1;
		 bq24192_reg_read_modify(chip->client,BQ24192_POWER_ON_CFG_REG,0x20, true);	
	}else{
		OtgOk = 0;
		bq24192_reg_read_modify(chip->client,BQ24192_POWER_ON_CFG_REG,0x20, false);
	}

	INIT_DELAYED_WORK(&chip->chrg_full_wrkr, bq24192_full_worker);
	INIT_DELAYED_WORK(&chip->chrg_task_wrkr, bq24192_task_worker);
	INIT_DELAYED_WORK(&chip->power_state_task_wrkr, bq24192_PowerState_worker);
	INIT_DELAYED_WORK(&chip->jeita_wrkr, JetiaWork);
	INIT_DELAYED_WORK(&chip->otg_disable_wrkr, otg_disable_worker);
	schedule_delayed_work(&chip->chrg_full_wrkr, 3*HZ);
#ifdef ASUS_ENG_BUILD
        schedule_delayed_work(&chip->jeita_wrkr, 20*HZ);
#else
	schedule_delayed_work(&chip->jeita_wrkr, 60*HZ);
#endif
	//INIT_DELAYED_WORK(&chip->chrg_temp_wrkr, bq24192_temp_update_worker);
	mutex_init(&chip->event_lock);

	/* Initialize the wakelock */
	wake_lock_init(&chip->wakelock, WAKE_LOCK_SUSPEND,
						"ctp_charger_wakelock");

	/* register bq24192 usb with power supply subsystem */
	if (!chip->pdata->slave_mode) {
		chip->usb.name = CHARGER_PS_NAME;
		chip->usb.type = POWER_SUPPLY_TYPE_USB;
		chip->usb.supplied_to = chip->pdata->supplied_to;
		chip->usb.num_supplicants = chip->pdata->num_supplicants;
		chip->usb.throttle_states = chip->pdata->throttle_states;
		chip->usb.num_throttle_states =
					chip->pdata->num_throttle_states;
		chip->usb.supported_cables = chip->pdata->supported_cables;
		chip->max_cc = 1216;
		chip->max_cv = 4200;
		chip->bat_health = POWER_SUPPLY_HEALTH_GOOD;
		chip->chgr_stat = BQ24192_CHRGR_STAT_UNKNOWN;
		chip->usb.properties = bq24192_usb_props;
		chip->usb.num_properties = ARRAY_SIZE(bq24192_usb_props);
		chip->usb.get_property = bq24192_usb_get_property;
		chip->usb.set_property = bq24192_usb_set_property;
		ret = power_supply_register(&client->dev, &chip->usb);
		if (ret) {
			dev_err(&client->dev, "failed:power supply register\n");
			i2c_set_clientdata(client, NULL);
			kfree(chip);
			return ret;
		}
	}
	/* Init Runtime PM State */
	//pm_runtime_put_noidle(&chip->client->dev);
	//pm_schedule_suspend(&chip->client->dev, MSEC_PER_SEC);

	/* create debugfs for maxim registers */
	ret = bq24192_create_debugfs(chip);
	if (ret < 0) {
		dev_err(&client->dev, "debugfs create failed\n");
		power_supply_unregister(&chip->usb);
		i2c_set_clientdata(client, NULL);
		kfree(chip);
		return ret;
	}
        charger_work_queue = create_singlethread_workqueue("charger_workqueue");
        INIT_DELAYED_WORK(&charger_work, do_charger);
#ifdef USB_NOTIFY_CALLBACK
	cable_status_register_client(&cable_status_notifier);
        cable_status_notify( NULL, query_cable_status(), &client->dev);
#else
	/*
	 * Register to get USB transceiver events
	 */
	ret = register_otg_notification(chip);
	if (ret) {
		dev_err(&chip->client->dev,
					"REGISTER OTG NOTIFICATION FAILED\n");
	}
#endif
	/* Program the safety charge temperature threshold with default value*/
	ret =  intel_scu_ipc_iowrite8(MSIC_CHRTMPCTRL,
				(CHRTMPCTRL_TMPH_45 | CHRTMPCTRL_TMPL_00));
	if (ret) {
		dev_err(&chip->client->dev,
				"IPC Failed with %d error\n", ret);
	}

	bq2415x_sysfs_flag=bq24192_powersupply_init(client);
	if(!bq2415x_sysfs_flag){	
		bq2415x_sysfs_init();
	}

	intel_mid_pmic_writeb(0x24, 0xaa);//vaild battery detection threshold 3.5v
#ifdef  ASUS_ENG_BUILD
        ret = init_asus_charging_limit_toggle();
        if (ret) {
            printk("Unable to create proc file\n");
            return ret;
        }
#endif
	return 0;
}

static int bq24192_remove(struct i2c_client *client)
{
	struct bq24192_chip *chip = i2c_get_clientdata(client);

	bq24192_remove_debugfs(chip);
#ifdef USB_NOTIFY_CALLBACK
	cable_status_unregister_client(&cable_status_notifier);
#endif
	if (!chip->pdata->slave_mode)
		power_supply_unregister(&chip->usb);

	if (chip->irq > 0)
		free_irq(chip->irq, chip);
	
    if(!bq2415x_sysfs_flag){
	
		bq2415x_sysfs_exit();
	}
	i2c_set_clientdata(client, NULL);
	wake_lock_destroy(&chip->wakelock);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM

static void bq24192_shutdown(struct i2c_client *client)
{
	//disable otg
	if(OtgOk){
		bq24192_reg_read_modify(chip_extern->client,
		   BQ24192_POWER_ON_CFG_REG,
		   0x20, false);
		OtgOk = 0;
		}
	
	cancel_delayed_work(&chip_extern->chrg_full_wrkr);
	cancel_delayed_work(&chip_extern->chrg_task_wrkr);
	cancel_delayed_work(&chip_extern->power_state_task_wrkr);
	flush_delayed_work(&chip_extern->chrg_full_wrkr);
	flush_delayed_work(&chip_extern->chrg_task_wrkr);
	flush_delayed_work(&chip_extern->power_state_task_wrkr);
}

static int bq24192_suspend(struct device *dev)
{
	struct bq24192_chip *chip = dev_get_drvdata(dev);
	int ret,reg;

	printk("bq24192_suspend\n");
	flush_delayed_work(&chip->chrg_full_wrkr);
	flush_delayed_work(&chip->chrg_task_wrkr);
	flush_delayed_work(&chip->power_state_task_wrkr);
	if (chip->irq > 0) {
		/*
		 * Once the WDT is expired all bq24192 registers gets
		 * set to default which means WDT is programmed to 40s
		 * and if there is no charger connected, no point
		 * feeding the WDT. Since reg07[1] is set to default,
		 * charger will interrupt SOC every 40s which is not
		 * good for S3. In this case we need to free chgr_int_n
		 * interrupt so that no interrupt from charger wakes
		 * up the platform in case of S3. Interrupt will be
		 * re-enabled on charger connect.
		 */
		if (chip->irq > 0)
			free_irq(chip->irq, chip);
	}
	reset_wdt_timer(chip);
	ret = program_timers(chip, CHRG_TIMER_EXP_CNTL_WDTDISABLE, false);
	if (ret < 0)
		dev_warn(&chip->client->dev, "TIMER enable failed\n");
	dev_dbg(&chip->client->dev, "bq24192 suspend\n");
	return 0;
}

static int bq24192_resume(struct device *dev)
{
	struct bq24192_chip *chip = dev_get_drvdata(dev);
	int ret,gpio_handle,gpio_data;

	printk("bq24192_resume\n");
	schedule_delayed_work(&chip->chrg_full_wrkr, 0);
	if (chip->irq > 0) {
		ret = request_threaded_irq(chip->irq,
				bq24192_irq_isr, bq24192_irq_thread,
				IRQF_TRIGGER_FALLING, "BQ24192", chip);
		if (ret) {
			dev_warn(&bq24192_client->dev,
				"failed to register irq for pin %d\n",
				chip->irq);
		} else {
			dev_warn(&bq24192_client->dev,
				"registered charger irq for pin %d\n",
				chip->irq);
		}
	}
	ChargeInit(cable_status);

	dev_dbg(&chip->client->dev, "bq24192 resume\n");
	return 0;
}
#else
#define bq24192_suspend NULL
#define bq24192_resume NULL
#endif

#ifdef CONFIG_PM_RUNTIME
static int bq24192_runtime_suspend(struct device *dev)
{

	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int bq24192_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int bq24192_runtime_idle(struct device *dev)
{

	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}
#else
#define bq24192_runtime_suspend	NULL
#define bq24192_runtime_resume		NULL
#define bq24192_runtime_idle		NULL
#endif

static const struct i2c_device_id bq24192_id[] = {
	{ DEV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bq24192_id);

static const struct dev_pm_ops bq24192_pm_ops = {
	.suspend		= bq24192_suspend,
	.resume			= bq24192_resume,
	.runtime_suspend	= bq24192_runtime_suspend,
	.runtime_resume		= bq24192_runtime_resume,
	.runtime_idle		= bq24192_runtime_idle,
};

static struct i2c_driver bq24192_i2c_driver = {
	.driver	= {
		.name	= DEV_NAME,
		.owner	= THIS_MODULE, 
		.pm	= &bq24192_pm_ops,
	},
	.probe		= bq24192_probe,
	.remove		= bq24192_remove,
	.id_table	= bq24192_id,
	.shutdown   = bq24192_shutdown,	
};
//--------legacy
static int __init bq24192_init(void)
{	
	return i2c_add_driver(&bq24192_i2c_driver);
}
module_init(bq24192_init);
static void __exit bq24192_exit(void)
{
	i2c_del_driver(&bq24192_i2c_driver);
}
module_exit(bq24192_exit);
/*
static int bq24192_init(void)
{
	return i2c_add_driver(&bq24192_i2c_driver);
}

static void bq24192_exit(void)
{
	i2c_del_driver(&bq24192_i2c_driver);
}

static int bq24192_rpmsg_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;

	if (rpdev == NULL) {
		pr_err("rpmsg channel not created\n");
		ret = -ENODEV;
		goto out;
	}

	dev_info(&rpdev->dev, "Probed bq24192 rpmsg device\n");

	ret = bq24192_init();

out:
	return ret;
}

static void bq24192_rpmsg_remove(struct rpmsg_channel *rpdev)
{
	bq24192_exit();
	dev_info(&rpdev->dev, "Removed bq24192 rpmsg device\n");
}

static void bq24192_rpmsg_cb(struct rpmsg_channel *rpdev, void *data,
					int len, void *priv, u32 src)
{
	dev_warn(&rpdev->dev, "unexpected, message\n");

	print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,
		       data, len,  true);
}

static struct rpmsg_device_id bq24192_rpmsg_id_table[] = {
	{ .name	= "rpmsg_bq24192" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, bq24192_rpmsg_id_table);

static struct rpmsg_driver bq24192_rpmsg = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= bq24192_rpmsg_id_table,
	.probe		= bq24192_rpmsg_probe,
	.callback	= bq24192_rpmsg_cb,
	.remove		= bq24192_rpmsg_remove,
};

static int __init bq24192_rpmsg_init(void)
{
	return register_rpmsg_driver(&bq24192_rpmsg);
}
module_init(bq24192_rpmsg_init);

static void __exit bq24192_rpmsg_exit(void)
{
	return unregister_rpmsg_driver(&bq24192_rpmsg);
}
*/
MODULE_AUTHOR("Ramakrishna Pallala <ramakrishna.pallala@intel.com>");
MODULE_AUTHOR("Raj Pandey <raj.pandey@intel.com>");
MODULE_DESCRIPTION("BQ24192 Charger Driver");
MODULE_LICENSE("GPL");
