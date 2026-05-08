# SPDX-License-Identifier: GPL-2.0
#
# Dual-purpose Makefile.
#
# 1. Inside a kernel tree (drivers/video/fbdev/cberryfb/) this is consumed
#    as a normal Kbuild fragment via CONFIG_FB_CBERRY.
#
# 2. Standalone on a Raspberry Pi running raspberrypi-kernel-headers
#    (`make`, `make install`, `make clean`).
#
# The accompanying device-tree overlay cberry.dts is built with `dtc`.

ifneq ($(KERNELRELEASE),)
# -- Called from kernel build system --
# In-tree build (CONFIG_FB_CBERRY set by Kconfig); standalone build
# (no CONFIG_FB_CBERRY) falls back to obj-m so `make M=...` works.
ifdef CONFIG_FB_CBERRY
obj-$(CONFIG_FB_CBERRY) += cberryfb.o
else
obj-m += cberryfb.o
endif

else
# -- Standalone build on a Pi (or any system with kernel headers) --

KVER  ?= $(shell uname -r)
KDIR  ?= /lib/modules/$(KVER)/build
PWD   := $(shell pwd)
DTC   ?= dtc

# Where Bookworm / Bullseye keep boot files. Override on older systems
# with `BOOT=/boot make install`.
BOOT  ?= $(shell [ -d /boot/firmware ] && echo /boot/firmware || echo /boot)

obj-m := cberryfb.o

.PHONY: all module overlay clean install uninstall

all: module overlay

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

overlay: cberry.dtbo

cberry.dtbo: cberry.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f cberry.dtbo

install: all
	install -d $(DESTDIR)/lib/modules/$(KVER)/extra
	install -m 0644 cberryfb.ko $(DESTDIR)/lib/modules/$(KVER)/extra/
	install -d $(DESTDIR)$(BOOT)/overlays
	install -m 0644 cberry.dtbo $(DESTDIR)$(BOOT)/overlays/
	depmod -a $(KVER)
	@echo
	@echo "Installed. To enable on next boot, add to $(BOOT)/config.txt:"
	@echo "    dtparam=spi=on"
	@echo "    dtoverlay=cberry"

uninstall:
	rm -f $(DESTDIR)/lib/modules/$(KVER)/extra/cberryfb.ko
	rm -f $(DESTDIR)$(BOOT)/overlays/cberry.dtbo
	depmod -a $(KVER) || true

endif
