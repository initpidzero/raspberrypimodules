obj-m += i2c_class.o

KERNEL_DIR ?= /mnt/caviar_green/code/rpi/linux

all: 
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) clean 
	
deploy : i2c_class.ko 
	scp $^ root@raspberrypi:
