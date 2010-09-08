KVER = $(shell uname -r)
KERNELDIR = /lib/modules/$(KVER)/build

obj-$(CONFIG_ACX_MAC80211) += acx-mac80211.o

acx-obj-$(CONFIG_ACX_MAC80211_PCI) += pci.o
acx-obj-$(CONFIG_ACX_MAC80211_USB) += usb.o
acx-obj-$(CONFIG_ACX_MAC80211_CS)  += cs.o acx_generic.o

acx-mac80211-objs := common.o $(acx-obj-y)

ifeq ($(KERNELRELEASE),)

all: $(obj-m)

$(obj-m): $(module-files)
	$(MAKE) -C $(KERNELDIR) SUBDIRS=`pwd` modules

clean:
	rm -rf *.o *.ko .*.cmd .*.flags *.mod.c Module.symvers .tmp_versions/

.PHONY: clean

endif
