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

	struct dma_private *dma_device;

	dma_cap_mask_t dma_m2m_mask;
	struct dma_slave_config dma_m2m_config = {0};

	/* allocate space for priv structure */
	dma_device = devm_kzalloc(&pdev->dev, sizeof(struct dma_slave_config),
				  GFP_KERNEL); /* zeroed virtual memory */

	dma_device->dma_misc_device.minor = MISC_DYNAMIC_MINOR;
	dma_device->dma_misc_device.name = "sdma_test";
	dma_device->dma_misc_device.fops = &dma_fops;

	/* copy device in private struct */
	dma_device->dev = &pdev->dev;

	/* read write buffer allocation */
	dma_device->wbuf = devm_kzalloc(&pdev->dev, SDMA_BUF_SIZE, GFP_KERNEL);
	dma_device->rbuf = devm_kzalloc(&pdev->dev, SDMA_BUF_SIZE, GFP_KERNEL);

	/* channel caps */
	dma_cap_zero(dma_m2m_mask); /* zero the mask */
	dma_cap_set(DMA_MEMCPY, dma_m2m_mask); /* capability */

	/* initialise custom DMA controller structures */
	/* raspberry pi stuff? */

	/* request DMA channel */
	dam_device->dma_m2m_chan = dma_request_channel(dma_m2m_mask,
						       dma_m2m_filter,
						       &???);

	/* set slave and controller specific parameters */
	dma_m2m_config.direction = DMA_MEM_TO_MEM;
	dma_m2m_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmaengine_slave_config(dma_device->dma_m2m_chan, &dma_m2m_config);

	ret = misc_register(&dma_device->dma_misc_device);
	platform_set_drvdata(pdev, dma_device);

	return 0;

}

/* write function to communicate with user space */
static ssize_t sdma_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *offset)
{
	/* get data from userspace and write them to wbuf */
	/* get dma address using dma_map_single */

}
static bool dma_m2m_filter(struct dma_chan *chan, void *param)
{
	chan->private = param;
	return true;
}

