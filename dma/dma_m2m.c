#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>

/* this needs to  be changed for function specific to raspbery pi */
/* no specific dma file for raspberry pi */

/* dmaengine provides dma slave channel allocation, slave and controller
 * specific parameters, transaction descriptor, txn submission, pendin requests
 * and callback notification.
 */

#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of_device.h>

/* private strurcture for DMA specific information*/

struct dma_private {
	struct miscdevice dma_misc_device; /* so a character device */
	struct device *dev;
	char *wbuf; /* allocated buffers */
	char *rbuf;
	struct dma_chan *dma_m2m_chan; /* associated channel */
	struct completion dma_m2m_ok; /* Mechanism for any activity outside of current thread */
	/* this activity can be new kernel thread, request to a process or DMA
	 * transfer */
};

/* 4 KB */
#define SDMA_BUFFER_SIZE (1024 * 4)

static void dma_m2m_callback(void *data)
{
	struct dma_private *dma_priv = data;
	dev_info(dma_priv->dev, "finished transaction %s\n", __func__);
	complete(&dma_priv->dma_m2m_ok);

	dev_info(dma_priv->dev, "wbuf is %s\n", dma_priv->wbuf);
	dev_info(dma_priv->dev, "rbuf is %s\n", dma_priv->rbuf);
}

/* write function to communicate with user space */
static ssize_t sdma_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *offset)
{
	/* get data from userspace and write them to wbuf */
	/* get dma address using dma_map_single */
	struct dma_async_tx_descriptor *dma_m2m_desc;
	struct dma_device *dma_dev;
	struct dma_private *dma_priv;
	//struct device *chan_dev;
	dma_cookie_t cookie;
	dma_addr_t dma_src;
	dma_addr_t dma_dst;

	/* get private structure */
	dma_priv = container_of(file->private_data, struct dma_private,
				dma_misc_device);

	/* channel device */
	dma_dev = dma_priv->dma_m2m_chan->device;
	//chan_dev = dma_priv->dma_m2m_chan->device->dev;

	/* read from user space and store it in wbuf */
	if (copy_from_user(dma_priv->wbuf, buf, count))
		return -EFAULT;

	/* use virtual address(wbuf) to get DMA address */
	dma_src = dma_map_single(dma_priv->dev, dma_priv->wbuf,
				 SDMA_BUFFER_SIZE, DMA_TO_DEVICE); /* to device From DMA */
	dma_dst = dma_map_single(dma_priv->dev, dma_priv->rbuf,
				 SDMA_BUFFER_SIZE, DMA_FROM_DEVICE); /* from device to DMA */

	/* get DMA descriptor for transactions */
	dma_m2m_desc = dma_dev->device_prep_dma_memcpy(dma_priv->dma_m2m_chan,
						       dma_dst, dma_src,
						       SDMA_BUFFER_SIZE,
						       DMA_CTRL_ACK |
						       DMA_PREP_INTERRUPT);

	dev_info(dma_priv->dev, "successful descriptor obtained");

	/*Add callback information */
	dma_m2m_desc->callback = dma_m2m_callback;
	dma_m2m_desc->callback_param = dma_priv;

	/* completion signaling stuff */
	init_completion(&dma_priv->dma_m2m_ok);

	/* add to pending queue */
	cookie = dmaengine_submit(dma_m2m_desc);
	if (dma_submit_error(cookie)){
		dev_err(dma_priv->dev, "Failed to submit DMA\n");
		return -EINVAL;
	}
	
	/* issue DMA transaction */
	dma_async_issue_pending(dma_priv->dma_m2m_chan);

	/* if the queue is idle, the first transaction pending on queue is
	 * started */

	/* wait for completion of the event */
	wait_for_completion(&dma_priv->dma_m2m_ok);

	/*check channel status */
	dma_async_is_tx_complete(dma_priv->dma_m2m_chan, cookie, NULL ,NULL);

	dev_info(dma_priv->dev, "rbuf = %s\n", dma_priv->rbuf);

	/* DMA transactions are finised */
	dma_unmap_single(dma_priv->dev, dma_src,
			 SDMA_BUFFER_SIZE, DMA_TO_DEVICE);
	dma_unmap_single(dma_priv->dev, dma_dst,
			 SDMA_BUFFER_SIZE, DMA_FROM_DEVICE);

	if (*(dma_priv->rbuf) != *(dma_priv->wbuf)) {
		dev_err(dma_priv->dev, "buffer copy failed\n");
		return -EINVAL;
	}

	return count;
}

struct file_operations dma_fops = {
	.write = sdma_write,
};

static int __init my_probe(struct platform_device *pdev)
{
	/* custom data structure for raspberry pi */
	/* init custom DS */
	/* Allocate read/write buffer */
	/* request DMA channel from DAM engine */
	/* dma_m2m_mask holds chan capabilites */
	/* dma_m2m_filter helps select channel among multiple channels */
	/* configure channel by filling dma_slave_config with proper values
	 * this can include direction, addresses, bus width, DMA burst lengths*/
	/* for more info embed dma_slave_config into controller specific
	 * structure */
	int ret;

	struct dma_private *dma_device;

	dma_cap_mask_t dma_m2m_mask;
	//struct dma_slave_config dma_m2m_config = {0};

	/* allocate space for priv structure */
	dma_device = devm_kzalloc(&pdev->dev, sizeof(struct dma_private),
				  GFP_KERNEL); /* zeroed virtual memory */

	dma_device->dma_misc_device.minor = MISC_DYNAMIC_MINOR;
	dma_device->dma_misc_device.name = "sdma_test";
	dma_device->dma_misc_device.fops = &dma_fops;

	/* copy device in private struct */
	dma_device->dev = &pdev->dev;

	/* read write buffer allocation */
	dma_device->wbuf = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!dma_device->wbuf) {
		dev_err(&pdev->dev, "error allocating wbuf\n");
		return -ENOMEM;
	}
	dma_device->rbuf = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!dma_device->rbuf) {
		dev_err(&pdev->dev, "error allocating rbuf\n");
		return -ENOMEM;
	}

	/* channel caps */
	dma_cap_zero(dma_m2m_mask); /* zero the mask */
	dma_cap_set(DMA_MEMCPY, dma_m2m_mask); /* capability */

	/* initialise custom DMA controller structures */
	/* raspberry pi stuff? */

	/* request DMA channel */
	dma_device->dma_m2m_chan = dma_request_channel(dma_m2m_mask,
						       0, NULL);
	if(!dma_device->dma_m2m_chan) {
		dev_err(&pdev->dev, "error opening memory channel\n");
		return -EINVAL;
	}


	/* set slave and controller specific parameters */
	/*dma_m2m_config.direction = DMA_MEM_TO_MEM;
	dma_m2m_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmaengine_slave_config(dma_device->dma_m2m_chan, &dma_m2m_config);*/

	ret = misc_register(&dma_device->dma_misc_device);
	if (ret) return ret;

	platform_set_drvdata(pdev, dma_device);

	return 0;

}


static int __exit my_remove(struct platform_device *pdev)
{
	struct dma_private *dma_device = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "remove\n");
	misc_deregister(&dma_device->dma_misc_device);
	dma_release_channel(dma_device->dma_m2m_chan);

	return 0;
}

static const struct of_device_id my_of_ids [] = {
	{ .compatible = "arrow,sdma_m2m"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver my_platform_driver = {
	.probe = my_probe,
	.remove = my_remove,
	.driver = {
		.name = "sdma_m2m",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};

static int demo_init(void)
{
	int ret;
	pr_info("init function\n");
	ret = platform_driver_register(&my_platform_driver);
	if (ret) {
		pr_err("platform driver returned %d\n", ret);
		return ret;
	}
	return 0;
}

static void demo_exit(void)
{
	pr_info("exit function\n");
	platform_driver_unregister(&my_platform_driver);
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anuz");
MODULE_DESCRIPTION("sdma memory driver implementation");
