#
# Makefile for the kernel mmc device drivers.
#

subdir-ccflags-$(CONFIG_MMC_DEBUG) := -DDEBUG

obj-$(CONFIG_FACTORY_ITEMS)	+= fac_int.o
#obj-y	+= fac_int.o

obj-$(CONFIG_MMC)		+= core/
obj-$(CONFIG_MMC)		+= card/
obj-$(subst m,y,$(CONFIG_MMC))	+= host/
