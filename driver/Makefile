NAME := fpga

# Build variables
KVER := $(shell uname -r)
KDIR := /lib/modules/`uname -r`/build
RHR := /etc/redhat-release
#DDIR := ../src/sw/driver

obj-m += $(NAME).o
$(NAME)-y := fpga_driver.o
ccflags-y := -std=gnu99 -fms-extensions

default: $(NAME).ko

$(NAME).ko:
	make -C $(KDIR) SUBDIRS=`pwd` modules

load: build
	insmod $(NAME).ko
	
unload:
	rmmod $(NAME)

clean:
	rm -Rf *.ko *.cmd *.o .*.cmd Module.symvers Module.markers modules.order *.mod.c .tmp_versions

setup:
	@if [ -f "$(RHR)" ]; then yum install kernel-devel-`uname -r`;\
	else \
	dpkg-query -s linux-headers-`uname -r` 1> /dev/null;\
	if [ $$? -ne 0 ]; then \
		apt-get install linux-headers-`uname -r`;\
	fi; \
	fi

install: $(NAME).ko
	mkdir -p /lib/modules/$(KVER)/kernel/drivers/fpga
	cp $(NAME).ko /lib/modules/$(KVER)/kernel/drivers/fpga/
	if [ -f "$(RHR)" ]; then\
		printf "%b\n" "#!/bin/sh\nexec /sbin/modprobe $(NAME) >/dev/null 2>&1" > /etc/sysconfig/modules/fpga.modules;\
		chmod 755 /etc/sysconfig/modules/fpga.modules;\
	else\
		if ! grep -Fxq "$(NAME)" /etc/modules; then echo "$(NAME)" >> /etc/modules; fi;\
	fi
	printf "%b\n" "KERNEL==\"$(NAME)\", MODE=\"777\", GROUP=\"root\"" > /etc/udev/rules.d/99-$(NAME).rules
	cp ../fpga_common.h /usr/include/
	depmod

uninstall: 
	rm -rf /lib/modules/$(KVER)/kernel/drivers/fpga
	rm -f /etc/udev/rules.d/99-$(NAME).rules
	rm -f /usr/include/fpga_driver.h
	if [ -f "$(RHR)" ]; then rm -f /etc/sysconfig/modules/fpga.modules;\
	else cp /etc/modules ./etc.modules.bak; sed -i '/$(NAME)/d' /etc/modules; fi
	depmod

rebuild: clean default
	sudo make install
	sudo modprobe -r fpga
	sudo modprobe fpga