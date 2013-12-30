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


static struct i2c_board_info __initdata gt9271_i2c_device[] = {
#if defined (CONFIG_TOUCHSCREEN_GT9271)	//<asus-toby20131226+>
        {
         	.type          = "Goodix-TS",
		    .addr          = 0x5d,
		    .flags         = 0,
		    .irq           = 158,
		    .platform_data = goodix_info,
        },
#endif	//<asus-toby20131226->
};

#ifdef CONFIG_TOUCHSCREEN_GT9271
static int __init gt9271_i2c_init(void)
{

	return i2c_register_board_info(6, gt9271_i2c_device, ARRAY_SIZE(gt9271_i2c_device));
}
module_init(gt9271_i2c_init);
#endif
