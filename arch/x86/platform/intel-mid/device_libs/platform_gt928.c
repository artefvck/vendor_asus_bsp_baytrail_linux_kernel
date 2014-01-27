#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/lnw_gpio.h>
#include <linux/gpio.h>
#include <linux/power/smb347-charger.h>
#include <asm/intel-mid.h>
#include <asm/intel_crystalcove_pwrsrc.h>

int goodix_info[2] = {
	158,
	60
};


static struct i2c_board_info __initdata gt928_i2c_device[] = {
#if defined (CONFIG_TOUCHSCREEN_ME181_GT928)	//add by red_zhang@asus.com
        {
         	.type          = "Goodix-TS",
		    .addr          = 0x5d,
		    .flags         = 0,
		    .irq           = 158,
		    .platform_data = goodix_info,
        },
#endif	//add by red_zhang@asus.com
};

#ifdef CONFIG_TOUCHSCREEN_ME181_GT928
static int __init gt928_i2c_init(void)
{

	return i2c_register_board_info(6, gt928_i2c_device, ARRAY_SIZE(gt928_i2c_device));
}
module_init(gt928_i2c_init);
#endif
