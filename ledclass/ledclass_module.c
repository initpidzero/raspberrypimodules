/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/fs.h> /* file operations fops */

/* platform_driver_register platform_get_resources and platform_set_drvdata() */
#include <linux/platform_device.h>
#include <linux/io.h> /* devm_ioremap(), iowrite32() */
#include <linux/of.h> /* of_property_read_string() */
#include <linux/uaccess.h> /* copy_from/to_user() */
#include <linux/types.h> 
#include <linux/leds.h>

#define GPIO_27 			27
#define GPIO_22 			22
#define GPIO_26 			26

#define GPFSEL2_offset			0x08
#define GPSET0_offset			0x1c
#define GPCLR0_offset			0x28

/* set individual LEDs */
#define GPIO_27_INDEX 			1 << (GPIO_27 % 32)  
#define GPIO_22_INDEX 			1 << (GPIO_22 % 32)  
#define GPIO_26_INDEX 			1 << (GPIO_26 % 32)  

/* output function */
/* each register seems to have 30 bits for FSEL, 3 bit per pin, 10 pins per register */
#define GPIO_27_FUNC 			1 << ((GPIO_27 % 10) * 3)  
#define GPIO_22_FUNC 			1 << ((GPIO_22 % 10) * 3)  
#define GPIO_26_FUNC 			1 << ((GPIO_26 % 10) * 3)  

/* masks for configuring differt registers */
#define FSEL_27_MASK 			0b111 << ((GPIO_27 % 10) * 3) /* bit 21 for pin 27, red */ 
#define FSEL_22_MASK 			0b111 << ((GPIO_22 % 10) * 3) /* bit 21 for pin 22, green */ 
#define FSEL_26_MASK 			0b111 << ((GPIO_26 % 10) * 3) /* bit 18 for pin 26, blue */ 

#define GPIO_SET_FUNCTION_LEDS		(GPIO_27_FUNC | GPIO_22_FUNC | GPIO_26_FUNC)
#define GPIO_SET_ALL_LEDS		(GPIO_27_INDEX | GPIO_22_INDEX | GPIO_26_INDEX)
#define GPIO_MASK_ALL_LEDS 		(FSEL_27_MASK | FSEL_22_MASK | FSEL_26_MASK) 


/* private data structure */
struct led_dev {
	u32 led_mask;  /* mask for each led device */
	void __iomem *base; /* GPIO base register address */
	struct led_classdev cdev; /* some device specific settings */
};


static void  led_control(struct led_classdev *led_cdev, enum led_brightness b)
{
	struct led_dev *led_device = container_of(led_cdev, struct led_dev, cdev);

	iowrite32(GPIO_MASK_ALL_LEDS, led_device->base + GPCLR0_offset);

	if (b != LED_OFF) {
		/* write mask in set0 to switch on the LED */
		iowrite32(led_device->led_mask, led_device->base + GPSET0_offset);
	} else {
		iowrite32(led_device->led_mask, led_device->base + GPCLR0_offset);
	}
}

static int __init ledclass_probe(struct platform_device *pdev)
{
	void __iomem *g_ioremap_addr;
	struct device_node *child;
	int ret;
	struct resource *r;
	int count;
	struct device *dev = &pdev->dev;
	u32 gpsel_read, gpsel_write;

	pr_info("hello there! \n");
	/* get memory resource form device tree */
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "IORESOURCE_MEM, 0 doesn't exist\n");
		return -EINVAL;
	}
	
	dev_info(dev, "r->start = 0x%08lx\n", (unsigned long)r->start);
	dev_info(dev, "r->end = 0x%08lx\n", (unsigned long)r->end);

	g_ioremap_addr = devm_ioremap(dev, r->start, resource_size(r));
	if (!g_ioremap_addr) {
		dev_err(dev, "ioremap failed \n");
		return -ENOMEM;
	}
	
	count = of_get_child_count(dev->of_node);
	if (!count)
		return -EINVAL;

	dev_info(dev, "number of nodes %d\n", count);

	gpsel_read = ioread32(g_ioremap_addr + GPFSEL2_offset); /* read value of select2 register */
	gpsel_write = (gpsel_read & ~GPIO_MASK_ALL_LEDS) |
		(GPIO_SET_FUNCTION_LEDS & GPIO_MASK_ALL_LEDS);

	/* Enable all leds and set direction to OUTPUT */
	iowrite32(gpsel_write, g_ioremap_addr + GPFSEL2_offset); /* set dir leds to output */
	iowrite32(GPIO_SET_ALL_LEDS, g_ioremap_addr + GPCLR0_offset); /* clear all leds */


	for_each_child_of_node(dev->of_node, child) {
		struct led_dev *led_device;
		struct led_classdev *cdev;
		
		/* get a private data structure for each node */
		led_device = devm_kzalloc(dev, sizeof(struct led_dev), GFP_KERNEL); 
		if (!led_device) 
			return -ENOMEM;

		cdev = &led_device->cdev;
		led_device->base = g_ioremap_addr;

		/* get mask for each node */
		of_property_read_string(child, "label", &cdev->name);
		pr_info("print this = %s\n", cdev->name);
	
		/* assign a different mask for each LED */
		if (!strncmp(cdev->name, "red", strlen("red"))) {
			led_device->led_mask = GPIO_27_INDEX;
			led_device->cdev.default_trigger = "heartbeat";

		} else if (!strncmp(cdev->name, "green", strlen("green"))) {
			led_device->led_mask = GPIO_22_INDEX;
		} else if (!strncmp(cdev->name, "blue", strlen("blue"))) {
			led_device->led_mask = GPIO_26_INDEX;
		} else {
			pr_info("invalid device tree value \n");
			return -EINVAL;
		}

		/* Initialise each led_classdev struct */
		/* disable led trigger timer until led is on */
		led_device->cdev.brightness = LED_OFF;
		led_device->cdev.brightness_set = led_control;
	
		/* register misc device led char devices */
		ret = devm_led_classdev_register(dev, &led_device->cdev);
		if (ret) {
			dev_err(dev, "failed to register classdev led %s \n", cdev->name);
			of_node_put(child);
			return ret;
		}

		/* attach led_device structure to pdev */
		platform_set_drvdata(pdev, led_device);
	}
	return 0;
}

static int __exit ledclass_remove(struct platform_device *pdev)
{
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,RGBclassleds"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver led_platform_driver = {
	.probe = ledclass_probe,
	.remove = ledclass_remove,
	.driver = {
		.name = "RGBclassleds",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int ledclass_init(void)
{
	int ret = platform_driver_register(&led_platform_driver);

	pr_info("usual init function \n");
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit ledclass_exit(void)
{
	pr_info("usual exit function \n");
	
	platform_driver_unregister(&led_platform_driver);
	
}
module_init(ledclass_init);
module_exit(ledclass_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
