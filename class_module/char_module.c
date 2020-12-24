/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>

#define DEVICE_NAME "mydev"
#define CLASS_NAME "myclass"

static struct cdev my_dev;
static struct class *hello_class;
dev_t dev;

static long my_dev_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
	pr_info("ioctl() command = %d arg = %ld\n", command, arg);
	return 0;
}

static int my_dev_close(struct inode *inode, struct file *file)
{
	pr_info("close() call happened \n");
	return 0;
}

static int my_dev_open(struct inode *inode, struct file *file)
{
	pr_info("open() call happened \n");
	return 0;
}

const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = my_dev_open,
	.release = my_dev_close,
	.unlocked_ioctl = my_dev_ioctl
};

static int __init hello_init(void)
{
	int ret;
	dev_t dev_no;
	struct device *hello_device;
	int major;
	
	pr_info("hello there! \n");

	/* allocate device numbers */
	ret = alloc_chrdev_region(&dev_no, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_info("Unable to allocate region \n");
		return ret;
	}
	/* get a device identifier  */
	major = MAJOR(dev_no);
	dev = MKDEV(major, 0);

	pr_info("Allocated major number = %d\n", major);

	/* initialise cdev structure and add it to kernel space */
	cdev_init(&my_dev, &my_fops);
	ret = cdev_add(&my_dev, dev, 1);
	if (ret < 0) {
		unregister_chrdev_region(dev, 1);
		pr_info("Unable to add cdev\n");
		return ret;
	}

	hello_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(hello_class)) {
		unregister_chrdev_region(dev, 1);
		cdev_del(&my_dev);
		pr_info("Failed to creat class\n");
		return PTR_ERR(hello_class);
	
	}

	pr_info("Class created \n");
	hello_device = device_create(hello_class, NULL, dev, NULL, DEVICE_NAME);
	if (IS_ERR(hello_device)) {
		class_destroy(hello_class);
		cdev_del(&my_dev);
		unregister_chrdev_region(dev, 1);
		pr_info("Failed to create device\n");
		return PTR_ERR(hello_device);
	
	}
	pr_info("Device created \n");
	return 0;
}

static void __exit hello_exit(void)
{
	device_destroy(hello_class, dev);
	class_destroy(hello_class);
	cdev_del(&my_dev);
	unregister_chrdev_region(dev, 1);
	pr_info("General Kenobi! \n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("HELLO WORLD MODULE");
