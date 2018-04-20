
obj-m += driver.o


KDIR = /lib/modules/4.4.124+/build
#KDIR = /lib/modules/4.16.0-rc5+/build

all:
	make -C $(KDIR)  M=$(shell pwd) modules

clean:
	make -C $(KDIR)  M=$(shell pwd) clean
