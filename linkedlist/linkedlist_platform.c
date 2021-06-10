#include <linux/module.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/of_device.h>

static int BlockNumber = 10;
static int BlockSize = 5;
static int size_to_read = 0;
static int node_count = 1;
static int cnt = 0;

struct dnode {
	char *buffer;
	struct dnode *next;
};

struct lnode {
	struct dnode *head;
	struct dnode *cur_write_node;
	struct dnode *cur_read_node;
	int cur_read_offset;
	int cur_write_offset;
};

static struct lnode newlist;

static int createlist(struct platform_device *device)
{
	struct dnode *new_node;
	struct dnode *head_node;
	struct dnode *prev_node;
	int i;

	/* new node */
	new_node = devm_kmalloc(&device->dev, sizeof(struct dnode), GFP_KERNEL);
	if (new_node)
		new_node->buffer = devm_kmalloc(&device->dev,
						BlockSize * sizeof(char),
						GFP_KERNEL);
	if (!new_node || !new_node->buffer)
		return -ENOMEM;

	new_node->next = NULL;

	newlist.head = new_node;
	head_node = new_node;
	prev_node = new_node;

	for (i = 1; i < BlockNumber; i++) {
		new_node = devm_kmalloc(&device->dev, sizeof(struct dnode),
					GFP_KERNEL);
		if (new_node)
			new_node->buffer = devm_kmalloc(&device->dev,
							BlockSize * sizeof(char),
							GFP_KERNEL);
		if (!new_node || !new_node->buffer)
			return -ENOMEM;

		new_node->next = NULL;
		prev_node->next = new_node;
		prev_node = new_node;

	}
	new_node->next = head_node;
	newlist.cur_read_node = head_node;
	newlist.cur_write_node = head_node;
	newlist.cur_read_offset = 0;
	newlist.cur_write_offset = 0;

	return 0;
}

static ssize_t linkedlist_write(struct file *file, const char __user *buf,
				size_t size, loff_t *offset)
{
	int size_to_copy;
	pr_info("node_number %d\n", node_count);

	if((*(offset) == 0) || (node_count == 1))
		size_to_read += size;

	if (size < BlockSize - newlist.cur_write_offset)
		size_to_copy = size;
	else
		size_to_copy = BlockSize - newlist.cur_write_offset;

	if (copy_from_user(newlist.cur_write_node->buffer +
			   newlist.cur_write_offset, buf, size_to_copy))
		return  -EFAULT;

	*(offset) += size_to_copy;
	newlist.cur_write_offset += size_to_copy;

	if (newlist.cur_write_offset == BlockSize) {
		newlist.cur_write_node = newlist.cur_write_node->next;
		newlist.cur_write_offset = 0;
		node_count++;

		if (node_count > BlockNumber) {
			newlist.cur_read_node = newlist.cur_write_node;
			newlist.cur_read_offset = 0;
			node_count = 0;
			cnt = 0;
			size_to_read = 0;
		}
	}

	return size_to_copy;

}

static ssize_t linkedlist_read(struct file *file, char __user *buf,
			       size_t count, loff_t *offset)
{
        int size_to_copy;
	int read_value;

	pr_info("read %d\n", node_count);

	read_value = size_to_read - (BlockSize * cnt);
	if (*(offset) < size_to_read) {
		if (read_value < BlockSize - newlist.cur_read_offset)
			size_to_copy = read_value;
		else
			size_to_copy = BlockSize - newlist.cur_read_offset;
		if (copy_to_user(buf, newlist.cur_write_node->buffer +
				   newlist.cur_read_offset, size_to_copy))
			return  -EFAULT;
		newlist.cur_read_offset += size_to_copy;
		*(offset) += size_to_copy;
	
		if (newlist.cur_read_offset == BlockSize) {
			cnt++;
			newlist.cur_read_node = newlist.cur_read_node->next;
			newlist.cur_read_offset = 0;
		}
		return size_to_copy;
	} else {
		msleep(250);
		newlist.cur_read_node = newlist.head;
		newlist.cur_write_node = newlist.head;
		newlist.cur_read_offset = 0;
		newlist.cur_write_offset = 0;
		node_count = 1;
		cnt = 0;
		size_to_read = 0;
		return 0;
	}
}
static int linkedlist_open(struct inode *inode, struct file *file)
{

	pr_info("linked list device open \n");
	return 0;
}

static int linkedlist_close(struct inode *inode, struct file *file)
{

	pr_info("linked list device closed \n");
	return 0;
}

static const struct file_operations linkedlist_fops = {
	.owner = THIS_MODULE,
	.open = linkedlist_open,
	.write = linkedlist_write,
	.read = linkedlist_read,
	.release = linkedlist_close,
};

static struct  miscdevice linkedlist_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "linkedlist",
	.fops = &linkedlist_fops,
};

static int linkedlist_probe(struct platform_device *pdev)
{
	int ret;

	pr_info("linked list driver probe\n");
	createlist(pdev);
	ret = misc_register(&linkedlist_miscdevice);
	if (ret) {
		pr_err("failed to register linkedlist misc device");
		return ret;
	}
	return 0;
}

static int linkedlist_remove(struct platform_device *pdev)
{
	misc_deregister(&linkedlist_miscdevice);
	return 0;

}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "arrow,memory"},
	{},
};

MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver linkedlist_driver = {
	.probe = linkedlist_probe,
	.remove = linkedlist_remove,
	.driver = {
		.name = "memory",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}

};
/* init function */
static int linkedlist_init(void)
{
	int ret;
	pr_info("linked list driver init\n");
	ret = platform_driver_register(&linkedlist_driver);
	if (ret) {
		pr_err("failed to register linkedlist platform driver");
		return ret;
	}
	return 0;
}

static void linkedlist_cleanup(void)
{
	pr_info("linked list driver cleanup\n");
	platform_driver_unregister(&linkedlist_driver);
}

module_init(linkedlist_init);
module_exit(linkedlist_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("LINKED LIST  MODULE");
