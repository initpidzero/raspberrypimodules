/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
/* platform_driver_register platform_get_resources and platform_set_drvdata() */
#include <linux/platform_device.h>
#include <linux/interrupt.h> /* */
#include <linux/property.h> /*  */
#include <linux/kthread.h> /* */
#include <linux/gpio/consumer.h> /* copy_from/to_user() */
#include <linux/delay.h> 
#include <linux/spinlock.h>
#include <linux/of.h>

#define LED_NAME_LEN	32
#define INT_NUMBER	2

//static const char *KEYS_NAME1 = "key1";
//static const char *KEYS_NAME2 = "key2";

/* private data structure */
/* store device specific infor for eachof three led devices */
struct led_device {
	char name[LED_NAME_LEN];
	struct gpio_desc *ledd; /* gpio descriptor for each pin */
	struct device *dev;
	struct keyled_priv *private; /* global data for all led devices */
};

/* global information for all led levices */
struct keyled_priv {
	u32 num_leds; /* number of leds declare in DT. */
	u8 led_flag; /* if any LEDS needs to be switched on in the beginning */
	u8 task_flag; /* if any kthread is running */
	u32 period; /* blinking period for leds */
	spinlock_t period_lock; /* protect period between interrupt and user context tasks */
	struct task_struct *task; /* kthread_run returns this */
	struct class *led_class; /* class_create returns this, used for device_create. */ 
	struct device *dev;
	dev_t led_devt; /* alloc_chrdev_region(), first device identifier */
	struct led_device *leds[]; /* array of pointers */
	
};

/* kthread function */
static int led_flash(void *data)
{
	unsigned long flags;
	u32 value = 0;
	struct led_device *led_dev = data;
	dev_info(led_dev->dev, "Task Started\n");
	/* kthread_stop() request is queued previously = non zero value */
	while (!kthread_should_stop()) {
		u32 period;
		/* disable irqs when period is being accessed */
		spin_lock_irqsave(&led_dev->private->period_lock, flags);
		period = led_dev->private->period;
		spin_unlock_irqrestore(&led_dev->private->period_lock, flags);
		value = !value;
		gpiod_set_value(led_dev->ledd, value);
		msleep(period / 2); /* repeat this loop after every half period until value changes to non zero */
	}
	gpiod_set_value(led_dev->ledd, 1); /* switch off the led */
	dev_info(led_dev->dev, "Task completed\n");
	return 0;
}

/* sysfs methods */

/* switch on and off leds */
static ssize_t set_led_store(struct device *dev, struct device_attribute *attr, 
		const char *buf, size_t count)
{
	int i;
	char *buffer = (char *)buf;
	struct led_device *led_count;
	struct led_device *led = dev_get_drvdata(dev); 

	*(buffer + count - 1) = '\0'; /* replace \n with \0, we don't need the damn newline */

	if (led->private->task_flag == 1) {
		kthread_stop(led->private->task); /* so the stop request comes here */
		led->private->task_flag = 0;
	}

	if (!strcmp(buffer , "on")) {
		if (led->private->led_flag == 1) {
			for (i = 0; i < led->private->num_leds; i++) {
				led_count = led->private->leds[i]; /* each data structure from led */
				gpiod_set_value(led_count->ledd, 1);
			}
			gpiod_set_value(led->ledd, 0);
		} else {
			gpiod_set_value(led->ledd, 0);
			led->private->led_flag = 1; /* we change this value when led_flag == 0 */
		}
	} else if (!strcmp(buffer , "off")) {
		gpiod_set_value(led->ledd, 1); /* active low */
	} else {
		dev_info(led->dev, "Bad led value.\n");
		return -EINVAL; 
	}
	return count;
}

static DEVICE_ATTR_WO(set_led);

/* blinking on specific led running a kthread() */
static ssize_t blink_on_led_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int i;
	char *buffer = (char *)buf;
	
	struct led_device *led = dev_get_drvdata(dev);
	*(buffer + count - 1) = '\0';

	/* kthread flag */
	if (led->private->led_flag == 1) {
		for (i = 0; i < led->private->num_leds; i++) {
			struct led_device *led_count = led->private->leds[i]; 
			gpiod_set_value(led_count->ledd, 1); /* set value of gpiod to 1*/
		}
	}	

	if (!strcmp( buffer , "on")) {
		if (led->private->task_flag == 0) {
			led->private->task = kthread_run(led_flash, led, "led_flash_thread");
			if (IS_ERR(led->private->task)) {
				dev_info(led->dev, "Failed to create the task by kthread_run \n"); 
				return PTR_ERR(led->private->task);
			}
		} else
			return -EBUSY;
	} else {
		dev_info(led->dev, "Bad led value.\n");
		return -EINVAL; 
	} 
	
	led->private->task_flag = 1;
	dev_info(led->dev, "Blink on led exited\n");

	return count;
}

static DEVICE_ATTR_WO(blink_on_led);

/* switch off blinking of leds */
static ssize_t blink_off_led_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int i;
	char *buffer = (char *)buf;
	struct led_device *led = dev_get_drvdata(dev);

	*(buffer + count - 1) = '\0';

	if (!strcmp(buffer, "off")) {
		if (led->private->task_flag == 1) {
			kthread_stop(led->private->task);
			for (i = 0; i< led->private->num_leds; i++) {
				struct led_device *led_count = led->private->leds[i];
				gpiod_set_value(led_count->ledd, 1);
			}  
		} else
			return 0;
	} else {
		dev_info(led->dev, "Bad led value\n");
		return -EINVAL;
	}
	led->private->task_flag = 0;
	return count;
}

static DEVICE_ATTR_WO(blink_off_led);

static ssize_t set_period_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long flags;
	int ret;
	unsigned period;
	struct led_device *led = dev_get_drvdata(dev);
	
	ret = sscanf(buf, "%u", &period);
	if (ret < 1 || period < 10 || period > 10000) {
		dev_err(dev, "invalid period\n");
		return -EINVAL;
	}
	
	spin_lock_irqsave(&led->private->period_lock, flags);
	led->private->period = period;
	spin_unlock_irqrestore(&led->private->period_lock, flags);

	dev_info(led->dev, "period is set to %u\n", period);
	return count;
}

static DEVICE_ATTR_WO(set_period);

static struct attribute *led_attrs[] = {
	&dev_attr_set_led.attr,
	&dev_attr_blink_on_led.attr,
	&dev_attr_blink_off_led.attr,
	&dev_attr_set_period.attr,
	NULL,
};

static const struct attribute_group led_group = {
	.attrs = led_attrs,
};

static const struct attribute_group *led_groups[] = {
	&led_group,
	NULL,
};

/* calculate sizeof structure with num_leds */
static inline int sizeof_keyled_priv(int num_leds)
{
	return sizeof(struct keyled_priv) + sizeof(struct led_device *) * num_leds;
}

static irqreturn_t KEY_ISR1(int irq, void *data)
{
	struct keyled_priv *priv = data;

	spin_lock(&priv->period_lock);
	priv->period = priv->period + 10;
	if ((priv->period < 10) || (priv->period > 10000))
		priv->period = 10;
	spin_unlock(&priv->period_lock);
	
	dev_info(priv->dev, "In ISR period value is  %d\n", priv->period);
	return IRQ_HANDLED;
}

static irqreturn_t KEY_ISR2(int irq, void *data)
{
	struct keyled_priv *priv = data;

	spin_lock(&priv->period_lock);
	priv->period = priv->period - 10;
	if ((priv->period < 10) || (priv->period > 10000))
		priv->period = 10;
	spin_unlock(&priv->period_lock);

	dev_info(priv->dev, "In ISR period value is  %d\n", priv->period);
	return IRQ_HANDLED;
}

/* create the LED devices under sysfs keyled entry */
static struct led_device *led_device_register(const char *name, int count,
		struct device *parent, dev_t led_devt, struct class *led_class)
{
	struct led_device *led;
	dev_t devt;
	int ret;

	/* first allocate a new led device */
	led = devm_kzalloc(parent, sizeof(struct led_device), GFP_KERNEL);	
	if (!led)
		return ERR_PTR(-ENOMEM);

	/* get the minor number of each device */
	devt = MKDEV(MAJOR(led_devt), count);

	/* create a device node and initialise it */
	led->dev = device_create(led_class, parent, devt, led, "%s", name);
	if (IS_ERR(led->dev)) {
		dev_err(led->dev, "unable to create device %s\n", name);
		ret = PTR_ERR(led->dev);
		return ERR_PTR(ret);
	}

	dev_info(led->dev, "major and minor %d %d\n",MAJOR(led_devt), MINOR(devt));

	/* to be recovered in sysfs entry */
	dev_set_drvdata(led->dev, led);
	strncpy(led->name, name, LED_NAME_LEN);

	return led;
}

static int __init my_probe(struct platform_device *pdev)
{
	int i;
	unsigned int major;
	struct fwnode_handle *child;
	struct keyled_priv *priv;
	int ret;
	int count;
	struct device *dev = &pdev->dev;

	pr_info("hello there! \n");
	
	/* get number of nodes for leds and interrupts */
	count = device_get_child_node_count(dev);
	if (!count)
		return -EINVAL;

	dev_info(dev, "number of nodes %d\n", count);

	priv = devm_kzalloc(dev, sizeof_keyled_priv(count - INT_NUMBER), GFP_KERNEL); 
	if (!priv)
		return -ENOMEM; 

	/* Allocate 3 device numbers */
	alloc_chrdev_region(&priv->led_devt, 0, count - INT_NUMBER, "keyled_class");
	major = MAJOR(priv->led_devt);
	dev_info(dev, "Major number  %d\n", major);

	/* create the LED class */
	priv->led_class	= class_create(THIS_MODULE, "key_led");
	if (!priv->led_class) {
		dev_info(dev, "failed to allocate class\n");
		return -ENOMEM;
	}

	/* create sysfs group */
	priv->led_class->dev_groups = led_groups;
	priv->dev = dev;

	spin_lock_init(&priv->period_lock); /* so we created lock here? */
	
	/* parse all device nodes in DT */
	device_for_each_child_node(dev, child) {
		struct led_device *new_led;
		int irq, flags;
		struct gpio_desc *keyd;
		const char *label_name, *colour_name, *trigger;

		fwnode_property_read_string(child, "label", &label_name);
	
		/* assign a different mask for each LED */
		if (!strcmp(label_name, "led")) {
			fwnode_property_read_string(child, "colour", &colour_name);
			/* 0 for first minor number and then increment */
			/* led_devices under keyled class priv->num_leds = 0 */ 
			new_led = led_device_register(colour_name, priv->num_leds,
					dev, priv->led_devt, priv->led_class);
			if (!new_led) {
				fwnode_handle_put(child);
				ret = PTR_ERR(new_led);
				for (i = 0; i < priv->num_leds - 1; i++) {
					device_destroy(priv->led_class, MKDEV(MAJOR(priv->led_devt), i));
				}
				class_destroy(priv->led_class);
				return ret;
			}
			new_led->ledd = devm_fwnode_get_gpiod_from_child(dev, NULL, child, GPIOD_ASIS, label_name);
			if (IS_ERR(new_led->ledd)) {
				fwnode_handle_put(child);
				ret = PTR_ERR(new_led->ledd);
				goto error;
			}

			/* associate each led struct with the global one */
			new_led->private = priv;

			/* add to global structure of array */
			priv->leds[priv->num_leds] = new_led;
			priv->num_leds++;
			
			gpiod_direction_output(new_led->ledd, 1);
			/* set led state to off */
			gpiod_set_value(new_led->ledd, 1); /* active_low as high */

		} else if (!strcmp(label_name, "KEY_1")) {
			keyd = devm_fwnode_get_gpiod_from_child(dev, NULL, child, GPIOD_ASIS, label_name);
			gpiod_direction_input(keyd);
			fwnode_property_read_string(child, "trigger", &trigger);
			if (!strcmp(trigger, "falling")) {
				flags = IRQF_TRIGGER_FALLING;
			} else if (!strcmp(trigger, "rising")) {
				flags = IRQF_TRIGGER_RISING;
			} else if (!strcmp(trigger, "both")) {
				flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
			} else {
				return -EINVAL;
			}
			irq = gpiod_to_irq(keyd);
			if (irq < 0)
				return irq;

			ret = devm_request_irq(dev, irq, KEY_ISR1, flags, "ISR1", priv);
			if (ret) {
				dev_err(dev, "Failed to request irq %d %d\n", irq, ret);
				return ret;
			}
			dev_info(dev, "IRQ number %d\n", irq);
		} else if (!strcmp(label_name, "KEY_2")) {
			keyd = devm_fwnode_get_gpiod_from_child(dev, NULL, child, GPIOD_ASIS, label_name);
			gpiod_direction_input(keyd);
			fwnode_property_read_string(child, "trigger", &trigger);
			if (!strcmp(trigger, "falling")) {
				flags = IRQF_TRIGGER_FALLING;
			} else if (!strcmp(trigger, "rising")) {
				flags = IRQF_TRIGGER_RISING;
			} else if (!strcmp(trigger, "both")) {
				flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
			} else {
				return -EINVAL;
			}

			irq = gpiod_to_irq(keyd);
			if (irq < 0)
				return irq;

			ret = devm_request_irq(dev, irq, KEY_ISR2, flags, "ISR2", priv);
			if (ret) {
				dev_err(dev, "Failed to request irq %d %d\n", irq, ret);
				return ret;
			}
			dev_info(dev, "IRQ number %d\n", irq);
		} else {
			pr_info("invalid device tree value \n");
			ret =  -EINVAL;
			goto error;
		}
	}
	/* reset period to 10 */
	priv->period = 10;
	/* attach led_device structure to pdev */
	platform_set_drvdata(pdev, priv);
	return 0;

error:
	/* unregister stuff */
	for (i = 0; i < priv->num_leds; i++) {
		device_destroy(priv->led_class, MKDEV(MAJOR(priv->led_devt), i));
	}
	class_destroy(priv->led_class);
	unregister_chrdev_region(priv->led_devt, priv->num_leds);
	return ret;
}

static int __exit my_remove(struct platform_device *pdev)
{
	int i;
	struct keyled_priv *priv = platform_get_drvdata(pdev);
	struct led_device *led_count;

	pr_info("General Kenobi! \n");

	if (priv->task_flag == 1) {
		kthread_stop(priv->task);
		priv->task_flag = 0;
	}

	if (priv->led_flag == 1) {
		for (i = 0; i < priv->num_leds; i++) {
			led_count = priv->leds[i];
			gpiod_set_value(led_count->ledd, 1);
		}
	}

	for (i = 0; i < priv->num_leds; i++) {
		device_destroy(priv->led_class, MKDEV(MAJOR(priv->led_devt), i));
	}

	class_destroy(priv->led_class);
	unregister_chrdev_region(priv->led_devt, priv->num_leds);
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,ledpwm"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver my_platform_driver = {
	.probe = my_probe,
	.remove = my_remove,
	.driver = {
		.name = "ledpwm",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int my_init(void)
{
	int ret = platform_driver_register(&my_platform_driver);

	pr_info("usual init function \n");
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit my_exit(void)
{
	pr_info("usual exit function \n");
	
	platform_driver_unregister(&my_platform_driver);
	
}
module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("TWO INTERRUPT MODULE");
