/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>

static int num = 5;

/* S_IRUG0: everyone can reada the sysfs entry */

module_param(num, int, S_IRUGO);

static int __init hello_init(void)
{
	pr_info("param = %d.\n", num);
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("hello world exit \n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("HELLO WORLD MODULE");
