/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Jani Nikula <jani.nikula@intel.com>
 *	   Shobhit Kumar <shobhit.kumar@intel.com>
 *
 *
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/i915_drm.h>
#include <linux/slab.h>
#include <video/mipi_display.h>
#include <asm/intel-mid.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_dsi_cmd.h"
#include "dsi_mod_nov_m176.h"
#include <linux/lnw_gpio.h>
#include <linux/acpi_gpio.h>	//pbtest
#include <linux/acpi.h>			//pbtest
#include <linux/gpio.h>			//pbtest
#include <linux/mfd/intel_mid_pmic.h>		//pbtest
static void m176_get_panel_info(int pipe,
				struct drm_connector *connector)
{
	DRM_DEBUG_KMS("\n");
	printk("----pbtest----m176_get_panel_info----\n");
	if (!connector)
		return;

	if (pipe == 0) {
		/* FIXME: the actual width is 94.5, height is 151.2 */
		connector->display_info.width_mm = 94;		//pbtest
		connector->display_info.height_mm = 151;
	}

	return;
}

bool m176_init(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);

	DRM_DEBUG_KMS("\n");
	printk("----pbtest----m176_init----\n");
	intel_dsi->hs = 1;
	intel_dsi->channel = 0;
	intel_dsi->lane_count = 4;
//	intel_dsi->eot_disable = 1;
	intel_dsi->eotp_pkt = 0;
	//intel_dsi->dsi_clock_freq = 513;
	intel_dsi->video_mode_type = DSI_VIDEO_NBURST_SEVENT;
	intel_dsi->pixel_format = VID_MODE_FORMAT_RGB888;
	intel_dsi->port_bits = 0;
	intel_dsi->turn_arnd_val = 0x14;		//pbtest
	intel_dsi->rst_timer_val = 0xff;		//pbtest
	intel_dsi->hs_to_lp_count = 0x1A;		//pbtest
	intel_dsi->lp_byte_clk = 0x03;			//pbtest
	intel_dsi->bw_timer = 0x0;				//pbtest
	intel_dsi->clk_lp_to_hs_count = 0x1E;	//pbtest
	intel_dsi->clk_hs_to_lp_count = 0x0D;	//pbtest
	intel_dsi->video_frmt_cfg_bits = 0x8;
	intel_dsi->dphy_reg = 0x200F370B;		//pbtest

	intel_dsi->backlight_off_delay = 20;
	intel_dsi->backlight_on_delay = 20;		//pbtest
	intel_dsi->send_shutdown = true;
	intel_dsi->shutdown_pkt_delay = 20;

	return true;
}

void m176_create_resources(struct intel_dsi_device *dsi) { }

void m176_dpms(struct intel_dsi_device *dsi, bool enable)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);

	DRM_DEBUG_KMS("\n");
	printk("----pbtest----m176_dpms----\n");
	if (enable) {
		dsi_vc_dcs_write_0(intel_dsi, 0, MIPI_DCS_EXIT_SLEEP_MODE);

		dsi_vc_dcs_write_1(intel_dsi, 0, MIPI_DCS_SET_TEAR_ON, 0x00);

		dsi_vc_dcs_write_0(intel_dsi, 0, MIPI_DCS_SET_DISPLAY_ON);
		dsi_vc_dcs_write_1(intel_dsi, 0, 0x14, 0x55);

	} else {
		dsi_vc_dcs_write_0(intel_dsi, 0, MIPI_DCS_SET_DISPLAY_OFF);
		dsi_vc_dcs_write_0(intel_dsi, 0, MIPI_DCS_ENTER_SLEEP_MODE);
	}
}

int m176_mode_valid(struct intel_dsi_device *dsi,
		   struct drm_display_mode *mode)
{
	return MODE_OK;
}

bool m176_mode_fixup(struct intel_dsi_device *dsi,
		    const struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode) {
	return true;
}
void  m176_disable_panel_power(struct intel_dsi_device *dsi)
{


	printk("----pbtest----m176_disable_panel_power----\n");


    intel_mid_pmic_setb(0x3C,0x24);//GPIOxxxCTLO GPIO1P1 pbtest ??
//    intel_mid_pmic_writeb(0x51,0);//BACKLIGHT_EN
    intel_mid_pmic_writeb(0x52,0);//PANEL_EN

    msleep(500);

}
void m176_panel_reset(struct intel_dsi_device *dsi)
{

//	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
//	struct drm_device *dev = intel_dsi->base.base.dev;
//	struct drm_i915_private *dev_priv = dev->dev_private;

	int err;
	printk("----pbtest----m176_panel_reset----\n");

   err = gpio_request(69, "sd_pwr_en");
			if (err){
				DRM_DEBUG_KMS("----pbtest----m176_panel_reset----10000----\n");
	}

		gpio_direction_output(69, 1);
		usleep_range(10000,15000);
		gpio_set_value(69, 0);
		usleep_range(10000,15000);
		gpio_set_value(69, 1);

    msleep(300);

}



void m176_send_otp_cmds(struct intel_dsi_device *dsi)
{

	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);

//	struct drm_device *dev = intel_dsi->base.base.dev;
//	struct drm_i915_private *dev_priv = dev->dev_private;
//	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);
//	int reset_inno =-1;
	int err;
	DRM_DEBUG_KMS("\n");
	printk("----pbtest----m176_send_otp_cmds----\n");
	//add reset function
	msleep(40);			//pbtest t4

	err = gpio_request(69, "sd_pwr_en1");
			if (err){
				printk("----pbtest----m176_panel_reset----10000----\n");

			}
	gpio_direction_output(69, 1);
	usleep_range(10000,15000);
	gpio_set_value(69, 0);
	usleep_range(10000,15000);
	gpio_set_value(69, 1);
	msleep(20);

	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmds----2----\n");
	intel_dsi->hs = 0 ;
	msleep(30);		//pbtest t5>20

	{
		unsigned char data[] = {0xFF, 0xAA, 0x55, 0xA5, 0x80};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	//========== Internal setting ==========
	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmdstttt----\n");

	{
		unsigned char data[] = {0x6F, 0x11, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}

	{
		unsigned char data[] = {0xF7, 0x20, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}


	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmdstttt--2--\n");
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);

	dsi_vc_dcs_write_1(intel_dsi, 0, 0xF7, 0xA0);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x19);

	dsi_vc_dcs_write_1(intel_dsi, 0, 0xF7, 0x12);

	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x08);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xFA, 0x40);

	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x11);

	dsi_vc_dcs_write_1(intel_dsi, 0, 0xF3, 0x01);

	//========== page0 relative ==========
	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmdstttt--3--\n");
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmdstttt--4--\n");
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC8, 0x80);
	{
		unsigned char data[] = {0xB1, 0x68, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xB6, 0x08);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x02);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xB8, 0x08);
	{
		unsigned char data[] = {0xBB, 0x54, 0x54};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBC, 0x05, 0x05};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC7, 0x01);
	{
		unsigned char data[] = {0xBD, 0x02, 0xB0, 0x0C, 0x0A, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	//========== page1 relative ==========
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB0, 0x05, 0x05};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB1, 0x05, 0x05};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBC, 0x3A, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBD, 0x3E, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xCA, 0x00);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC0, 0x04);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xBE, 0x80);
	{
		unsigned char data[] = {0xB3, 0x19, 0x19};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB4, 0x12, 0x12};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB9, 0x24, 0x24};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBA, 0x14, 0x14};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	//========== page2 relative ==========
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xEE, 0x02);
	{
		unsigned char data[] = {0xEF, 0x09, 0x06, 0x15, 0x18};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xB0, 0x00, 0x00, 0x00, 0x08, 0x00 ,0x17};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xB0, 0x00, 0x25, 0x00, 0x30, 0x00 ,0x45};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xB0, 0x00, 0x56, 0x00, 0x7A};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xB1, 0x00, 0xA3, 0x00, 0xE7, 0x01 ,0x20};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xB1, 0x01, 0x7A, 0x01, 0xC2, 0x01 ,0xC5};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xB1, 0x02, 0x06, 0x02, 0x5F};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xB2, 0x02, 0x92, 0x02, 0xD0, 0x02 ,0xFC};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xB2, 0x03, 0x35, 0x03, 0x5D, 0x03 ,0x8B};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xB2, 0x03, 0xA2, 0x03, 0xBF};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xB3, 0x03, 0xE8, 0x03, 0xFF};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xBC, 0x00, 0x00, 0x00, 0x08, 0x00 ,0x18};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xBC, 0x00, 0x27, 0x00, 0x32, 0x00 ,0x49};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xBC, 0x00, 0x5C, 0x00, 0x83};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xBD, 0x00, 0xAF, 0x00, 0xF3, 0x01 ,0x2A};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xBD, 0x01, 0x84, 0x01, 0xCA, 0x01 ,0xCD};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xBD, 0x02, 0x0E, 0x02, 0x65};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xBE, 0x02, 0x98, 0x02, 0xD4, 0x03 ,0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x06);
	{
		unsigned char data[] = {0xBE, 0x03, 0x37, 0x03, 0x5F, 0x03 ,0x8D};
		dsi_vc_dcs_write(intel_dsi, 0, data, 7);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0x6F, 0x0C);
	{
		unsigned char data[] = {0xBE, 0x03, 0xA4, 0x03, 0xBF};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}
	{
		unsigned char data[] = {0xBF, 0x03, 0xE8, 0x03, 0xFF};
		dsi_vc_dcs_write(intel_dsi, 0, data, 5);
	}

	// PAGE6 : GOUT Mapping, VGLO select
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB0, 0x00, 0x17};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB1, 0x16, 0x15};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB2, 0x14, 0x13};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB3, 0x12, 0x11};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB4, 0x10, 0x2D};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB5, 0x01, 0x08};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB6, 0x09, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB7, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB8, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB9, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBA, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBB, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBC, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBD, 0x31, 0x09};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBE, 0x08, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBF, 0x2D, 0x10};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC0, 0x11, 0x12};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC1, 0x13, 0x14};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC2, 0x15, 0x16};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC3, 0x17, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xE5, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC4, 0x00, 0x17};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC5, 0x16, 0x15};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC6, 0x14, 0x13};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC7, 0x12, 0x11};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC8, 0x10, 0x2D};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC9, 0x01, 0x08};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCA, 0x09, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCB, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCC, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCD, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCE, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xCF, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD0, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD1, 0x31, 0x09};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD2, 0x08, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD3, 0x2D, 0x10};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD4, 0x11, 0x12};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD5, 0x13, 0x14};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD6, 0x15, 0x16};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD7, 0x17, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xE6, 0x31, 0x31};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD8, 0x00, 0x00, 0x00, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xD9, 0x00, 0x00, 0x00, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xE7, 0x00);

	// PAGE3 :
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB0, 0x20, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB1, 0x20, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB2, 0x05, 0x00, 0x42, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB6, 0x05, 0x00, 0x42, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xBA, 0x53, 0x00, 0x42, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xBB, 0x53, 0x00, 0x42, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC4, 0x40);

	// PAGE5 :
	{
		unsigned char data[] = {0xF0, 0x55, 0xAA, 0x52, 0x08, 0x05};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB0, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xB8, 0x00);
	{
		unsigned char data[] = {0xBD, 0x03, 0x01, 0x01, 0x00, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xB1, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB9, 0x00, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB2, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBA, 0x00, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB3, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBB, 0x0A, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB4, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB5, 0x17, 0x06};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB6, 0x14, 0x03};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xB7, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xBC, 0x02, 0x01};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC0, 0x05);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xC4, 0xA5);
	{
		unsigned char data[] = {0xC8, 0x03, 0x30};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xC9, 0x03, 0x51};
		dsi_vc_dcs_write(intel_dsi, 0, data, 3);
	}
	{
		unsigned char data[] = {0xD1, 0x00, 0x05, 0x03, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	{
		unsigned char data[] = {0xD2, 0x00, 0x05, 0x09, 0x00, 0x00};
		dsi_vc_dcs_write(intel_dsi, 0, data, 6);
	}
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xE5, 0x02);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xE6, 0x02);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xE7, 0x02);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xE9, 0x02);
	dsi_vc_dcs_write_1(intel_dsi, 0, 0xED, 0x33);
	DRM_DEBUG_KMS("----pbtest----m176_send_otp_cmdstttt--5--\n");
	msleep(120);
	//dsi_vc_dcs_write_0(intel_dsi, 0, 0x11);			//pbtest ??
	//msleep(120);
	//dsi_vc_dcs_write_0(intel_dsi, 0, 0x29);			//pbtest ??


}

void m176_enable(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);

	DRM_DEBUG_KMS("\n");

	dsi_vc_dcs_write_0(intel_dsi, 0, 0x11);
	msleep(120);
	dsi_vc_dcs_write_0(intel_dsi, 0, 0x29);
}

void m176_disable(struct intel_dsi_device *dsi)
{
	struct intel_dsi *intel_dsi = container_of(dsi, struct intel_dsi, dev);

	DRM_DEBUG_KMS("\n");

	dsi_vc_dcs_write_0(intel_dsi, 0, 0x28);
	msleep(20);
	dsi_vc_dcs_write_0(intel_dsi, 0, 0x10);
	msleep(80);
}

enum drm_connector_status m176_detect(struct intel_dsi_device *dsi)
{
	printk("----pbtest----m176_detect----\n");
	return connector_status_connected;
}

bool m176_get_hw_state(struct intel_dsi_device *dev)
{
	return true;
}

struct drm_display_mode *m176_get_modes(struct intel_dsi_device *dsi)
{
	struct drm_display_mode *mode;
	printk("----pbtest----m176_get_modes----\n");
	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	/* beta = 00, alpha = 45 */
	/* from renesas spec alpha + beta <= 45 */
	mode->hdisplay = 800;			//pbtest
	mode->hsync_start = 840;		//pbtest	HFP = 40
	mode->hsync_end = 844;			//pbtest	HW = 4
	mode->htotal = 884;				//pbtest	HBP = 40


	/* Added more vblank so more time for frame update */
	mode->vdisplay = 1280;			//pbtest
	mode->vsync_start = 1290;		//pbtest	VFP	= 10
	mode->vsync_end = 1294;			//pbtest	VW	= 4
	mode->vtotal = 1306;			//pbtest	VBP	= 12

	mode->vrefresh = 60;

	mode->clock =  (mode->vrefresh * mode->vtotal *
		mode->htotal) / 1000;

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	mode->type |= DRM_MODE_TYPE_PREFERRED;

	return mode;
}

void m176_dump_regs(struct intel_dsi_device *dsi) { }

void m176_destroy(struct intel_dsi_device *dsi) { }

/* Callbacks. We might not need them all. */
struct intel_dsi_dev_ops nov_m176_dsi_display_ops = {
	.init = m176_init,
	.get_info = m176_get_panel_info,
	.create_resources = m176_create_resources,
	.dpms = m176_dpms,
	.mode_valid = m176_mode_valid,
	.mode_fixup = m176_mode_fixup,
	.panel_reset = m176_panel_reset,
	.disable_panel_power = m176_disable_panel_power,
	.send_otp_cmds = m176_send_otp_cmds,
	.enable = m176_enable,
	.disable = m176_disable,
	.detect = m176_detect,
	.get_hw_state = m176_get_hw_state,
	.get_modes = m176_get_modes,
	.destroy = m176_destroy,
	.dump_regs = m176_dump_regs,
};
