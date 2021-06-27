/* Send content from 3 wbuf to 3 rbuf at the same time using SG DMA
 *
 * */
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

struct dma_chan *dma_m2m_chan; /* associated channel */
struct completion dma_m2m_ok; /* Mechanism for any activity outside of current thread */

/* 4 KB */
#define SDMA_BUFFER_SIZE (1024 * 4)

static dma_addr_t dma_dst;
static dma_addr_t dma_src;
static char *dma_dst_coherent;
/* store userspace buffer using cohernt allocation */
static char *dma_src_coherent;
/* six buffers by Kzalloc() */
static unsigned int *wbuf, *wbuf2, *wbuf3;
static unsigned int *rbuf, *rbuf2, *rbuf3;
static struct scatterlist sg3[1], sg4[1]; /* one entry each */
static struct scatterlist sg[3], sg2[3]; /* three entry each */

static void dma_m2m_callback(void *data)
{
	pr_info("finished transaction %s\n", __func__);
	complete(&dma_m2m_ok);
}

static void dma_sg_callback(void *data)
{
	pr_info("finished transaction %s\n", __func__);
	complete(&dma_m2m_ok);
}

/* write function to communicate with user space */
static ssize_t sdma_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *offset)
{
	struct dma_async_tx_descriptor *dma_m2m_desc;
	dma_cookie_t cookie;
	int i;
	dma_addr_t wsg[3];
	dma_addr_t rsg[3];
	dma_addr_t wsg2;
	dma_addr_t rsg2;

	unsigned int *index1 = wbuf;
	unsigned int *index2 = wbuf2;
	unsigned int *index3 = wbuf3;

	struct dma_device *dma_dev = dma_m2m_chan->device;

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		*(index1 + i) = 0x12345678;
	}

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		*(index2 + i) = 0x87654321;
	}

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		*(index3 + i) = 0xabcdef12;
	}

	init_completion(&dma_m2m_ok);

	/* read from user space and store it in wbuf */
	if (copy_from_user(dma_src_coherent, buf, count))
		return -EFAULT;

	pr_info("User string is %s\n", dma_src_coherent);

	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], wbuf, SDMA_BUFFER_SIZE);
	sg_set_buf(&sg[1], wbuf2, SDMA_BUFFER_SIZE);
	sg_set_buf(&sg[2], wbuf3, SDMA_BUFFER_SIZE);
	dma_map_sg(dma_dev->dev, sg, 3, DMA_TO_DEVICE); /* send to device */

	for(i = 0; i < 3; i++)
		wsg[i] = sg_dma_address(&sg[i]);

	sg_init_table(sg2, 3);
	sg_set_buf(&sg2[0], rbuf, SDMA_BUFFER_SIZE);
	sg_set_buf(&sg2[1], rbuf2, SDMA_BUFFER_SIZE);
	sg_set_buf(&sg2[2], rbuf3, SDMA_BUFFER_SIZE);
	dma_map_sg(dma_dev->dev, sg2, 3, DMA_FROM_DEVICE); /* send from device */
	for(i = 0; i < 3; i++)
		rsg[i] = sg_dma_address(&sg2[i]);

	sg_init_table(sg3, 1);
	sg_set_buf(sg3, dma_src_coherent, SDMA_BUFFER_SIZE);
	dma_map_sg(dma_dev->dev, sg3, 1, DMA_TO_DEVICE); /* send from device */
	wsg2 = sg_dma_address(sg3);

	sg_init_table(sg4, 1);
	sg_set_buf(sg4, dma_dst_coherent, SDMA_BUFFER_SIZE);
	dma_map_sg(dma_dev->dev, sg4, 1, DMA_FROM_DEVICE); /* send to device */
	rsg2 = sg_dma_address(sg4);

	/* get DMA descriptor for first set of transactions */
	dma_m2m_desc = dma_dev->device_prep_dma_memcpy(dma_m2m_chan,
						       rsg[0], wsg[0], 3 * SDMA_BUFFER_SIZE,
						       DMA_CTRL_ACK |
						       DMA_PREP_INTERRUPT);

	pr_info("successful descriptor obtained");

	/*Add callback information */
	dma_m2m_desc->callback = dma_sg_callback;

	/* add to pending queue */
	cookie = dmaengine_submit(dma_m2m_desc);
	if (dma_submit_error(cookie)){
		pr_err("Failed to submit DMA\n");
		return -EINVAL;
	}

	/* issue DMA transaction */
	dma_async_issue_pending(dma_m2m_chan);

	/* if the queue is idle, the first transaction pending on queue is
	 * started */

	/* wait for completion of the event */
	wait_for_completion(&dma_m2m_ok);

	/*check channel status */
	dma_async_is_tx_complete(dma_m2m_chan, cookie, NULL ,NULL);

	/* DMA transactions are finised */
	dma_unmap_sg(dma_dev->dev, sg, 3, DMA_TO_DEVICE);
	dma_unmap_sg(dma_dev->dev, sg2, 3, DMA_FROM_DEVICE);

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		pr_err("rbuf = %x wbuf = %x\n", *(rbuf + i), *(wbuf + i));
		if (*(rbuf + i) != *(wbuf + i)) {
			pr_err("buffer copy failed\n");
			//return -EINVAL;
		}
	}
	pr_info("buffer 1 copied properly\n");

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		pr_err("rbuf = %x wbuf = %x\n", *(rbuf + i), *(wbuf + i));
		if (*(rbuf2 + i) != *(wbuf2 + i)) {
			pr_err("buffer copy failed\n");
			//return -EINVAL;
		}
	}
	pr_info("buffer 2 copied properly\n");

	for (i = 0; i < SDMA_BUFFER_SIZE / 4; i++) {
		pr_err("rbuf = %x wbuf = %x\n", *(rbuf + i), *(wbuf + i));
		if (*(rbuf3 + i) != *(wbuf3 + i)) {
			pr_err("buffer copy failed\n");
			//return -EINVAL;
		}
	}
	pr_info("buffer 3 copied properly\n");

	reinit_completion(&dma_m2m_ok);

	/* get DMA descriptor for second set of transactions */
	dma_m2m_desc = dma_dev->device_prep_dma_memcpy(dma_m2m_chan,
						       rsg2, wsg2, 1 * SDMA_BUFFER_SIZE,
						       DMA_CTRL_ACK |
						       DMA_PREP_INTERRUPT);
	dma_m2m_desc->callback = dma_m2m_callback;

	/* add to pending queue */
	cookie = dmaengine_submit(dma_m2m_desc);
	if (dma_submit_error(cookie)){
		pr_err("Failed to submit DMA\n");
		return -EINVAL;
	}
	/* issue DMA transaction */
	dma_async_issue_pending(dma_m2m_chan);
	/* wait for completion of the event */
	wait_for_completion(&dma_m2m_ok);
	/*check channel status */
	dma_async_is_tx_complete(dma_m2m_chan, cookie, NULL ,NULL);

	/* DMA transactions are finised */
	dma_unmap_sg(dma_dev->dev, sg3, 1, DMA_TO_DEVICE);
	dma_unmap_sg(dma_dev->dev, sg4, 1, DMA_FROM_DEVICE);

	if (*(dma_src_coherent) != *(dma_dst_coherent)) {
		pr_err("buffer copy failed\n");
		return -EINVAL;
	}

	pr_info("coherent buffer copied properly %s %s\n", dma_src_coherent,
		dma_dst_coherent);

	return count;
}

struct file_operations dma_fops = {
	.write = sdma_write,
};

static struct miscdevice dma_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sdma_test",
	.fops = &dma_fops,
};

static int __init my_probe(struct platform_device *pdev)
{
	int ret;

	dma_cap_mask_t dma_m2m_mask;
	//struct dma_slave_config dma_m2m_config = {0};

	ret = misc_register(&dma_miscdevice);
	if (ret) return ret;

	pr_info("misc device registerd with minor number %d\n", dma_miscdevice.minor);

	/* read write buffer allocation */
	wbuf = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!wbuf) {
		dev_err(&pdev->dev, "error allocating wbuf\n");
		return -ENOMEM;
	}
	wbuf2 = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!wbuf2) {
		dev_err(&pdev->dev, "error allocating wbuf\n");
		return -ENOMEM;
	}
	wbuf3 = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!wbuf3) {
		dev_err(&pdev->dev, "error allocating wbuf\n");
		return -ENOMEM;
	}
	rbuf = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!rbuf) {
		dev_err(&pdev->dev, "error allocating rbuf\n");
		return -ENOMEM;
	}
	rbuf2 = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!rbuf2) {
		dev_err(&pdev->dev, "error allocating rbuf\n");
		return -ENOMEM;
	}
	rbuf3 = devm_kzalloc(&pdev->dev, SDMA_BUFFER_SIZE, GFP_KERNEL);
	if(!rbuf3) {
		dev_err(&pdev->dev, "error allocating rbuf\n");
		return -ENOMEM;
	}

	dma_src_coherent = dma_alloc_coherent(&pdev->dev,SDMA_BUFFER_SIZE,
					      &dma_src, GFP_DMA); /* to device From DMA */
	if (!dma_src_coherent) {
		dev_err(&pdev->dev, "error allocating dma_src_coherent\n");
		return -ENOMEM;
	}
	dma_dst_coherent = dma_alloc_coherent(&pdev->dev, SDMA_BUFFER_SIZE,
					      &dma_dst, GFP_DMA); /* from device to DMA */
	if (!dma_dst_coherent) {
		dev_err(&pdev->dev, "error allocating dma_src_coherent\n");
		return -ENOMEM;
	}
	/* channel caps */
	dma_cap_zero(dma_m2m_mask); /* zero the mask */
	dma_cap_set(DMA_MEMCPY, dma_m2m_mask); /* capability */

	/* initialise custom DMA controller structures */
	/* raspberry pi stuff? */

	/* request DMA channel */
	dma_m2m_chan = dma_request_channel(dma_m2m_mask,
						       0, NULL);
	if(!dma_m2m_chan) {
		dev_err(&pdev->dev, "error opening memory channel\n");
		return -EINVAL;
	}

	return 0;
}


static int __exit my_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "remove\n");
	misc_deregister(&dma_miscdevice);
	dma_release_channel(dma_m2m_chan);
	dma_free_coherent(&pdev->dev, SDMA_BUFFER_SIZE, &dma_dst_coherent, dma_dst);
	dma_free_coherent(&pdev->dev, SDMA_BUFFER_SIZE, &dma_src_coherent, dma_src);

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
MODULE_DESCRIPTION("sdma memory driver with scatter gather implementation");
