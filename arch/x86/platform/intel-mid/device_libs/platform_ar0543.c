/*
 * platform_ar0543.c: ar0543 platform data initilization file
 *
 * (C) Copyright 2008 Intel Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/atomisp_platform.h>
#include <asm/intel_scu_ipcutil.h>
#include <asm/intel-mid.h>
#include <media/v4l2-subdev.h>
#include <linux/regulator/consumer.h>
#include <linux/sfi.h>
#include <linux/mfd/intel_mid_pmic.h>
#include <linux/vlv2_plat_clock.h>
#include <asm/intel_vlv2.h>
#include <linux/lnw_gpio.h>

#include "platform_camera.h"
#include "platform_ar0543.h"

extern int PCBVersion;

/* workround - pin defined for byt */
#ifdef CONFIG_VLV2_PLAT_CLK
#define OSC_CAM0_CLK	0x0
#define CLK_19P2MHz	0x1
#endif

static int camera_reset;
static int camera_power_down;
static int camera_vcm_power_down;
static int camera_vprog1_on;
static int camera_I2C_3_SCL;
static int camera_I2C_3_SDA;

/*
 * CLV PR0 primary camera sensor - AR0543 platform data
 */
static int ar0543_i2c_gpio_set_alt(int flag)
{
	int ret;
	lnw_gpio_set_alt(SIO_I2C3_SCL, LNW_GPIO);
    lnw_gpio_set_alt(SIO_I2C3_SDA, LNW_GPIO);

	if (flag){
	    if (camera_I2C_3_SCL < 0) {
	        ret = camera_sensor_gpio(SIO_I2C3_SCL, GP_I2C_3_SCL,
	                GPIOF_DIR_OUT, 1);
	        if (ret < 0){
	            printk("%s not available.\n", GP_I2C_3_SCL);
	            return ret;
	        }
	        camera_I2C_3_SCL = SIO_I2C3_SCL;
	    }

	    if (camera_I2C_3_SDA < 0) {
	        ret = camera_sensor_gpio(SIO_I2C3_SDA, GP_I2C_3_SDA,
	                GPIOF_DIR_OUT, 1);
	        if (ret < 0){
	            printk("%s not available.\n", GP_I2C_3_SDA);
	            return ret;
	        }
	        camera_I2C_3_SDA = SIO_I2C3_SDA;
	    }

	    if (camera_I2C_3_SCL >= 0){
	        gpio_set_value(camera_I2C_3_SCL, 1);
	        printk("<<< I2C_4 SCL = 1\n");
	        msleep(1);
	    }

	    if (camera_I2C_3_SDA >= 0){
	        gpio_set_value(camera_I2C_3_SDA, 1);
	        printk("<<< I2C_4 SDA = 1\n");
	        msleep(1);
	    }
		lnw_gpio_set_alt(SIO_I2C3_SCL, LNW_ALT_1);
	    lnw_gpio_set_alt(SIO_I2C3_SDA, LNW_ALT_1);

	    msleep(2);
	}else{
		if (camera_I2C_3_SCL >= 0){
			gpio_free(camera_I2C_3_SCL);
			camera_I2C_3_SCL = -1;
			mdelay(1);
		}
		
		if (camera_I2C_3_SDA >= 0){
			gpio_free(camera_I2C_3_SDA);
			camera_I2C_3_SDA = -1;
			mdelay(1);
		}
	}
	return ret;
}
static int ar0543_gpio_init()
{
	int pin;
	int ret = 0;
	pin = CAMERA_0_RESET;
	if (camera_reset < 0) {
		ret = gpio_request(pin, NULL);
		if (ret) {
			pr_err("%s: failed to request gpio(pin %d)\n",
				__func__, pin);
			return ret;
		}

		camera_reset = pin;
		ret = gpio_direction_output(pin, 0);
		if (ret) {
			pr_err("%s: failed to set gpio(pin %d) direction\n",
				__func__, pin);
			gpio_free(pin);
			return ret;
		}
	}
	pin = CAMERA_0_PWDN;
	if (camera_power_down < 0) {
		ret = gpio_request(pin, NULL);
		if (ret) {
			pr_err("%s: failed to request gpio(pin %d)\n",
				__func__, pin);
			return ret;
		}
		camera_power_down = pin;
		ret = gpio_direction_output(pin, 0);
		if (ret) {
			pr_err("%s: failed to set gpio(pin %d) direction\n",
			__func__, pin);
			gpio_free(pin);
			return ret;
		}
	}
	return ret;
}
static int ar0543_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	//move to power ctrl ar0543_gpio_init();
	
	if (flag) {
#ifdef CONFIG_BOARD_CTP
		if(camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			msleep(60);
		}
#endif
		hm2056_set_gpio(1,1);
		if(camera_power_down >= 0){
			gpio_set_value(camera_power_down, 1);
			//msleep(2);
		}
		if(camera_reset >= 0){
			gpio_set_value(camera_reset, 1);
			msleep(1);
		}
		camera_set_pmic_power(CAMERA_2P8V, true);//move from power ctrl
        //gpio_set_value(camera_vcm_power_down, 1);
	} else {
		if(camera_power_down >= 0){
			gpio_set_value(camera_power_down, 0);
		}
		if(camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			msleep(2);
		}
        //gpio_set_value(camera_vcm_power_down, 0);
		hm2056_set_gpio(1,0);

		if(PCBVersion==-1 || PCBVersion==0){
			hm2056_free_gpio();
			if (camera_reset >= 0){
				gpio_free(camera_reset);
				camera_reset = -1;
				mdelay(1);
			}

			if (camera_power_down >= 0){
				gpio_free(camera_power_down);
				camera_power_down = -1;
				mdelay(1);
			}
		}
    }

	ar0543_i2c_gpio_set_alt(flag);
	return 0;
}

static int ar0543_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
	int ret = 0;

#ifdef CONFIG_VLV2_PLAT_CLK
	if (flag) {
		ret = vlv2_plat_set_clock_freq(OSC_CAM0_CLK, CLK_19P2MHz);
		if (ret)
			return ret;
	}
	ret = vlv2_plat_configure_clock(OSC_CAM0_CLK, flag?flag:2);
	if (flag) {
		msleep(1);
	}
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
	return intel_scu_ipc_osc_clk(OSC_CLK_CAM0, flag ? clock_khz : 0);
#else
	pr_err("ar0543 clock is not set.\n");
	return 0;
#endif
}

/*
 * Checking the SOC type is temporary workaround to enable AR0543
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog1, vprog2) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int ar0543_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int ret = 0;
	pr_err("%s ++.\n",__func__);

	//<ASUS-Vincent_Liu-20131219180431+>
	//BD2610GW PMIC
	//TODO 1.8v <--> V1P8SX
	//TODO 2.8v <--> V2P85SX
	if (flag) {
		if (!camera_vprog1_on) {

#ifdef CONFIG_CRYSTAL_COVE
			/*
			 * This should call VRF APIs.
			 *
			 * VRF not implemented for BTY, so call this
			 * as WAs
			 */
			ret = camera_set_pmic_power(CAMERA_1P8V, true);
			//msleep(2);
			//if (ret)
			//	return ret;
			//Move to gpio_ctrl ret = camera_set_pmic_power(CAMERA_2P8V, true);
			//msleep(2);

#endif
			if (!ret) {
				camera_vprog1_on = 1;
			} else {
				camera_set_pmic_power(CAMERA_1P8V, false);
			}
			ar0543_gpio_init();//move from gpio ctrl
			printk("<<< %s 1.8V and 2.8V = 1\n",__FUNCTION__);
			return ret;
		}
	} else {
		if (camera_vprog1_on) {		
#ifdef CONFIG_CRYSTAL_COVE
			ret = camera_set_pmic_power(CAMERA_2P8V, false);
			msleep(1);
			//FIXME: bug anyway
			if (ret)
				return ret;
			ret = camera_set_pmic_power(CAMERA_1P8V, false);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
			return intel_scu_ipc_msic_vprog1(0);
#else
			pr_err("ar0543 power ctrl is not set.\n");
			return -1;
#endif
			if (!ret) {
				camera_vprog1_on = 0;
			}
			printk("<<< %s 1.8V and 2.8V = 0\n",__FUNCTION__);
			return ret;
		}
	}

	return 0;
}

static int ar0543_csi_configure(struct v4l2_subdev *sd, int flag)
{
	static const int LANES = 2;
	//<ASUS-Vincent_Liu-20131223161915+>
	//TODO: atomisp_bayer_order_grbg ??
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, LANES,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_grbg, flag);
}

/*
 * Checking the SOC type is temporary workaround to enable AR0543
 * on Bodegabay (tangier) platform. Once standard regulator devices
 * (e.g. vprog1, vprog2) and control functions (pmic_avp) are added
 * for the platforms with tangier, then we can revert this change.
 * (dongwon.kim@intel.com)
 */
static int ar0543_platform_init(struct i2c_client *client)
{
	return 0;
}

/*
 * Checking the SOC type is temporary workaround to enable AR0543 on Bodegabay
 * (tangier) platform once standard regulator devices (e.g. vprog1, vprog2) and
 * control functions (pmic_avp) are added for the platforms with tangier, then
 * we can revert this change.(dongwon.kim@intel.com
 */
static int ar0543_platform_deinit(void)
{
	return 0;
}
static struct camera_sensor_platform_data ar0543_sensor_platform_data = {
	.gpio_ctrl      = ar0543_gpio_ctrl,
	.flisclk_ctrl   = ar0543_flisclk_ctrl,
	.power_ctrl     = ar0543_power_ctrl,
	.csi_cfg        = ar0543_csi_configure,
	//.platform_init = ar0543_platform_init,
	//.platform_deinit = ar0543_platform_deinit,
};

void *ar0543_platform_data(void *info)
{
	camera_I2C_3_SCL = -1;
	camera_I2C_3_SDA = -1;

	camera_reset = -1;
	camera_power_down = -1;
	camera_vcm_power_down = -1;
	camera_vprog1_on = 0;

	return &ar0543_sensor_platform_data;
}
