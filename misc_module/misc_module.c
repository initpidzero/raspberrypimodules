/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

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

static struct miscdevice hello_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mydev",
	.fops = &my_fops,
};
static int __init hello_init(void)
{
	int ret;
	
	pr_info("hello there! \n");

	/* allocate device numbers */
	ret = misc_register(&hello_misc);
	if (ret < 0) {
		pr_info("Unable to allocate misc device \n");
		return ret;
	}

	pr_info("Device created with mino number = %d\n", hello_misc.minor);
	return 0;
}

static void __exit hello_exit(void)
{
	misc_deregister(&hello_misc);
	pr_info("General Kenobi! \n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("HELLO WORLD MODULE");
