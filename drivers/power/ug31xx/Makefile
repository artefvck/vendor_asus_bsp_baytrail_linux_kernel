#
#  Copyright (c) 2012, UPI-SEMI, Inc. All Rights Reserved.
#

EXTRA_CFLAGS    += -DBUILD_UG31XX_LINUX_KERNEL -DuG31xx_OS_ANDROID -DUG31XX_USE_DAEMON_AP_FOR_FILE_OP -DCONFIG_ASUS_ENGINEER_MODE -fno-pic

$(obj)/uG31xx_API_Backup.o: $(src)/uG31xx_API_Backup.obj
	cp $< $@
$(obj)/uG31xx_API_Capacity.o: $(src)/uG31xx_API_Capacity.obj
	cp $< $@
$(obj)/uG31xx_API_Measurement.o: $(src)/uG31xx_API_Measurement.obj
	cp $< $@
$(obj)/uG31xx_API.o: $(src)/uG31xx_API.obj
	cp $< $@
$(obj)/uG31xx_API_Otp.o: $(src)/uG31xx_API_Otp.obj
	cp $< $@
$(obj)/uG31xx_API_System.o: $(src)/uG31xx_API_System.obj
	cp $< $@

ug31xx_battery-objs	+= uG31xx_API_Backup.o
ug31xx_battery-objs	+= uG31xx_API_Capacity.o
ug31xx_battery-objs	+= uG31xx_API_Measurement.o
ug31xx_battery-objs	+= uG31xx_API.o
ug31xx_battery-objs	+= uG31xx_API_Otp.o
ug31xx_battery-objs	+= uG31xx_API_System.o

ug31xx_battery-objs	+= uG31xx_API_Platform.o
ug31xx_battery-objs	+= ug31xx_gauge.o

obj-$(CONFIG_UPI_BATTERY)	+= ug31xx_battery.o

