/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#define LED_NAME_LEN 	32
#define CMD_RED_SHIFT	4
#define CMD_BLUE_SHIFT	4
#define CMD_GREEN_SHIFT	0
#define CMD_MAIN_SHIFT 	4	
#define CMD_SUB_SHIFT	0
#define EN_CS_SHIFT	(1 << 2)	

struct led_device {
	u8 brightness; /* 0 to 15 */
	struct led_classdev cdev; /* probe to populate this structure */
	struct led_priv *private; /* global data updated and exposed to all leds */

};

/* global led data, the parameters are updated after led_control() call */
struct led_priv {
	u32 num_leds; /* total leds declared in DT */
	u8 command[3]; /* commands sent to ltc3206 : three of them */
	struct gpio_desc *display_cs; /* Control ENRGB/S pin */
	struct i2c_client *client; /* get i2c address from the the client */
};

/* write to i2c device */
/* send on/off value from your terminal to led */
static int ltc3206_led_write(struct i2c_client *client, u8 *command)
{
	int ret = i2c_master_send(client, command, 3);
	if (ret >= 0)
		return 0;
	return ret;
}

/*sysfs entry */
static ssize_t sub_select(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	char *buffer;
	struct led_priv *priv;
	struct i2c_client *client;

	buffer = buf;
	*(buffer + (count -1)) = '\0';

	client = to_i2c_client(dev); /* extracting client data from dev */
	priv = i2c_get_clientdata(client); /* this another conversion function */
	
	priv->command[0] |=  EN_CS_SHIFT; /* set third bit in A2 */
	ltc3206_led_write(priv->client, priv->command);

	if (!strcmp(buffer, "on")) {
		gpiod_set_value(priv->display_cs, 1); /* low */
		usleep_range(100, 200);
		gpiod_set_value(priv->display_cs, 0); /* high */
	} else if (!strcmp(buffer, "off")) {
		gpiod_set_value(priv->display_cs, 0); /* high */
		usleep_range(100, 200);
		gpiod_set_value(priv->display_cs, 1); /* low */
	}
	else {
		dev_err(&client->dev, "bad bad bad value \n");
		return -EINVAL;
	}
	return count;
}

/*sysfs entry */
static ssize_t rgb_select(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	char *buffer;
	struct led_priv *priv;
	struct i2c_client *client;

	buffer = buf;
	*(buffer + (count -1)) = '\0';

	client = to_i2c_client(dev); /* extracting client data from dev */
	priv = i2c_get_clientdata(client); /* this another conversion function */
	
	priv->command[0] |=  ~(EN_CS_SHIFT); /* clear third bit in A2 */
	ltc3206_led_write(priv->client, priv->command);

	if (!strcmp(buffer, "on")) {
		gpiod_set_value(priv->display_cs, 1); /* low */
		usleep_range(100, 200);
		gpiod_set_value(priv->display_cs, 0); /* high */
	} else if (!strcmp(buffer, "off")) {
		gpiod_set_value(priv->display_cs, 0); /* high */
		usleep_range(100, 200);
		gpiod_set_value(priv->display_cs, 1); /* low */
	}
	else {
		dev_err(&client->dev, "bad bad bad value \n");
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(rgb, S_IWUSR, NULL, rgb_select);
static DEVICE_ATTR(sub, S_IWUSR, NULL, sub_select);

static struct attribute *display_cs_attrs[] = {
	&dev_attr_rgb.attr,
	&dev_attr_sub.attr,
	NULL,
};

static struct attribute_group display_cs_group = {
	.name = "display_cs",
	.attrs = display_cs_attrs,
};

/* write led brightness */
/* command is in led_priv, pointed inside led_device */
static int led_control(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct led_device *led = container_of(led_cdev, struct led_device, cdev);
	struct led_classdev *cdev = &led->cdev;
	led->brightness = brightness;

	dev_info(cdev->dev, "The subsystem name is %s\n", cdev->name);
	if (brightness > 15 || brightness < 0)
		return -EINVAL;

	if (strcmp(cdev->name, "red") == 0) {
		led->private->command[0] &= 0x0F; /* clear the upper nibble */
		led->private->command[0] |= ((led->brightness << CMD_RED_SHIFT) & 0xF0);
	} else if (strcmp(cdev->name, "blue") == 0) {
		led->private->command[1] &= 0x0F; /* clear the upper nibble */
		led->private->command[1] |= ((led->brightness << CMD_BLUE_SHIFT) & 0xF0);
	} else if (strcmp(cdev->name, "green") == 0) {
		led->private->command[1] &= 0xF0; /* clear the lower nibble */
		led->private->command[1] |= ((led->brightness << CMD_GREEN_SHIFT) & 0x0F);
	} else if (strcmp(cdev->name, "main") == 0) {
		led->private->command[2] &= 0xF0; /* clear the lower nibble */
		led->private->command[2] |= ((led->brightness << CMD_MAIN_SHIFT) & 0x0F);
	} else if (strcmp(cdev->name, "sub") == 0) {
		led->private->command[2] &= 0xF0; /* clear the lower nibble */
		led->private->command[2] |= ((led->brightness << CMD_SUB_SHIFT) & 0x0F);
	} else {
		dev_info(cdev->dev, "No display found\n");
	}

	return ltc3206_led_write(led->private->client, led->private->command);
}

static int __init ltc3206_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	u8 value[3];
	int ret;
	struct fwnode_handle *child;
	struct led_priv *priv;
	int count;
	struct device *dev = &client->dev;

	pr_info("Hello there! \n");

	value[0] = 0x00;
	value[1] = 0xF0; /* blue */
	value[2] = 0x00;

	i2c_master_send(client, value, 3);

	count = device_get_child_node_count(dev);
	if (!count)
		return -ENODEV;

	dev_info(dev, "There are %d nodes \n", count);

	priv = devm_kzalloc(dev, sizeof(struct led_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->display_cs = devm_gpiod_get(dev, NULL, GPIOD_ASIS);
	if (IS_ERR(priv->display_cs)) {
		ret = PTR_ERR(priv->display_cs);
		dev_err(dev, "unable to get gpio desc\n");
		return ret;
	}
	gpiod_direction_output(priv->display_cs, 1); /* low */
	
	ret = sysfs_create_group(&client->dev.kobj, &display_cs_group);
	if (ret < 0) {
		dev_err(&client->dev, "couldn't create sysfs group \n");
		return ret;
	}
	
	/* all child nodes */
	device_for_each_child_node(dev, child) {
		struct led_device *led_dev;
		struct led_classdev *cdev;

		led_dev = devm_kzalloc(dev, sizeof(struct led_device), GFP_KERNEL);
		if (!led_dev) 
			return -ENOMEM;

		cdev = &led_dev->cdev;
		led_dev->private = priv;

		fwnode_property_read_string(child, "label", &cdev->name);

		if (strcmp(cdev->name, "main") == 0) {
			led_dev->cdev.brightness_set_blocking = led_control;
			ret = devm_led_classdev_register(dev, &led_dev->cdev);
			if (ret)
				goto err;
			dev_info(cdev->dev, "name %s num = %d\n", cdev->name, priv->num_leds);
		} else if (strcmp(cdev->name, "sub") == 0) {
			led_dev->cdev.brightness_set_blocking = led_control;
			devm_led_classdev_register(dev, &led_dev->cdev);
			if (ret)
				goto err;
			dev_info(cdev->dev, "name %s num = %d\n", cdev->name, priv->num_leds);
		} else if (strcmp(cdev->name, "red") == 0) {
			led_dev->cdev.brightness_set_blocking = led_control;
			devm_led_classdev_register(dev, &led_dev->cdev);
			if (ret)
				goto err;
			dev_info(cdev->dev, "name %s num = %d\n", cdev->name, priv->num_leds);
		} else if (strcmp(cdev->name, "green") == 0) {
			led_dev->cdev.brightness_set_blocking = led_control;
			devm_led_classdev_register(dev, &led_dev->cdev);
			if (ret)
				goto err;
			dev_info(cdev->dev, "name %s num = %d\n", cdev->name, priv->num_leds);
		} else if (strcmp(cdev->name, "blue") == 0) {
			led_dev->cdev.brightness_set_blocking = led_control;
			devm_led_classdev_register(dev, &led_dev->cdev);
			if (ret)
				goto err;
			dev_info(cdev->dev, "name %s num = %d\n", cdev->name, priv->num_leds);
		} else {
			dev_err(dev, "Bad device tree value\n");
			return -EINVAL;
		}
		priv->num_leds++;
	}

	dev_info(dev, "end of probe function \n");
	return 0;
err:
	fwnode_handle_put(child);
	sysfs_remove_group(&client->dev.kobj, &display_cs_group);
	return ret;
}

static int __exit ltc3206_remove(struct i2c_client *client)
{
	pr_info("General Kenobi! \n");
	sysfs_remove_group(&client->dev.kobj, &display_cs_group);
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,ltc3206"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static const struct i2c_device_id ltc3206_id[] = {
	{"ltc3206", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, ltc3206_id);

static struct i2c_driver ltc3206_driver = {
	.probe = ltc3206_probe,
	.remove = ltc3206_remove,
	.id_table = ltc3206_id,
	.driver = {
		.name = "ltc3206",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};

static int led_init(void)
{
	int ret = i2c_add_driver(&ltc3206_driver);

	pr_info("usual init function \n");
	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit led_exit(void)
{
	pr_info("usual exit function \n");
	
	i2c_del_driver(&ltc3206_driver);
	
}
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
