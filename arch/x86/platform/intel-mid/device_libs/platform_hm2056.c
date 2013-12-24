/*
 * platform_hm2056.c: hm2056 platform data initilization file
 *
 * (C) Copyright 2013 Intel
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
#include <asm/intel-mid.h>
#include <asm/intel_scu_ipcutil.h>
#include <media/v4l2-subdev.h>
#include <linux/regulator/consumer.h>
#include <linux/sfi.h>
#include <linux/lnw_gpio.h>
#ifdef CONFIG_VLV2_PLAT_CLK
#include <linux/vlv2_plat_clock.h>
#endif
#include "platform_camera.h"
#include "platform_hm2056.h"

static int camera_I2C_3_SCL;
static int camera_I2C_3_SDA;

#ifdef CONFIG_VLV2_PLAT_CLK
#define OSC_CAM0_CLK 0x0
#define CLK_19P2MHz  0x1
#endif
//#define VPROG1_VAL 1800000
static int camera_reset;
static int camera_power_down;
static int camera_vprog1_on;

static struct regulator *vprog1_reg;

static int hm2056_gpio_ctrl(struct v4l2_subdev *sd, int flag)
{
    printk("%s: ++\n",__func__);

	if (flag){
		gpio_set_value(camera_reset, 1);
        printk("<<< camera_reset = 1\n");
        mdelay(10);
		gpio_set_value(camera_reset, 0);
        mdelay(10);
		gpio_set_value(camera_reset, 1);
    }
    else{
        gpio_set_value(camera_power_down, 1);
        printk("<<< camera_power_down = 1\n");
        mdelay(10);
		gpio_set_value(camera_reset, 0);
        printk("<<< camera_reset = 0\n");
    }
    mdelay(1);

	return 0;
}

static int hm2056_flisclk_ctrl(struct v4l2_subdev *sd, int flag)
{
	static const unsigned int clock_khz = 19200;
    v4l2_err(sd, "%s: ++\n",__func__);
#ifdef CONFIG_VLV2_PLAT_CLK
	if(flag)
	{
		int ret;
		ret = vlv2_plat_set_clock_freq(OSC_CAM0_CLK,CLK_19P2MHz);
		if(ret){
			return ret;
		}
	}
	return vlv2_plat_configure_clock(OSC_CAM0_CLK,flag);
#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
	return intel_scu_ipc_osc_clk(OSC_CLK_CAM0, flag ? clock_khz : 0);
#else
	pr_err("hm2056 clock is not set.\n");
	return 0;
#endif
}

static int hm2056_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	int reg_err, ret;

    printk("%s: ++\n",__func__);

    if (camera_reset < 0) {
        ret = camera_sensor_gpio(CAMERA_0_RESET, GP_CAMERA_0_RESET, GPIOF_DIR_OUT, 0);
        if (ret < 0){
            printk("camera_reset not available.\n");
            return ret;
        }
        camera_reset = ret;
    }
    printk("<< camera_reset:%d, flag:%d\n", camera_reset, flag);

    if (camera_power_down < 0) {
        ret = camera_sensor_gpio(CAMERA_0_PWDN, GP_CAMERA_0_STANDBY, GPIOF_DIR_OUT, 0);
        if (ret < 0){
            printk("camera_power_down not available.\n");
            return ret;
        }
        camera_power_down = ret;
    }
    printk("<< camera_power_down:%d, flag:%d\n", camera_power_down, flag);

    lnw_gpio_set_alt(SIO_I2C3_SCL, LNW_GPIO);
    lnw_gpio_set_alt(SIO_I2C3_SDA, LNW_GPIO);

    if (camera_I2C_3_SCL < 0) {
        ret = camera_sensor_gpio(SIO_I2C3_SCL, GP_I2C_3_SCL,
                GPIOF_DIR_OUT, 0);
        if (ret < 0){
            printk("%s not available.\n", GP_I2C_3_SCL);
            return ret;
        }
        camera_I2C_3_SCL = SIO_I2C3_SCL;
    }

    if (camera_I2C_3_SDA < 0) {
        ret = camera_sensor_gpio(SIO_I2C3_SDA, GP_I2C_3_SDA,
                GPIOF_DIR_OUT, 0);
        if (ret < 0){
            printk("%s not available.\n", GP_I2C_3_SDA);
            return ret;
        }
        camera_I2C_3_SDA = SIO_I2C3_SDA;
    }

    if (camera_I2C_3_SCL >= 0){
        gpio_set_value(camera_I2C_3_SCL, 0);
        printk("<<< I2C_4 SCL = 0\n");
        msleep(1);
    }

    if (camera_I2C_3_SDA >= 0){
        gpio_set_value(camera_I2C_3_SDA, 0);
        printk("<<< I2C_4 SDA = 0\n");
        msleep(1);
    }
    
	ret = 0;
    if (flag){
        if (camera_reset >= 0){
            gpio_set_value(camera_reset, 0);
            printk("<<< camera_reset = 0\n");
            msleep(1);
        }
        if (camera_power_down >= 0){
            gpio_set_value(camera_power_down, 0);
            printk("<<< camera_power_down = 0\n");
            msleep(1);
        }

        //turn on power 1.8V and 2.8V
        if (!camera_vprog1_on) {
			#ifdef CONFIG_CRYSTAL_COVE
				ret = camera_set_pmic_power(CAMERA_2P8V, true);
				if (ret)
					return ret;
				ret = camera_set_pmic_power(CAMERA_1P8V, true);
			#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
				ret = intel_scu_ipc_msic_vprog1(1);
			#else
				pr_err("hm2056 power is not set.\n");
			#endif
				if (!ret)
					camera_vprog1_on = 1;			
		/*
            reg_err = regulator_enable(vprog1_reg);
            if (reg_err) {
                printk(KERN_ALERT "Failed to enable regulator vprog1\n");
                return reg_err;
            }
            */
            printk("<<< 1.8V and 2.8V = 1\n");
            msleep(1);
        }

        lnw_gpio_set_alt(SIO_I2C3_SCL, LNW_ALT_1);
        lnw_gpio_set_alt(SIO_I2C3_SDA, LNW_ALT_1);

        msleep(2);

		return ret;
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

        //turn off power 1.8V and 2.8V
        if (camera_vprog1_on) {
			#ifdef CONFIG_CRYSTAL_COVE
				ret = camera_set_pmic_power(CAMERA_2P8V, false);
				if (ret)
					return ret;
				ret = camera_set_pmic_power(CAMERA_1P8V, false);
			#elif defined(CONFIG_INTEL_SCU_IPC_UTIL)
				ret = intel_suc_ipc_msic_vprog1(0);
			#else
				pr_err("hm2056 power is not set.\n");
			#endif
				if (!ret)
					camera_vprog1_on = 0;
		/*
            reg_err = regulator_disable(vprog1_reg);
            if (reg_err) {
                printk(KERN_ALERT "Failed to disable regulator vprog1\n");
                return reg_err;
            }
            */
            printk("<<< 1.8V and 2.8V = 0\n");
            msleep(1);
        }
        if (camera_power_down >= 0){
            gpio_set_value(camera_power_down, 0);
            printk("<<< camera_power_down = 0\n");
            msleep(1);
        }
        return ret;
    }
    return 0;
}

static int hm2056_csi_configure(struct v4l2_subdev *sd, int flag)
{
    static const int LANES = 4;
    return camera_sensor_csi(sd, ATOMISP_CAMERA_PORT_PRIMARY, LANES,
            ATOMISP_INPUT_FORMAT_RAW_8, atomisp_bayer_order_grbg, flag);
}

static int hm2056_platform_init(struct i2c_client *client)
{
    int ret;

    printk("%s: ++\n", __func__);

    //VPROG1 for 1.8V and 2.8V
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
	pr_err("hm2056 power is not set.\n");
#endif
/*
    vprog1_reg = regulator_get(&client->dev, "vprog1");
    if (IS_ERR(vprog1_reg)) {
        dev_err(&client->dev, "regulator_get failed\n");
        return PTR_ERR(vprog1_reg);
    }
    ret = regulator_set_voltage(vprog1_reg, VPROG1_VAL, VPROG1_VAL);
    if (ret) {
        dev_err(&client->dev, "regulator voltage set failed\n");
        regulator_put(vprog1_reg);
    }
*/
    return ret;
}

static int hm2056_platform_deinit(void)
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
	pr_err("hm2056 power is not set.\n");
#endif
    //regulator_put(vprog1_reg);
    return ret;
}

static struct camera_sensor_platform_data hm2056_sensor_platform_data = {
    .gpio_ctrl	 = hm2056_gpio_ctrl,
    .flisclk_ctrl	 = hm2056_flisclk_ctrl,
    .power_ctrl	 = hm2056_power_ctrl,
    .csi_cfg	 = hm2056_csi_configure,
    //.platform_init   = hm2056_platform_init,
    //.platform_deinit = hm2056_platform_deinit,
};

void *hm2056_platform_data(void *info)
{
    camera_I2C_3_SCL = -1;
    camera_I2C_3_SDA = -1;
    camera_reset = -1;
    camera_power_down = -1;
	camera_vprog1_on = 0;
    return &hm2056_sensor_platform_data;
}

