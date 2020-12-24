/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/time.h>

static int num = 10;
static struct timeval start_time;

/* S_IRUG0: everyone can reada the sysfs entry */

module_param(num, int, S_IRUGO);

static void say_hello(void)
{
	int i;
	for (i = 0; i < num; i++) {
		pr_info("hello = %d.\n", i);
	}
}

static int __init hello_init(void)
{
	do_gettimeofday(&start_time);
	pr_info("Loading first\n");
	say_hello();

	return 0;
}

static void __exit hello_exit(void)
{
	
	struct timeval end_time;
	do_gettimeofday(&start_time);
	pr_info("unloading module after %ld seconds\n",
		end_time.tv_sec - start_time.tv_sec);
	say_hello();
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("HELLO WORLD MODULE");
