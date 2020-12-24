/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>

/* platform_driver_register platform_get_resources and platform_set_drvdata() */
#include <linux/platform_device.h>
#include <linux/io.h> /* devm_ioremap(), iowrite32() */
#include <linux/of.h>
#include <linux/uio_driver.h> /* uio_info, uio_register_device() */

static struct uio_info the_uio_info;

static int __init uio_probe(struct platform_device *pdev)
{
	void __iomem *g_ioremap_addr;
	int ret;
	struct resource *r;
	struct device *dev = &pdev->dev;

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
	
	the_uio_info.name = "led_uio";
	the_uio_info.version = "1.0";
	the_uio_info.mem[0].memtype = UIO_MEM_PHYS;
	the_uio_info.mem[0].addr = r->start; /* physical address needed for start of kernel user mapping */
	the_uio_info.mem[0].size = resource_size(r);
	the_uio_info.mem[0].name = "demo_uio_driver_hw_region";
	the_uio_info.mem[0].internal_addr = g_ioremap_addr; /* virtual address for internal driver usage */


	ret = uio_register_device(&pdev->dev, &the_uio_info);
	if (ret) {
		pr_err("uio register returned %d\n", ret);
		return ret;
	}

	return 0;
}

static int __exit uio_remove(struct platform_device *pdev)
{
	uio_unregister_device(&the_uio_info);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,UIO"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver uio_platform_driver = {
	.probe = uio_probe,
	.remove = uio_remove,
	.driver = {
		.name = "UIO",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int uio_init(void)
{
	int ret = platform_driver_register(&uio_platform_driver);

	pr_info("usual init function \n");
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit uio_exit(void)
{
	pr_info("usual exit function \n");
	
	platform_driver_unregister(&uio_platform_driver);
	
}
module_init(uio_init);
module_exit(uio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
