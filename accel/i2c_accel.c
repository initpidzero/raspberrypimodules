/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/fs.h> /* file operations fops */
#include <linux/i2c.h> /* for i2c_driver, i2c_client, i2c_get/set_clientdata, */
#include <linux/of.h> /* of_property_read_string() */
#include <linux/uaccess.h> /* copy_from/to_user() */
#include <linux/input-polldev.h> /* input_polled_dev, input_allocate_polled_device, input_register_polled_device */

#define POWER_CTL 0x2D
#define PCTL_MEASURE  (1 << 3)
#define OUT_X_MSB 0x33

/* private data structure */
/* pointers between physical devices(i2c) and logical devices(input subsystem) */
/* To manage multiple devices from same driver */
struct ioaccel_dev {
	struct i2c_client *i2c_client; /* i2c device */
	struct input_polled_dev *polled_input; /* handle open() */
};

/*  files fops structure */
static const struct file_operations ioaccel_fops = {
	.owner = THIS_MODULE,
	.read = ioaccel_read_file,
	.write = ioaccel_write_file,
};

/* Called every 50 ms.
 */
static void ioaccel_poll(struct input_polled_dev *pl_dev)
{
	struct ioacc_dev *ioaccel = pl_dev->private;
	int val = 0;
	/* read OUT_X_MSB register.
	 * ioaccel->i2c_client can be used to get I2C address in
	 * client->address; I2C Address is 0x1D. This address is obtained though
	 * device tree binding and stored in struct i2c_client, this is sent to
	 * probe by client pointer */
	val = i2c_smbus_read_byte_data(ioaccel->i2c_client, OUT_X_MSB);
}

static int ioaccel_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	static int counter = 0;
	struct ioaccel_dev *ioaccel;

	pr_info("Hello there! \n");
	/* allocate memory of new device structure */
	ioaccel = devm_kzalloc(&client->dev, sizeof(struct ioaccel_dev), GFP_KERNEL);
	if (!ioaccel)
		return -ENOMEM;

	/* store pointer to the device structure for bus device context */
	/* attach ioaccel to i2c_client to be used in other functions */
	i2c_set_clientdata(client, ioaccel);

	/* allocated input_polled_dev */
	ioaccel->polled_input = devm_input_allocate_polled_device(&client->dev);

	/* init polled input device */
	/* store pointer to I2C device/client  physical to logical mapping */
	ioaccel->client = client; /* to exchange data with accel */
	ioaccel->polled_input->private = ioaccel;  /* this can be recovered later */
	ioaccel->polled_input->poll_interval = 50; /* call back interval */
	ioaccel->polled_input->poll = ioaccel_poll; /* call back fn */
	ioaccel->polled_input->dev.parent = &client->dev; /* physical/logical device mapping */
	ioaccel->polled_input->input->name = "IOACCEL keyboard"; /* input sub-device parameters that will appear on log on registering the device */
	ioaccel->polled_input->input->id.bustype = BUS_I2C; /* input sub-device parameters */

	set_bit(EV_KEY, ioaccel->polled_input->input->evbit); /* EV_KEY event type */
	set_bit(KEY_1, ioaccel->polled_input->input->keybit); /* KEY_1 event code */

	/* register with input core */
	input_register_polled_device(ioaccel->polled_input);
	
	return 0;
}

static int ioaccel_remove(struct i2c_client *client)
{
	struct ioaccel_dev *ioaccel;
	ioaccel = i2c_get_clientdata(client);
	input_register_polled_device(ioaccel->polled_input);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,ioaccel"},
	{ },
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static const struct i2c_device_id i2c_ids[] = {
	{ .name = "adxl345", },
	{ }
};

MODULE_DEVICE_TABLE(i2c, i2c_ids);

static struct i2c_driver ioaccel_driver = {
	.probe = ioaccel_probe,
	.remove = ioaccel_remove,
	.id_table = i2c_ids,
	.driver = {
		.name = "adxl345",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int ioaccel_init(void)
{
	int ret = module_i2c_driver(ioaccel_driver);

	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	pr_info("usual init function \n");
	return 0;
}

static void __exit ioaccel_exit(void)
{
	pr_info("usual exit function \n");
	
	i2c_del_driver(&ioaccel_driver);
}
module_init(ioaccel_init);
module_exit(ioaccel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
