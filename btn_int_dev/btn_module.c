/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/of.h>

static char *keys_name = "hello_keys";

static irqreturn_t hello_keys_isr(int irq, void *data)
{
	struct device *dev = data;
	dev_info(dev, "irq recieved = %s\n", keys_name);
	return IRQ_HANDLED;
}

static struct miscdevice hello_keys = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mydev",
};
static int __init my_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	struct gpio_desc *gpio;
	
	/* first method to get linuxirq */
	gpio = devm_gpiod_get(&pdev->dev, NULL, GPIOD_IN);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "gpiod_get failed \n");
		return PTR_ERR(gpio);
	}
	irq = gpiod_to_irq(gpio);
	if (irq < 0)
		return irq;
	dev_info(&pdev->dev, "irq number is %d\n", irq);
	
	/* second method */
/*	irq =  platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq found \n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "irq number is %d\n", irq);
*/
	ret = devm_request_irq(&pdev->dev, irq, hello_keys_isr, IRQF_TRIGGER_FALLING,
			keys_name, &pdev->dev);
	if (ret) {
		pr_info("Unable to allocate request_irq \n");
		return ret;
	}
	pr_info("hello there! \n");

	/* allocate device numbers */
	ret = misc_register(&hello_keys);
	if (ret < 0) {
		pr_info("Unable to allocate misc device \n");
		return ret;
	}

	pr_info("Device created with mino number = %d\n", hello_keys.minor);
	return 0;
}

static int __exit my_remove(struct platform_device *pdev)
{
	misc_deregister(&hello_keys);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,intkey"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver my_platform_driver = {
	.probe = my_probe,
	.remove = my_remove,
	.driver = {
		.name = "intkey",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,		
	}
};

/* Register your platform driver */
module_platform_driver(my_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("Press interupt button module");
