/*
 ** Author - anuz
** This file is distibuted under GPLv2
**/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/sched.h>
#include <linux/kernel.h> /* printk */
#include <linux/slab.h> /* kmalloc */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h> /* mark_bh */


#include <linux/in.h> /*inet implementation of tcp/ip */
#include <linux/netdevice.h> /*inet implementation of tcp/ip */
#include <linux/etherdevice.h> /* eth_type_trans: get protocol value or packet type */
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>

#include "snull.h"

#include <linux/in6.h>
#include <asm/checksum.h>

/* packet as represented in snull */
struct snull_packet {
	struct snull_packet *next;
	struct net_device *dev;
	int datalen;
	u8 data[ETH_DATA_LEN];
};

/* this structure is used to pass packet in and out, so there is place for packet */
struct snull_priv {
	struct net_device_stats stats; /* interface statisitics */
	int status;
	struct snull_packet *ppool;
	struct snull_packet *rx_queue; /* List of incoming packets */
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct sk_buff *skb;
	spinlock_t lock;
	struct net_device *dev;
	struct napi_struct napi;
};

/* when this is enabled every n seconds a lockup happens */
static int lockup = 0;
module_param(lockup, int, 0);

static int timeout = SNULL_TIMEOUT;
module_param(timeout, int, 0);


/* do we use napi or not ? */
static int use_napi = 0;
module_param(use_napi, int, 0);

int pool_size = 8;
module_param(pool_size, int, 0);

/* The devices */
struct net_device *snull_devs[2];

/* interrupt function ptr */
static void (*snull_interrupt)(int, void *, struct pt_regs *);


/* snull ioctls */
int snull_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	PDEBUG("ioctl\n");
	return 0;
}

/* statistical information in snull */
struct net_device_stats *snull_stats(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

/* fill ETH header since there is no APR function */
int snull_header_cache(const struct neighbour *neigh, struct hh_cache *hh,
		       __be16 type)
{
	int ret;
	printk("header cache");

	ret = eth_header_cache(neigh, hh, type);
	if ( ret == 0) {
		struct ethhdr *eth;
		struct net_device *dev;

		eth = (struct ethhdr *)(((u8 *)hh->hh_data) +
					HH_DATA_OFF(sizeof(*eth)));
		dev = neigh->dev;

		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		memcpy(eth->h_dest, dev->dev_addr, dev->addr_len);
		eth->h_dest[ETH_ALEN - 1] ^= 0x01; /* flip the bit in last bits of first octet */
	}

	return ret;
}

/* so we are allocating a pool of memory for packets */
static void snull_setup_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	int i;
	struct snull_packet *pkt;

	priv->ppool = NULL;

	for (i = 0; i < pool_size; i++) {
		pkt = kmalloc(sizeof(struct snull_packet), GFP_KERNEL);
		if (!pkt) {
			printk (KERN_NOTICE "Ran out of memory for pool allocations \n");
			return;
		}
		pkt->dev = dev;
		pkt->next = priv->ppool; /* so pkt->next is ppool and ppool is pkt */
		priv->ppool = pkt; /* queue packet at the back */
	}

}

/*get the pool back */
static void snull_teardown_pool(struct net_device *dev)
{
	struct snull_priv *priv= netdev_priv(dev);
	struct snull_packet *pkt;

	while ((pkt = priv->ppool)) {
		priv->ppool = pkt->next;
		kfree(pkt);
		/* FIXME - infligh packets */
	}
}

/* managing trasmitted packets */
static struct snull_packet *snull_get_tx_buffer(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	unsigned long flags;
	struct snull_packet *pkt;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->ppool;
	priv->ppool = pkt->next;
	if (priv->ppool == NULL) {
		printk(KERN_WARNING "Pool empty\n");
		netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}


static void snull_release_buffer(struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(pkt->dev);

	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->ppool;
	priv->ppool = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
	if (netif_queue_stopped(pkt->dev) && pkt->next == NULL)
		netif_wake_queue(pkt->dev);
}

/* add packet to the end of the queue?*/
static void snull_enqueue_buf(struct net_device *dev, struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);

	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->rx_queue; /* FIXME - misorders packets */
	priv->rx_queue = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
}

/* remove packet from the end of the queue?*/
static struct snull_packet *snull_dequeue_buf(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->rx_queue;
	if (pkt)
		priv->rx_queue = pkt->next;
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

/* enable disable recieve interrupts */
static void snull_rx_ints(struct net_device *dev, int enable)
{
	struct snull_priv *priv = netdev_priv(dev);
	priv->rx_int_enabled = enable;
}

/* Configuration changes as done through ifconfig */
static int snull_config(struct net_device *dev, struct ifmap *map)
{
	/* interface is up, cannot change config*/
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (map->base_addr != dev->base_addr) {
		printk(KERN_WARNING "snull: cannot change I/O address\n");
		return -EOPNOTSUPP;
	}

	if (map->irq != dev->irq) {
		dev->irq = map->irq;
		/* request_irq() is delayed to open-time */
	}

	/* ignore other fields */
	return 0;
}

/* Change hardware header */
static int snull_header(struct sk_buff *skb, struct net_device *dev,
		unsigned short type, const void *daddr, const void *saddr,
		unsigned int len)
{
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb, ETH_HLEN); /* add data at the beginning of the packet */
	eth->h_proto = htons(type);
	memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len); /* make sure src address is not null */
	memcpy(eth->h_dest, daddr  ? daddr : dev->dev_addr, dev->addr_len); /* make sure src address is not null */
	eth->h_dest[ETH_ALEN - 1] ^= 0x01; /*change destination by flipping the second last byte's last bit */
	return dev->hard_header_len;
}

/* NAPI driven polling rx funtion */
/* budget: maximum packet allowed to pass to the kernel */
/* the prototype of this function has changed */
static int snull_poll(struct napi_struct *napi, int budget)
{
	int npackets = 0;
	struct sk_buff *skb;
	struct snull_priv *priv = container_of(napi, struct snull_priv, napi);
	struct net_device *dev = priv->dev;
	struct snull_packet *pkt;

	/* if there is data in rx_queue */
	while (npackets < budget && priv->rx_queue ) {
		pkt = snull_dequeue_buf(dev);
		skb = dev_alloc_skb(pkt->datalen + 2);
		if (!skb) {
			if (printk_ratelimit()) {
				printk(KERN_NOTICE "snull_packet dropped");
			}
			priv->stats.rx_dropped++;
			snull_release_buffer(pkt);
			continue;
		}

		skb_reserve(skb, 2); /* align ip on 16B boundary */
		memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen); /* put at the end */
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		netif_receive_skb(skb); /* send to kernel */

		/* stats */
		npackets++;
		priv->stats.rx_packets++;
		priv->stats.rx_bytes += pkt->datalen;
		snull_release_buffer(pkt);
	}

	/* we are done here, all packet processed, enable interrupts */
	if (!priv->rx_queue) {
		napi_complete(napi); /* turn off polling */
		snull_rx_ints(dev, 1); /* enable interrupts */
		return 0;
	}
	/* queue is not empty, more packets to be processed */
	return npackets;
}

/* send packet to upper layers with any additional information
 * This function is independent of source of data packet and len
 */
void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	struct sk_buff *skb;
	struct snull_priv *priv = netdev_priv(dev);

	/* The packet is recieved from the Tx medium
 	* build the skb around it.
 	*/
	skb = dev_alloc_skb(pkt->datalen + 2);
	if (!skb) {
		if (printk_ratelimit()) /* to make sure console is not overwhelmed with debug messages */
			printk(KERN_NOTICE "snull rx: low on mem =packet dropped \n");
		priv->stats.rx_dropped++;
		goto out;
	}
	skb_reserve(skb, 2);
	memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen); /* skb_put() updates the end of the data pointer in the buffer and returns pointer to it */

	/* write metadata, and then pass to recieve level */
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev); /* so upper layer knows what kind of packet to expect */
	skb->ip_summed = CHECKSUM_UNNECESSARY; /* just ignore it */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += pkt->datalen;
	netif_rx(skb); /* hand over packet to upper layers */

out:
	return;
}

/* Interrupt handler for this interface */
/* what is pt_regs ofcourse these are copies of stack registers. */
static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;

	/* check if it is still interrupting device pointer
 	* assign it to struct device dev
 	*/
	struct net_device *dev = (struct net_device *)dev_id;
	if (!dev)
		return;

	priv = netdev_priv(dev);
	/* put a ring on it */
	spin_lock(&priv->lock);

	/* real devices will use some sort of I/O instructions */
	statusword = priv->status;
	priv->status = 0;

	if (statusword & SNULL_RX_INTR) {
		/* a packet is recieved send it to snull_rx */
		pkt = priv->rx_queue;
		if (pkt) {
			priv->rx_queue = pkt->next; /* move the queue */
			snull_rx(dev, pkt);
		}
	}

	if (statusword & SNULL_TX_INTR) {
		/* a packet is fully sent */
		/* free the socket buffer */
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);

	if (pkt) snull_release_buffer(pkt); /* free the packet */
	return;
}

/* printing details inside header ? */
void printk_ip_packet(struct iphdr *ih, int dir)
{
	uint32_t temp1 = ntohl(ih->saddr);
	uint32_t temp2 = ntohl(ih->daddr);

	unsigned char *saddr = (unsigned char *)&temp1;
	unsigned char *daddr = (unsigned char *)&temp2;

	if (dir) {
		printk("%d.%d.%d.%d:%05d\n -> %d.%d.%d.%d:%05d\n",
			saddr[3], saddr[2], saddr[1], saddr[0],
			ntohs(((struct tcphdr *)(ih + 1))->source),
			daddr[3], daddr[2], daddr[1], daddr[0],
			ntohs(((struct tcphdr *)(ih + 1))->dest));

	} else {
		printk("%d.%d.%d.%d:%05d\n -> %d.%d.%d.%d:%05d\n",
			daddr[3], daddr[2], daddr[1], daddr[0],
			ntohs(((struct tcphdr *)(ih + 1))->dest),
			saddr[3], saddr[2], saddr[1], saddr[0],
			ntohs(((struct tcphdr *)(ih + 1))->source));
	}
}


/* transmit a low level packet */
void snull_hw_tx(char *data, int len, struct net_device *dev)
{
	/* this is device dependent function
 	* it loops back packet to the other snull interface
 	* deals with hw details
 	*/
	struct iphdr *ih;
	struct net_device *dest;
	struct snull_priv *priv;
	u32 *saddr, *daddr;
	struct snull_packet *tx_buffer;
	int dev_num = (dev == snull_devs[0]) ? 0 : 1;

	printk(KERN_INFO "snull_hw device :%d", dev_num);

	if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
		printk("packet too small for all headers to fit size of %d\n", len);
		return;
	}

#ifdef SNULL_DEBUG
	{
		int i;
		printk(KERN_DEBUG "len is %d\n", len);
		for (i = 0; i < len; i++)
			printk(" %02x", data[i] & 0xff);
		printk("\n");
	}
#endif

	ih = (struct iphdr *)(data + sizeof(struct ethhdr));
	saddr = &ih->saddr;
	daddr = &ih->daddr;

	printk_ip_packet(ih, dev_num);

	((u8 *)saddr)[2] ^= 1; /* flip the third octet last bit */
	((u8 *)daddr)[2] ^= 1; /* flip the third octet last bit */

	ih->check = 0; /* rebuild the checksum */
	ih->check = ip_fast_csum((unsigned char *)ih, ih->ihl);

	/* packet is ready for transmission.
 	* simulate a recieve interrupt on the twin device,
 	* then a transmission-done  on transmitting device
 	*/

	dest = snull_devs[!dev_num];
	priv = netdev_priv(dest); /* we want priv data structure of dest */
	tx_buffer = snull_get_tx_buffer(dev);
	tx_buffer->datalen = len;
	memcpy(tx_buffer->data, data, len);
	snull_enqueue_buf(dest, tx_buffer);
	if (priv->rx_int_enabled) {
		priv->status |= SNULL_RX_INTR;
		snull_interrupt(0, dest, NULL);
	}

	priv = netdev_priv(dev);
	priv->tx_packetlen = len;
	priv->tx_packetdata = data;
	priv->status |= SNULL_TX_INTR;
	if (lockup && ((priv->stats.tx_packets + 1) % lockup) == 0) {
		/* simulate a dropped transmit interrupt */
		netif_stop_queue(dev);
		PDEBUG("simulating lockup at %ld, txp %ld\n", jiffies,
			(unsigned long)priv->stats.tx_packets);

	} else
		snull_interrupt(0, dev, NULL);


}

/* NAPI ISR handler */
static void snull_napi_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;

	/* device pointer is shared handler*/
	struct net_device *dev = (struct net_device *)dev_id;
	if (!dev)
		return;

	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	statusword = priv->status;
	priv->status = 0;

	if (statusword & SNULL_RX_INTR) {
		snull_rx_ints(dev, 0); /* disable interrupts */
		napi_schedule(&priv->napi); /* call the polling fn  at some time in future*/
	}

	if (statusword & SNULL_TX_INTR) {
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	spin_unlock(&priv->lock);
	return;
}

void snull_tx_timeout(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	//PDEBUG("Transmit timeout at %ld latency %ld", jiffies, jiffies - dev->trans_start);
	PDEBUG("Transmit timeout at %ld latency %ld", jiffies, jiffies -
	       dev_trans_start(dev));
	/* simulate a transmission interruption for this to start */
	priv->status = SNULL_TX_INTR;
	snull_interrupt(0, dev, NULL); /* fill the missing interrupt */
	priv->stats.tx_errors++; /* mark errors */
	netif_wake_queue(dev); /* restart the queue */
	return;
}

/* transmission method, skb contains packet data, which will have hardware header already in there */
static int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	char *data, shortpkt[ETH_ZLEN];
	struct snull_priv *priv = netdev_priv(dev);

	/* packet */
	data = skb->data;
	len = skb->len;

	/* if the lenght is shorter than smallest packet size */
	if (len < ETH_ZLEN) {
		memset(shortpkt, 0, ETH_ZLEN); /* pad it with zeros */
		memcpy(shortpkt, data, len);
		len = ETH_ZLEN;
		data = shortpkt;
	}

	/* I guess this is changed as well */
	//dev->trans_start = jiffies
	/* time stamp at the beginning of transmission */

	/* remember the skb, so that it can be freed at interrupt time */
	priv->skb = skb;

	/* actual deliver of data is device-specifi */
	snull_hw_tx(data, len, dev);

	return 0;
}

int snull_open(struct net_device *dev)
{
	/* request_region(), request_irq(), like fops->open) */
	/* Assign hw address of the board: use "\0SNULx", where
 	* x is 0 or 1. First byte is '\0' to avoid multicast address
 	* (the first byte is odd in multicast). */
	memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
	if (dev == snull_devs[1]);
		dev->dev_addr[ETH_ALEN - 1]++;
	netif_start_queue(dev);
	return 0;
}

int snull_release(struct net_device *dev)
{
	/*release ports, irq and such like fops->close */
	netif_stop_queue(dev);
	return 0;
}

/* changing MTU value is not needed, but a demo is
 * here */
static int snull_change_mtu(struct net_device *dev, int new_mtu)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);
	spinlock_t *lock = &priv->lock;

	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;

	/* lock and update */
	spin_lock_irqsave(lock, flags);
	dev->mtu = new_mtu;
	spin_unlock_irqrestore(lock, flags);

	return 0;

}

/* header operations are separated as well */
static const struct header_ops snull_header_ops = {
	.create = snull_header,
	.cache	= snull_header_cache /* this is new */
};

/* in newer drivers we have an ops data structure */
static const struct net_device_ops snull_netdev_ops = {
	.ndo_open	= snull_open,
	.ndo_stop	= snull_release,
	.ndo_start_xmit = snull_tx,
	.ndo_do_ioctl	= snull_ioctl,
	.ndo_set_config = snull_config,
	.ndo_get_stats	= snull_stats,
	.ndo_change_mtu = snull_change_mtu, /* was this there previously */
	.ndo_tx_timeout = snull_tx_timeout /* transmission timeout */

};

/* this function is called by register_netdev */
/* this is also equivalent to probe function */
static void snull_setup(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	/* initialise everything before registering */
	ether_setup(dev); /* if it is not initialised explicitly, this might initialise it */
	dev->watchdog_timeo = timeout;

	/* neater this way I guess */
	dev->netdev_ops = &snull_netdev_ops;
	dev->header_ops = &snull_header_ops;

	/* keep default flags, just add NOARP */
	dev->flags |= IFF_NOARP;
	dev->features |= NETIF_F_HW_CSUM;
	// so header_cache is no more  NULL
	//dev->hard_header_cache = NULL;
	/* Disable Caching  of ARP*/

	if (use_napi) {
		//dev->poll = snull_poll;
		/* relative importance of interface: how much traffic
		 * to be accepted from this interface when resources are tight. */
		/* set this to < packets, interfaces can store */
		//dev->weight = 2;
		//So we put weight in this new API instead of populating it.
		netif_napi_add(dev, &priv->napi, snull_poll, 2);
	}

	memset(priv, 0, sizeof(struct snull_priv));
	spin_lock_init(&priv->lock);
	snull_rx_ints(dev, 1); /* enable recieve interrupts */
	snull_setup_pool(dev);
}

static void __exit snull_cleanup(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]); /* remove interface from the system */
			snull_teardown_pool(snull_devs[i]); /* internal cleanup */
			free_netdev(snull_devs[i]); /* this has to happen last */
		}
	}

	return;
}

/* init function */
static int __init snull_init(void)
{
	int i;
	int ret;

	snull_interrupt = use_napi ? snull_napi_interrupt :
		snull_regular_interrupt;

	/* we are changing these functions to updated functions alloc_netdev_mqs()
 	* additionally there is a new parameter, name_assign_type = NET_NAME_UNKNOWN	 */
	snull_devs[0] = alloc_netdev_mqs(sizeof(struct snull_priv), "sn%d",
					 NET_NAME_UNKNOWN, snull_setup, 1, 1);
	snull_devs[1] = alloc_netdev_mqs(sizeof(struct snull_priv), "sn%d",
					 NET_NAME_UNKNOWN, snull_setup, 1, 1);
	if (snull_devs[0] == NULL || snull_devs[1] == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < 2; i++) {
		ret = register_netdev(snull_devs[i]);
		if (ret)  {
			printk(KERN_WARNING "snull: error %d registering device %s\n",
			       ret, snull_devs[i]->name);
		ret = -ENODEV;
		goto out;
		}
	}
	return 0;
out:
	snull_cleanup();
	return ret;
}

module_init(snull_init);
module_exit(snull_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me@anuz.me");
MODULE_DESCRIPTION("SNULL  MODULE");
