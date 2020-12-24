/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/fs.h> /* file operations fops */

/* platform_driver_register and platform_set_drvdata() */
#include <linux/platform_device.h>
#include <linux/io.h> /* devm_ioremap(), iowrite32() */
#include <linux/of.h> /* of_property_read_string() */
#include <linux/uaccess.h> /* copy_from/to_user() */
#include <linux/miscdevice.h> /* misc register */
#include <linux/types.h> 

#define BCM2710_PERI_BASE		0x3F000000
#define GPIO_BASE 			(BCM2710_PERI_BASE + 0x200000) /* GPIO Controller */

#define GPIO_27 			27
#define GPIO_22 			22
#define GPIO_26 			26

/* set individual LEDs */
#define GPIO_27_INDEX 	1 << (GPIO_27 % 32)  
#define GPIO_22_INDEX 	1 << (GPIO_22 % 32)  
#define GPIO_26_INDEX 	1 << (GPIO_26 % 32)  

/* output function */
/* each register seems to have 30 bits for FSEL, 3 bit per pin, 10 pins per register */
#define GPIO_27_FUNC 	1 << ((GPIO_27 % 10) * 3)  
#define GPIO_22_FUNC 	1 << ((GPIO_22 % 10) * 3)  
#define GPIO_26_FUNC 	1 << ((GPIO_26 % 10) * 3)  

/* masks for configuring differt registers */
#define FSEL_27_MASK 	0b111 << ((GPIO_27 % 10) * 3) /* bit 21 for pin 27, red */ 
#define FSEL_22_MASK 	0b111 << ((GPIO_22 % 10) * 3) /* bit 21 for pin 22, green */ 
#define FSEL_26_MASK 	0b111 << ((GPIO_26 % 10) * 3) /* bit 18 for pin 26, blue */ 

#define GPIO_SET_FUNCTION_LEDS	(GPIO_27_FUNC | GPIO_22_FUNC | GPIO_26_FUNC)
#define GPIO_SET_ALL_LEDS	(GPIO_27_INDEX | GPIO_22_INDEX | GPIO_26_INDEX)
#define GPIO_MASK_ALL_LEDS 	(FSEL_27_MASK | FSEL_22_MASK | FSEL_26_MASK) 

#define GPFSEL2				GPIO_BASE + 0x08
#define GPSET0				GPIO_BASE + 0x1c
#define GPCLR0				GPIO_BASE + 0x28

/* virutal address for physical address */
static void __iomem *GPFSEL2_V;
static void __iomem *GPSET0_V;
static void __iomem *GPCLR0_V;

/* private data structure */
struct led_dev {
	struct miscdevice led_misc_device; /* char device for each led */
	u32 led_mask;  /* mask for each led device */
	const char *led_name; /* labels for devices */
	char led_value[8];
};

/* send on/off value from your terminal to led */
static ssize_t led_write(struct file *file, const char __user *buff,
		size_t count, loff_t *ppos)
{
	const char *on = "on";
	const char *off = "off";
	struct led_dev *led_device;	
	int ret;
	
	pr_info("led_write() \n");
	led_device = container_of(file->private_data, struct led_dev, led_misc_device);

	/* so echo adds \n to the value so values are
 	* "on\n" and "off\n"
 	* count is 3 for on and 4 for off */

	ret = copy_from_user(led_device->led_value, buff, count);
	if (ret) {
		pr_info("Bad copied value \n");
		return -EFAULT;
	}

	led_device->led_value[count - 1] = '\0';

	/* check for on and off */
	if (!strncmp(led_device->led_value, on, strlen(on))) {
		iowrite32(led_device->led_mask, GPSET0_V);
	} else if (!strncmp(led_device->led_value, off, strlen(off))) {
		iowrite32(led_device->led_mask, GPCLR0_V);
	} else {
		pr_info("invalid input \n");
		return -EINVAL;
	}
	
	return count;
}

/* read each LED status on/off by using cat 
 * led_read is entered until *ppos > 0
 * twice in this function */
static ssize_t led_read(struct file *file, char __user *buff,
		size_t count, loff_t *ppos)
{
	int len;
	struct led_dev *led_device;
	int ret;

	pr_info("led_read() \n");

	led_device = container_of(file->private_data, struct led_dev, led_misc_device);
	
	if (*ppos == 0) {
		len = strlen(led_device->led_value);
		pr_info("size of user buffer is %d \n", len);
		led_device->led_value[len] = '\n'; /* requires a new line for cat */
		ret = copy_to_user(buff, led_device->led_value, len + 1);
		if (ret) {
			pr_info("Bad copy value to user \n");
			return -EFAULT;
		}
		*ppos += 1;
		return sizeof(led_device->led_value);	
	}	
	return 0; /* exit and don't call this function as soon as ppos is 1 */
}

const struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.read = led_read,
	.write = led_write,
};

static int __init led_probe(struct platform_device *pdev)
{
	/* whole private structure shenanighans. */
	struct led_dev *led_device;
	int ret;

	/* all leds are off */
	char led_value[8] = "off\n";

	pr_info("hello there! \n");

	led_device = devm_kzalloc(&pdev->dev, sizeof(struct led_dev), GFP_KERNEL); 

	/* so read each node "label" property in probe call
 	* called three times once for each compabtible "arrow,RGBleds"
 	*/ 
	of_property_read_string(pdev->dev.of_node, "label", &led_device->led_name);

	/* allocate device numbers */
	led_device->led_misc_device.minor = MISC_DYNAMIC_MINOR;
	led_device->led_misc_device.name = led_device->led_name;
	led_device->led_misc_device.fops = &led_fops;
	
	/* assign a different mask for each LED */
	if (!strncmp(led_device->led_name, "ledred", strlen("ledred"))) {
		led_device->led_mask = GPIO_27_INDEX;
	} else if (!strncmp(led_device->led_name, "ledgreen", strlen("ledgreen"))) {
		led_device->led_mask = GPIO_22_INDEX;
	} else if (!strncmp(led_device->led_name, "ledblue", strlen("ledblue"))) {
		led_device->led_mask = GPIO_26_INDEX;
	} else {
		pr_info("invalid device tree value \n");
		return -EINVAL;
	}

	/* initialise each led status to off */
	memcpy(led_device->led_value, led_value, sizeof(led_value));
	
	/* register misc device led char devices */
	ret = misc_register(&led_device->led_misc_device);
	if (ret) return ret;

	/* attach led_device structure to pdev */
	platform_set_drvdata(pdev, led_device);
	
	return 0;
}

static int __exit led_remove(struct platform_device *pdev)
{
	struct led_dev *led_device = platform_get_drvdata(pdev);
	misc_deregister(&led_device->led_misc_device);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,RGBleds"},
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
	u32 gpfsel_read, gpfsel_write;
	int ret = platform_driver_register(&led_platform_driver);

	pr_info("usual init function \n");
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	/* get virtual addresses */
	GPFSEL2_V = ioremap(GPFSEL2, sizeof(u32));
	GPSET0_V = ioremap(GPSET0, sizeof(u32));
	GPCLR0_V = ioremap(GPCLR0, sizeof(u32));
	
	gpfsel_read = ioread32(GPFSEL2_V); /* so what values does GP func SEL 2 register have */
	/* So this will set 0 to all but LED bits first and then
 	* set 1 to first bit of all leds i.e. 6, 18 21
 	*  	
 	*/
	gpfsel_write = (gpfsel_read & ~GPIO_MASK_ALL_LEDS) | (GPIO_SET_FUNCTION_LEDS & GPIO_MASK_ALL_LEDS);

	iowrite32(gpfsel_write, GPFSEL2_V); /* set leds to output */
	iowrite32(GPIO_SET_ALL_LEDS, GPCLR0_V); /* clear all the leds. */

	return 0;
}

static void __exit led_exit(void)
{
	pr_info("usual exit function \n");
	
	/* clear all masks */
	iowrite32(GPIO_SET_ALL_LEDS, GPCLR0_V); /* clear all  leds. */

	iounmap(GPFSEL2_V);
	iounmap(GPSET0_V);
	iounmap(GPCLR0_V);
	platform_driver_unregister(&led_platform_driver);
	
}
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
