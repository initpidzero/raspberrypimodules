/*
** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/fs.h> /* file operations fops */
#include <linux/of.h> /* of_property_read_string() */
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/interrupt.h>

/* macros for specific command byte spi_read, _write, _write_then_read() */
#define ADXL345_CMD_MULTB	(1 << 6)
#define ADXL345_CMD_READ	(1 << 7)
#define ADXL345_WRITECMD(reg)	(reg & 0x3F) /* consider only 6 bits */
#define ADXL345_READCMD(reg)	(ADXL345_CMD_READ | (reg & 0x3F))
#define ADXL345_READMB_CMD(reg)	(ADXL345_CMD_READ | ADXL345_CMD_MULTB | (reg & 0x3F))

/* registers */
#define DEVID		0x00 /* R */
#define THRESH_TAP	0x1D /* RW */
#define DUR		0x21 /* RW TAP Duration */
#define TAP_AXES	0x2A /* RW Single/Double Tap Axis control */
#define ACT_TAP_STATUS	0x2B /* R Source of TAP */
#define BW_RATE		0x2C /* RW Data rate and power mode ctl */
#define POWER_CTL	0x2D /* RW power saving features ctl */
#define INT_ENABLE	0x2E /* RW Interrupt enable ctl */
#define INT_MAP		0x2F /* RW Interrupt mapping ctl */
#define INT_SOURCE	0x30 /* R Interrupt source */
#define DATA_FORMAT	0x31 /* RW Data format control */
#define DATAX0		0x32 /* R X axis Data 0 */
#define DATAX1		0x33 /* R X Axis Data 1 */
#define DATAY0		0x34 /* R Y axis Data 0 */
#define DATAY1		0x35 /* R Y Axis Data 1 */
#define DATAZ0		0x36 /* R Z axis Data 0 */
#define DATAZ1		0x37 /* R Z Axis Data 1 */
#define FIFO_CTL	0x38 /* RW FIFO Control */

/* values to be passed to registers */
#define ID_ADXL345	0xE5 /* device id for adxl */

#define SINGLE_TAP	(1 << 6) /* Works for INT_* registers */
/* if SUPPRESS bit is set, double tap is suppressed if acc value is
 * above threshold during tap_latency period, which is after first tab
 * but before opening of second window */
#define TAP_X_EN	(1 << 2) /* enable X axis in TAP_AXES register */
#define TAP_Y_EN	(1 << 1) /* enable Y axis in TAP_AXES register */
#define TAP_Z_EN	(1 << 0) /* enable Z axis in TAP_AXES register */

#define LOW_POWER	(1 << 4) /* low power bit in BW Register */
/* data communication rate, I am assuming we only care about
 * last 4 bits of values in that register. This determines rate of data
 * output. Output rate should be compatible with communicaton protocol */
#define RATE(x)		((x) & 0xF) /* low power rate 12.5 to 200 Hz */

#define PCTL_MEASURE	(1 << 3) /* Put register in measurement mode */
#define PCTL_STANDBY	0x00 /* default device are in standby mode*/

#define FULL_RES	(1 << 3) /* A full resolution in DATA_FORMAT reg*/

#define FIFO_MODE(x)	(((x) & 0x3) << 6) /* MS two bits of FIFO register */
#define FIFO_BYPASS	0
#define FIFO_FIFO	1
#define FIFO_STREAM	2
/* Setting 0 in sample bits sets the watermark status bit in INT_SOURCE reg */
/* Samples are number of FIFO enteries needed to trigger watermark interrupt.
 * Applicable to FIFO_FIFO, FIFO_STREAM */
#define SAMPLES(x)	((x) & 0x1F) / * only 5 bits are sample bits */

/* FIFO_STATUS stores how many entries are available to read */
/* read clears FIFO levels Max entries 32 */
/* so data is read from DATA_X _Y _Z to FIFO */
#define ADXL_X_AXIS	0
#define ADXL_Y_AXIS	1
#define ADXL_Z_AXIS	2

#define ADXL345_GPIO_NAME "int"

/* SPI operations */
#define AC_READ(ac, reg) ((ac)->bops->read((ac)->dev, reg))
#define AC_WRITE(ac, reg, val) ((ac)->bops->write((ac)->dev, reg, val))


/* SPI bus operations */
struct adxl345_bus_ops {
	u16 bustype;
	int (*read)(struct device *, unsigned char);
	int (*read_block)(struct device *, unsigned char, int, void *);
	int (*write)(struct device *, unsigned char, unsigned char);
};

struct axis_triple {
	int x;
	int y;
	int z;
};

/*driver specific information */
struct adxl345_platform_data {
	u8 low_power_mode; /* set for low power but higher noise */
	u8 tap_threshold; /* 62.5 mg/LSB 0xFF = +16g */
	u8 tap_duration; /* min time value for tap to register 625 us/LSB */
	/* we are skipping some of the MACROS with ADXL_ */
	u8 tap_axis_control;
	u8 data_rate; /* rate is 3200 / (2 ^ (15 - x)) default is 0xA = 100 Hz*/

	/*resolution granularity? */
#define ADXL_RANGE_PM_2G	0
#define ADXL_RANGE_PM_4G	1
#define ADXL_RANGE_PM_8G	2
#define ADXL_RANGE_PM_16G	3
	u8 data_range; /* set: full range is 4mg/LSB. clear: 10 bit mode range determines g-Range and scale factor*/
	u32 ev_code_tap[3]; /* btn,key code, tap_axis_control to control this */
	u8 fifo_mode; /* FIFO stop after 32 values, STREAM: overwrite after 32 values */
	u8 watermark; /* store upto watermark value before triggering the FIFO interrupt */

};

static const struct adxl345_platform_data adxl345_default_init = {
	.tap_threshold = 50,
	.tap_duration = 3,
	.tap_axis_control = TAP_Z_EN,
	.data_rate = 8,
	.data_range = FULL_RES,
        .ev_code_tap = {BUTTON_TOUCH, BUTTON_TOUCH, BUTTON_TOUCH}, /* EV_KEY x,y,z */
	.fifo_mode = FIFO_BYPASS,
	.watermark = 0,
};

/* private data structure */
struct adxl345 {
	struct gpio_desc *gpio;
	struct  device *dev;
	struct input_dev *input;
	struct adxl345_platform_data pdata;
	struct axis_triple saved;
	u8 phys[32];
	int irq;
	u32 model;
	u32 int_mask;
	const struct adxl345_bus_ops *bops;
};

/**
 * get the adxl345 axis data
 */
static void adxl345_get_triple(struct adxl345 *ac, struct axis_triple *axis)
{
        __le16 buf[3]; /* this might be equivalent to short */

        ac->bops->read_block(ac->dev, DATAX0, DATAZ1 - DATAX0 + 1, buf); /* starting address, size? */

        ac->saved.x = sign_extend32(le16_to_cpu(buf[0], 12)); /* sign extend the value with 12th bit as sign bit */
        axix->x = ac->saved.x;


        ac->saved.y = sign_extend32(le16_to_cpu(buf[1], 12)); /* sign extend the value with 12th bit as sign bit */
        axix->y = ac->saved.y;

        ac->saved.z = sign_extend32(le16_to_cpu(buf[2], 12)); /* sign extend the value with 12th bit as sign bit */
        axix->z = ac->saved.z;
}

/**
 * EV_KEY will generated with 3 different event codes depending on the axes
 * where tap is detected
 * SINGLE_TAP event. check ACT_TAP_STATUS(0x2B) and TAP_* bits, if bits are
 * enabled there is an event to report
*/
static void adxl345_send_key_events(struct adxl345 *ac,
			   struct adxl345_platform_data *pdata, int status,
			   int press)
{
	int i;

	for (i = ADXL_X_AXIS; i < = ADXL_Z_AXIS; i++) {
		/*TODO decipher this */
		if (status & (1 << (ADXL_Z_AXIS - i)))
			input_report_key(ac->input,
                                         pdata->ev_code_tap[i], press);
	}
}

/* single tap event */
static void adxl345_do_tap(struct adxl345 *ac,
			   struct adxl345_platform_data *pdata, int status)
{
	adxl345_send_key_events(ac, pdata, status, true);
	input_sync(ac->input);
	adxl345_send_key_events(ac, pdata, status, false);

}

/* threaded interrupt handler for single tap
 * interrupt handler is executed inside a thread
 * its allowed to block to communicated with i2c/spi devices */
static  irqreturn_t adxl345_irq(int irq, void *handle)
{
	struct adxl345 *ac = handle;
	struct adxl345_plaform_data *pdata = &ac->pdata;
	int int_stat, tap_stat;

	/* read ACT_TAP_STATUS before clearing interrupt
	 * if TAP is disabled, don't read ACT_TAP_STATUS
	 * read ACT_TAP_STATUS if any of the axes is enabled */
	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))
		tap_stat = AC_READ(ac, ACT_TAP_STATUS);
	else
		tap_stat = 0;

	/* read register. The interrupt is cleared */
	int_stat = AC_READ(ac, INT_SOURCE);

	if (int_stat & SINGLE_TAP) {
		dev_info(ac->dev, "single tap interrupt has occured\n");
		adxl345_do_tap(ac, pdata, tap_stat); /* ACT_TAP_STATUS register */
	}

	input_sync(ac->input);

	return IRQ_HANDLED;
}

static ssize_t adxl345_rate_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct adxl345 *ac = dev_get_drvdata(dev);
        return sprintf(buf, "%u\n", RATE(ac->pdata.data_rate));
}

static ssize_t adxl345_rate_store(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct adxl345 *ac = dev_get_drvdata(dev);
        u8 val;
        int error;

        /* transform the array into u8 values */
        error = kstrtou8(buf, 10, &val); /* This is for one byte */
        if (error)
                return error;

        /*  low power mode low_power_mode = 1 but higher noise */
        ac->pdata.data_rate = RATE(val);
        AC_WRITE(ac, ac->pdata.data_rate |
                 (ac->pdata.low_power_mode ? LOW_POWER : 0));

        return count;
}

static ssize_t adxl345_position_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        ssize_t count;

        struct adxl345 *ac = dev_get_drvdate(dev);

        count = sprintf(buf, "(%d, %d, %d)\n", ac->saved.x,
                        ac->saved.y, ac->saved.z);

        return count;
}

static ssize_t adxl345_position_read(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        struct axis_triple axis;
        ssize_t count;

        struct adxl345 *ac = dev_get_drvdate(dev);
        adxl345_get_triple(ac, &axis);

        count = sprintf(buf, "(%d, %d, %d)\n", axis.x, axis.y, axis.z);

        return count;
}

/* sysfs entries to access from user space.
 * read sample rate, read data of three axes, last strored values of the axes */
static DEVICE_ATTR(rate, 0664, adxl345_rate_show, adxl345_rate_store);
static DEVICE_ATTR(position, S_IRUGO, adxl345_position_show, NULL);
static DEVICE_ATTR(read, S_IRUGO, adxl345_position_read, NULL);

static struct attribute *adxl345_attributes[] = {
        &dev_attr_rate.attr,
        &dev_attr_position.attr,
        &dev_attr_read.attr,
        NULL
};

static const struct attribute_group_adxl345_attr_group = {
        .attrs = adxl345_attributes,
};

/**
 * adxl345_spi_read - write register address and read its value
 * @reg: register address to read
 */
static int adxl345_spi_read(struct device *dev, unsigned char reg)
{
        struct spi_device *spi = to_spi_device(dev); /* container_of to get spi_device struct */
        u8 cmd;

        cmd = ADXL345_READCMD(reg);

        return spi_w8r8(spi, cmd); /* write the command and read the result */
}

/**
 * adxl345_spi_read_block - read multiple registers
 * @reg: first register address to read
 * @count: number of registers to read
 * @buf: store values retreived from registers
 */
static int adxl345_spi_read_block(struct device *dev, unsigned char reg,
                                  int count, void *buf)
{
        struct spi_device *spi = to_spi_device(dev); /* container_of to get spi_device struct */
        ssize_t status;

        /* add MB flags to the reading */
        reg = ADXL345_READMB_CMD(reg); /* set 7th and 8th bit ?*/

        /* write byte stored in reg(address with MB)
         * read count bytes (from successive addresses)
         * and store them to buf */
        /* send SPI bus a command byte of first reg to read(A0 to A5/ 6 bits)
         * set MB bit for multibyte reading and R bit for reading
         * read six register */
        status = spi_write_then_read(spi, &reg, 1, buf, count);

        return (status < 0) ? status : 0;
}

/* init bus operations and send it to adxl345_probe */
static const struct adxl345_bus_ops adxl345_spi_bops = {
	.bustype = BUS_SPI,
	.write = adxl345_spi_write,
	.read = adxl345_spi_read,
	.read_block = adxl345_spi_read_block,
};

static int adxl345_probe(struct device *dev, const struct adxl345_bus_ops *bops)
{
	/* private ds */
	struct adxl345 *ac;

	/* input device */
	struct input_dev *input_dev;

	/* we need a platform data structure */
	const struct adxl345_platform_data *pdata;

        int err;
        u8 revid;

	dev_info(dev, "Hello there! \n");
	/* allocate memory of new device structure */
	ac = devm_kzalloc(dev, sizeof(struct adxl345), GFP_KERNEL);
	if (!ac) {
                dev_err(dev, "Failed to kzalloc()\n")
		err = -ENOMEM;
                goto err_out;
        }

	/* allocated input_dev */
	input_dev = devm_input_allocate_device(dev);
	if (!input_dev){
                dev_err(dev, "Failed to input_allocate\n")
		err = -ENOMEM;
                goto err_out;
        }

	/* store platform data */
        pdata = &adxl345_default_init; /* const platform data */
        ac->pdata = *pdata;
        pdata = &ac->pdata; /* change address pointer */

        ac->input = input_dev; /* input device in private ds */
        ac->dev = dev; /* spi->dev */

        ac->bops = bops; /* spi operations */

        revid = AC_READ(ac, DEVID);
        dev->infor(dev, "DEVID: %d\n", revid);

        if (revid == ID_ADXL345)
                dev_info(dev, "Device found ADXL345\n");
        else {
                err = -ENODEV;
                dev_info(dev, "Device Not found \n");
                goto err_out;
        }
        snprintf(ac->phys, sizeof(ac->phys), "%s/input0", dev_name(dev));

	/* set device name */
	input_dev->name = "ADXL345 accelerometer";
	input_dev->phys = ac->phys;
	input_dev->dev.parent = dev;
	input_dev->id.product = ac->model;
	input_dev->id.bustype = bops->bustype;

	/* attach input device and private data structure */
	input_set_drvdata(input_dev, ac);

	set_bit(EV_KEY, input_dev->evbit); /* EV_KEY event type */
	set_bit(pdata->ev_code_tap[ADXL_X_AXIS], input_dev->keybit); /* 3 event code support event sent when a single tap interrupt is triggered */
	set_bit(pdata->ev_code_tap[ADXL_Y_AXIS], input_dev->keybit);
	set_bit(pdata->ev_code_tap[ADXL_Z_AXIS], input_dev->keybit);

	/* see if any of the axis are enabled and
	 * set interrupt mask
	 * only single tap is enabled */

	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))
	    ac->int_mask |= SINGLE_TAP;

	/* get gpio desc, set to input */
	ac->gpio = devm_gpiod_get_index(dev, ADXL345_GPIO_NAME, 0, GPIOD_IN);
        if (IS_ERR(ac->gpio)) {
                dev_err(dev, "gpio get index failed \n");
                err = PTR_ERR(ac->gpio); /* PTR to int */
                goto err_out;
        }

	/* get linux irq number of gpio interrupt */
	ac->irq = gpiod_to_irq(ac->gpio);
	if (ac->irq < 0) {
                dev_err(dev, "Failed to get irq from gpiod \n")
		err = ac->irq;
                goto err_out;
        }


	/*request threaded interrupt */
	err = devm_request_threaded_irq(input_dev->dev.parent, ac->irq, NULL,
				  adxl345_irq,
				  IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				  dev_name(dev), ac);
        if (err)
                goto err_out;

	/* create a group of sysfs enteries */
	err = sysfs_create_group(&dev->kobj, &adxl345_attr_group);
        if (err)
                goto err_out;

	/* register with input core */
	/* global until unregistered */
	err = input_register_device(input_dev);
        if (err)
                goto err_remove_attr;

	/* initilise adxl345 registers */

	/* tap threshold and duration */
	AC_WRITE(ac, THRES_TAP, pdata->tap_threshold);
	AC_WRITE(ac, DUR, pdata->tap_threshold);

	/* set tap axis */
	AC_WRITE(ac, TAP_AXES, pdata->tap_axis_control);

	/* data rate & axis reading power mode
	 * less or higher noise reducing power */
	AC_WRITE(ac, BW_RATE, RATE(ac->pdata.data_rate) |
		 (pdata->low_power_mode ? LOW_POWER : 0));

	/* 13-bit full resolution right justified */
	AC_WRITE(ac, DATA_FORMAT, pdata->data_range);

	/* set FIFO mode, no FIFO by default */
	AC_WRITE(ac, FIFO_CTL, FIFO_MODE(pdata->fifo_mode) |
		 SAMPLES(pdata->watermark));

	/* map interrupts to INT1 pin */
	AC_WRITE(ac, INT_MAP, 0);

	/* enable interrupts */
	AC_WRITE(ac, INT_ENABLE, ac->int_mask);

	AC_WRITE(ac, POWER_CTL, PCTL_MEASURE);

	return ac;

err_remove_attr:
        sysfs_remove_attr(&dev->kobj, &adxl345_attr_group);

        /* we will return an ERR PTR */
err_out:
        return ERR_PTR(err);
}

static int adxl345_spi_probe(struct spi_device *spi)
{
	/* private ds */
	struct adxl345 *ac;

	/* init driver and return private ds */
	ac = adxl345_probe(&spi->dev, &adxl345_spi_bops);

	/* attach private data structure */
	spi_set_drvdata(spi, ac);

	return 0;
}

static int adxl345_spi_remove(struct i2c_client *client)
{
	struct adxl345_dev *ioaccel;
	adxl345 = i2c_get_clientdata(client);
	input_unregister_polled_device(adxl345->polled_input);
	dev_info(&client->dev, "General Kenobi! \n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,adxl345"},
	{ },
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static const struct spi_device_id adxl345_ids[] = {
	{ .name = "adxl345", },
	{ }
};

MODULE_DEVICE_TABLE(spi, adxl345_ids);

static struct i2c_driver adxl345_driver = {
	.probe = adxl345_spi_probe,
	.remove = adxl345_spi_remove,
	.id_table = adxl345_ids,
	.driver = {
		.name = "adxl345",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	},
};

module_spi_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("i2c accelerometer driver");
