/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#define MY_MAJOR_NUM 42

static struct cdev my_dev;

static int my_dev_ioctl(struct file *file, unsigned int command, unsigned long arg)
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
	
	/* get a device identifier  */
	dev_t dev = MKDEV(MY_MAJOR_NUM, 0);
	pr_info("hello there! \n");

	/* allocate device numbers */
	ret = register_chrdev_region(dev, 1, "my_char_dev");
	if (ret < 0) {
		pr_info("Unable to register chrdev\n");
		return ret;
	}

	/* initialise cdev structure and add it to kernel space */
	cdev_init(&my_dev, &my_fops);
	ret = cdev_add(&my_dev, dev, 1);
	if (ret < 0) {
		unregister_chrdev_region(dev, 1);
		pr_info("Unable to add cdev\n");
		return ret;
	}
	return 0;
}

static void __exit hello_exit(void)
{
	dev_t dev = MKDEV(MY_MAJOR_NUM, 0);
	unregister_chrdev_region(dev, 1);
	cdev_del(&my_dev);
	pr_info("General Kenobi! \n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("HELLO WORLD MODULE");
