obj-m += dma_m2m.o

KERNEL_DIR ?= /mnt/caviar_green/code/rpi/linux

all:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) modules
clean:
	make -C $(KERNEL_DIR) \
		ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
		SUBDIRS=$(PWD) clean

deploy : dma_m2m.ko
	scp $^ root@raspberrypi.local:
