obj-m += led_module.o

KERNEL_DIR ?= /mnt/caviar_green/code/rpi/linux

all: 
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) clean 
	
deploy : led_module.ko 
	scp $^ root@raspberrypi:
