/*
 * platform_gc0339.c: GC0339 with iCatch 7002A ISP platform data initilization file
 *
 * (C) Copyright 2013 ASUSTeK COMPUTER INC
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/atomisp_platform.h>
#include <asm/intel_scu_ipcutil.h>
#include <asm/intel-mid.h>
#include <media/v4l2-subdev.h>
#include <linux/mfd/intel_mid_pmic.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_VLV2_PLAT_CLK
#include <linux/vlv2_plat_clock.h>
#endif

#include "platform_camera.h"
#include "platform_gc0339.h"

#ifdef CONFIG_VLV2_PLAT_CLK
#define OSC_CAM1_CLK 0x1
#define CLK_19P2MHz 0x1
#endif

//#define VPROG1_VAL 1800000

static int camera_power_down;
static int camera_reset;
//static int camera_suspend;
static int camera_vprog1_on;

//static struct regulator *vprog1_reg;


static int gc0339_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	v4l2_err(sd, "%s: ++\n",__func__);
	static const unsigned int clock_khz = 19200; //Intel just can support 19.2MHz/9.6MHz/4.8MHz 
#ifdef CONFIG_VLV2_PLAT_CLK
	int ret = 0;
	if (flag) {
		ret = vlv2_plat_set_clock_freq(OSC_CAM1_CLK, CLK_19P2MHz);
		if (ret)
			return ret;
	}
	return vlv2_plat_configure_clock(OSC_CAM1_CLK, flag);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
	return intel_scu_ipc_osc_clk(OSC_CLK_CAM1,
				     flag ? clock_khz : 0);
#else
	pr_err("gc0339 clock is not set.\n");
	return 0;
#endif
}


static int gc0339_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
	return 0;
}

static int gc0339_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int reg_err;
	int pin;
	int ret;
	printk("%s: ++\n",__func__);

	//if (HW_ID == 0xFF){ 
	//	HW_ID = Read_HW_ID(); 
	//}

	//printk("HW ID:%d\n", HW_ID);
	//switch (HW_ID){
	//	case HW_ID_SR1:
	//	case HW_ID_SR2:
		/*
			if (camera_reset < 0) {
				ret = camera_sensor_gpio(-1, GP_CAMERA_VGA_RESET,
							 GPIOF_DIR_OUT, 0);
				if (ret < 0){
					printk("%s not available.\n", GP_CAMERA_VGA_RESET);
					return ret;
				}
				printk("%s = %d\n",GP_CAMERA_VGA_RESET,ret);
				camera_reset = ret;
			}
		*/
	//		break;
	//	case HW_ID_ER:
	//	case HW_ID_PR:
	//	case HW_ID_MP:
	//	default:
		/*
			if (camera_power_2p8_en < 0) {
				ret = camera_sensor_gpio(-1, GP_CAMERA_2_8V,
							 GPIOF_DIR_OUT, 0);
				if (ret < 0){
					printk("%s not available.\n", GP_CAMERA_2_8V);
					return ret;
				}
				camera_power_2p8_en = ret;
				printk("%s = %d\n",GP_CAMERA_2_8V,ret);
			}
			if (camera_reset < 0) {
				ret = camera_sensor_gpio(-1, GP_CAMERA_VGA_RESET,
							 GPIOF_DIR_OUT, 0);
				if (ret < 0){
					printk("%s not available.\n", GP_CAMERA_VGA_RESET);
					return ret;
				}
				printk("%s = %d\n",GP_CAMERA_VGA_RESET,ret);
				camera_reset = ret;
			}
		*/
	//		break;
	//}


	pin = CAMERA_1_RESET;
	if (camera_reset < 0) {
		ret = gpio_request(pin, "camera_1_reset");
		if (ret) {
			pr_err("%s: failed to request gpio(pin %d)\n",
				__func__, pin);
			return ret;
		}
	}
	camera_reset = pin;
	ret = gpio_direction_output(pin, 0);
	if (ret) {
		pr_err("%s: failed to set gpio(pin %d) direction\n",
			__func__, pin);
		gpio_free(pin);
		return ret;
	}

	pin = CAMERA_1_PWDN;
	if (camera_power_down < 0) {
		ret = gpio_request(pin, "camera_1_power");
		if (ret) {
			pr_err("%s: failed to request gpio(pin %d)\n",
				__func__, pin);
			return ret;
		}
	}
	camera_power_down = pin;
	
	//if (spid.hardware_id == BYT_TABLET_BLK_8PR0 ||
	//	spid.hardware_id == BYT_TABLET_BLK_8PR1)
		ret = gpio_direction_output(pin, 0);
	//else
	//	ret = gpio_direction_output(pin, 1);
	
	if (ret) {
		pr_err("%s: failed to set gpio(pin %d) direction\n",
			__func__, pin);
		gpio_free(pin);
		return ret;
	}

	if (flag){
		//pull high reset first
		if (camera_power_down >= 0){
			gpio_set_value(camera_power_down, 1);
			printk("<<< camera_reset = 1\n");
			msleep(10);
		}

		//turn on power VDD_SEN VDD_HOST 1.8V
		if (!camera_vprog1_on) {
			camera_vprog1_on = 1;
#ifdef CONFIG_CRYSTAL_COVE
			ret = camera_set_pmic_power(CAMERA_2P8V, true);
			if (ret)
				return ret;
			ret = camera_set_pmic_power(CAMERA_1P8V, true);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
			ret = intel_scu_ipc_msic_vprog1(1);
#else
			pr_err("gc0339 power is not set.\n");
#endif
/*
			reg_err = regulator_enable(vprog1_reg);
			if (reg_err) {
				printk(KERN_ALERT "Failed to enable regulator vprog1\n");
				return reg_err;
			}
*/
			printk("<<< VDD_SEN VDD_HOST 1.8V = 1\n");
			msleep(10);
		}

		//turn on MCLK
		gc0339_flisclk_ctrl(sd, 1);
		msleep(10); 

		//pull high reset first
		if (camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			msleep(20);
			gpio_set_value(camera_reset, 1);
			printk("<<< camera_reset = 1\n");
			msleep(10);
		}
	}else{
		//pull low reset first
		if (camera_reset >= 0){
			gpio_set_value(camera_reset, 0);
			printk("<<< camera_reset = 0\n");
			msleep(10);
		}

		//turn off power VDD_SEN VDD_HOST 1.8V
		if (camera_vprog1_on) {
			camera_vprog1_on = 0;
#ifdef CONFIG_CRYSTAL_COVE
			ret = camera_set_pmic_power(CAMERA_2P8V, false);
			if (ret)
				return ret;
			ret = camera_set_pmic_power(CAMERA_1P8V, false);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
			ret = intel_scu_ipc_msic_vprog1(0);
#else
			pr_err("gc0339 power is not set.\n");
#endif
/*
			reg_err = regulator_disable(vprog1_reg);
			if (reg_err) {
				printk(KERN_ALERT "Failed to disable regulator vprog1\n");
				return reg_err;
			}
*/
			printk("<<< VDD_SEN VDD_HOST 1.8V = 0\n");
			msleep(10);
		}
		//pull low reset first
		if (camera_power_down >= 0){
			gpio_set_value(camera_power_down, 0);
			printk("<<< camera_reset = 0\n");
			msleep(10);
		}

		//turn off MCLK
		gc0339_flisclk_ctrl(sd, 0);
		msleep(10); 
	}
		
	return 0;

}

static int gc0339_csi_configure(struct v4l2_subdev *sd, int flag)
{
	/* soc sensor, there is no raw bayer order (set to -1) */
	return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_SECONDARY, 1,
		ATOMISP_INPUT_FORMAT_RAW_10, atomisp_bayer_order_grbg, flag);
}

static int gc0339_platform_init(struct i2c_client *client)
{
	int ret;

	printk("%s: ++\n", __func__);

#ifdef CONFIG_CRYSTAL_COVE
	/*
	 * This should call VRF APIs.
	 *
	 * VRF not implemented for BTY, so call this
	 * as WAs
	 */
	ret = camera_set_pmic_power(CAMERA_2P8V, true);
	if (ret)
		return ret;
	ret = camera_set_pmic_power(CAMERA_1P8V, true);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
	ret = intel_scu_ipc_msic_vprog1(1);
#else
	pr_err("gc0339 power is not set.\n");
#endif

/*
	//VPROG1 for VDD_HOST and VDD_SEN, 1.8V
	vprog1_reg = regulator_get(&client->dev, "vprog1");
	if (IS_ERR(vprog1_reg)) {
		dev_err(&client->dev, "vprog1 failed\n");
		return PTR_ERR(vprog1_reg);
	}
	ret = regulator_set_voltage(vprog1_reg, VPROG1_VAL, VPROG1_VAL);
	if (ret) {
		dev_err(&client->dev, "vprog1 set failed\n");
		regulator_put(vprog1_reg);
	}
*/
	return ret;
}

static int gc0339_platform_deinit(void)
{
	int ret;
#ifdef CONFIG_CRYSTAL_COVE
	ret = camera_set_pmic_power(CAMERA_2P8V, false);
	if (ret)
		return ret;
	ret = camera_set_pmic_power(CAMERA_1P8V, false);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
	ret = intel_scu_ipc_msic_vprog1(0);
#else
	pr_err("gc0339 power is not set.\n");
#endif

	//regulator_put(vprog1_reg);

	return ret;
}

static struct camera_sensor_platform_data gc0339_sensor_platform_data = {
	.gpio_ctrl	 = gc0339_gpio_ctrl,
	.flisclk_ctrl	 = gc0339_flisclk_ctrl,
	.power_ctrl	 = gc0339_power_ctrl,
	.csi_cfg	 = gc0339_csi_configure,
	//.platform_init   = gc0339_platform_init,
	//.platform_deinit = gc0339_platform_deinit,
};

void *gc0339_platform_data(void *info)
{
	camera_power_down = -1;
	camera_reset = -1;
	//camera_suspend = -1;
	camera_vprog1_on = 0;
	return &gc0339_sensor_platform_data;
}
