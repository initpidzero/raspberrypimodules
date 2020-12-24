/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/init.h>
/* platform_driver_register and platform_set_drvdata() */
#include <linux/platform_device.h>
#include <linux/io.h> /* devm_ioremap(), iowrite32() */
#include <linux/of.h> /* of_property_read_string() */
#include <linux/device.h> /* of_property_read_string() */
#include <linux/timer.h> /* jiffies */
#include <linux/miscdevice.h> /* misc register */

#define BCM2710_PERI_BASE               0x3F000000
#define GPIO_BASE                       (BCM2710_PERI_BASE + 0x200000) /* GPIO Controller */
/* gpioregs */
struct gpio_regs {
	uint32_t GPSEL[6];
	uint32_t res1;
	uint32_t GPSET[2];
	uint32_t res2;
	uint32_t GPCLR[2];
	
};

static struct gpio_regs *gpio_regs_s;

/* set gpio function */
static void set_gpio_fn(int gpio, int fn_code)
{
	int reg_index = gpio / 10; /* GPSEL 1 to 6 */ 
	int bit = (gpio % 10) * 3; /* 27 -> 21 22 -> 6 26 -> 18 */
	unsigned old = gpio_regs_s->GPSEL[reg_index];
	unsigned mask = 0b111 << bit;
	pr_info("changing function of gpio%d from %x to %x\n", gpio, 
			(old >> bit) & 0b111, fn_code);
	gpio_regs_s->GPSEL[reg_index] = (old & ~mask) | ((fn_code << bit) & mask);
}

static void set_gpio_output(int gpio, bool output)
{
	pr_info("output =  %x\n", output); 
	if (output)
		gpio_regs_s->GPSET[gpio / 32] = (1 << (gpio % 32)); 
	else
		gpio_regs_s->GPCLR[gpio / 32] = (1 << (gpio % 32)); 
}

static struct timer_list blink_timer;
static int blink_period = 1000;
static const int led_gpio_pin = 27;

static void blink_handler(struct timer_list *blink)
{
	static bool on = false;
	on = !on;
	set_gpio_output(led_gpio_pin, on);
	mod_timer(&blink_timer, jiffies + msecs_to_jiffies(blink_period));
	pr_info("blink_handler\n"); 
}

static ssize_t set_period(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	long period_val = 0;
	if (kstrtol(buf, 10, &period_val) < 0)
		return -EINVAL;
	if (period_val < 10)
		return -EINVAL;
	blink_period = period_val; /* period have to be greater than 10 */
	pr_info("blink_period %x\n", period_val); 

	return count;
}
static DEVICE_ATTR(period, S_IWUSR, NULL, set_period);

static struct miscdevice led_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "timerled"
};

static int __init led_probe(struct platform_device *pdev)
{
	int ret;
	int result;
	struct device *dev = &pdev->dev;
	
	pr_info("hello there! \n");

	gpio_regs_s = (struct gpio_regs *)devm_ioremap(dev, GPIO_BASE,
			sizeof(struct gpio_regs)); /* remap the address to io readable location, physical to virtual? */ 
	
	set_gpio_fn(led_gpio_pin, 0b001); /* set the pin to output */
	/*initialise a timer */
	timer_setup(&blink_timer, blink_handler, 0);
	
	/* schedule a timer in future */
	result = mod_timer(&blink_timer, jiffies + msecs_to_jiffies(blink_period));

	ret = device_create_file(dev, &dev_attr_period); /* sysfs entry for period */
	if ( ret != 0) {
		dev_err(dev, "couldn't create sysfs entry \n");
		return ret;
	}

	ret = misc_register(&led_miscdevice);
	if ( ret != 0) {
		dev_err(dev, "couldn't create sysfs entry \n");
		return ret;
	}
	dev_info(dev, "Finished probing \n");
	return 0;
}

static int __exit led_remove(struct platform_device *pdev)
{
	misc_deregister(&led_miscdevice);
	device_remove_file(&pdev->dev, &dev_attr_period);
	set_gpio_fn(led_gpio_pin, 0);
	del_timer(&blink_timer);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,timerled"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver led_platform_driver = {
	.probe = led_probe,
	.remove = led_remove,
	.driver = {
		.name = "RGBleds",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int led_init(void)
{
	int ret = platform_driver_register(&led_platform_driver);
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}
	pr_info("usual init function \n");
	return 0;
}

static void __exit led_exit(void)
{
	pr_info("usual exit function \n");
	platform_driver_unregister(&led_platform_driver);
	
}
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("TIMER MODULE");
