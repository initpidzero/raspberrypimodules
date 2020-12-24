/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h> /* file operations fops */
#include <linux/i2c.h> 
#include <linux/of.h> /* of_property_read_string() */
#include <linux/uaccess.h> /* copy_from/to_user() */


/* private data structure */
struct ioexp_dev {
	struct i2c_client *client; /* i2c device */
	struct miscdevice ioexp_miscdevice; /* handle open() */
	char name[8]; /* name of i2c i/o device */ 
};

/* user is reading data from /dev/ioexpxx */
static ssize_t  ioexp_read_file(struct file *file, char __user *userbuf,
		size_t count, loff_t *ppos)
{
	int size;
	struct ioexp_dev *ioexp;
	char buf[4];
	int val;

	ioexp =	container_of(file->private_data, struct ioexp_dev, ioexp_miscdevice);
	val = i2c_smbus_read_byte(ioexp->client); 
	if (val < 0) {
		dev_info(&ioexp->client->dev, "i2c smb read failed  %d\n", val);
		return val;
	}
	/* convert to string */
	size = sprintf(buf, "%02x", val); /* size is two since we are using hex */
	/* add \n to the value */
	buf[size] = '\n';
	
	if (*ppos == 0) {
		if (copy_to_user(userbuf, buf, size + 1)) {
			pr_info("copy to user failed\n");
			return -EFAULT;
		}
		(*ppos)++;
		return size + 1;
	}
	return 0;
}

/* user is sending data to /dev/ioexpxx */
static ssize_t ioexp_write_file(struct file *file, const char __user *userbuf, 
		size_t count, loff_t *ppos)
{
	int ret;
	struct ioexp_dev *ioexp;
	char buf[4];
	unsigned long val;

	ioexp =	container_of(file->private_data, struct ioexp_dev, ioexp_miscdevice);
	if (copy_from_user(buf, userbuf, count)) {
		dev_err(&ioexp->client->dev, "copy from user failed\n");
		return -EFAULT;
	}
	buf[count - 1] = '\0';
	ret = kstrtoul(buf, 0, &val);
	if (ret) {
		dev_err(&ioexp->client->dev, "kstrtoul failed \n");
		return ret;
	}
	ret = i2c_smbus_write_byte(ioexp->client, val); 
	if (ret < 0) {
		dev_info(&ioexp->client->dev, "i2c smb write failed  %d\n", ret);
		return ret;
	}
	
	dev_info(&ioexp->client->dev, "name = %s count = %zu val = %lu \n", ioexp->name, count, val);
	return count;
}

/* misc files fops structure */
static const struct file_operations ioexp_fops = {
	.owner = THIS_MODULE,
	.read = ioexp_read_file,
	.write = ioexp_write_file,
};

static int ioexp_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	static int counter = 0;
	struct ioexp_dev *ioexp;
	
	pr_info("Hello there! \n");
	/* allocate memory of new device structure */
	ioexp = devm_kzalloc(&client->dev, sizeof(struct ioexp_dev), GFP_KERNEL);
	if (!ioexp)
		return -ENOMEM;

	/* store pointer to the device structure for bus device context */
	i2c_set_clientdata(client, ioexp);

	
	/* store pointer to I2C device/client */
	ioexp->client = client;

	/* initialise a miscdevice, ioexp is incremented after each probe call */
	sprintf(ioexp->name, "ioexp%02d", counter++);
	ioexp->ioexp_miscdevice.name = ioexp->name;
	ioexp->ioexp_miscdevice.minor = MISC_DYNAMIC_MINOR;
	ioexp->ioexp_miscdevice.fops = &ioexp_fops;

	misc_register(&ioexp->ioexp_miscdevice);

	return 0;
}

static int ioexp_remove(struct i2c_client *client)
{
	struct ioexp_dev *ioexp;
	ioexp = i2c_get_clientdata(client);
	misc_deregister(&ioexp->ioexp_miscdevice);
	pr_info("General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,ioexp"},
	{ },
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static const struct i2c_device_id i2c_ids[] = {
	{ .name = "ioexp", },
	{ }
};

MODULE_DEVICE_TABLE(i2c, i2c_ids);

static struct i2c_driver ioexp_driver = {
	.probe = ioexp_probe,
	.remove = ioexp_remove,
	.id_table = i2c_ids,
	.driver = {
		.name = "ioexp",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};
static int ioexp_init(void)
{
	int ret = i2c_add_driver(&ioexp_driver);

	if (ret) {
		pr_err("Platform register returned %d\n", ret);
		return ret;
	}

	pr_info("usual init function \n");
	return 0;
}

static void __exit ioexp_exit(void)
{
	pr_info("usual exit function \n");
	
	i2c_del_driver(&ioexp_driver);
}
module_init(ioexp_init);
module_exit(ioexp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LED ON OFF MODULE");
