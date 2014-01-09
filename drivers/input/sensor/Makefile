#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.
# sensor drivers

ifeq ($(CONFIG_INPUT_SENSOR_KXTJ2_POLLED_MODE),y)
CFLAGS_kxtj2.o += -D CONFIG_INPUT_SENSOR_KXTJ2_POLLED_MODE
endif

obj-$(CONFIG_INPUT_SENSOR_KXTJ2)     += kxtj2.o
obj-$(CONFIG_INPUT_SENSOR_AK09911)   += akm09911.o
obj-$(CONFIG_INPUT_SENSOR_HALLSENSOR)       += hallsensor.o
