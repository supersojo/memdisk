obj-m := memdisk.o
memdisk-objs := memdisk_core.o

#KDIR := /usr/src/linux-source-4.4.0/linux-source-4.4.0
KDIR := /lib/modules/`uname -r`/build
PWD := $(shell pwd)

default:
	make -C $(KDIR) M=$(PWD) modules
clean:
	rm -rf *.o .*.cmd *.cmd *.ko *.mod.c Module.symvers modules.order .tmp_versions

