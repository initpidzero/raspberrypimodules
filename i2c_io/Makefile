obj-m += i2c_io_expander.o

KERNEL_DIR ?= /mnt/caviar_green/code/rpi/linux

all: 
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) clean 
	
deploy : i2c_io_expander.ko 
	scp $^ root@raspberrypi:
