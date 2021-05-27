#include <linux/module.h>

#include <net/cfg80211.h> /*wiphy stuff?*/
#include <linux/skbuff.h>

#include <linux/workqueue.h>
#include <linux/semaphore.h>

#define WIPHY_NAME "navifly"
#define NDEV_NAME "navifly%d"
#define SSID_DUMMY "NaviflyWIFI"
#define SSID_DUMMY_SIZE (sizeof(SSID_DUMMY) - 1)

struct navifly_context {
	struct wiphy *wiphy; /* physical device, list with iw list */
	struct net_device *ndev; /* network devices */
	struct semaphore sem;
	struct work_struct ws_connect;
	char connecting_ssid[sizeof(SSID_DUMMY)];
	struct work_struct ws_disconnect;
	u16 disconnect_reason_code;
	struct work_struct ws_scan;
	struct cfg80211_scan_request *scan_request;
};

struct navifly_wiphy_priv_context {
	struct navifly_context *navi;
};

struct navifly_ndev_priv_context {
	struct navifly_context *navi;
	struct wireless_dev wdev; /* this and ndev represent physical device */
};

/* get priv context from wiphy ds*/
static struct navifly_wiphy_priv_context
*wiphy_get_navi_context(struct wiphy *wiphy)
{
	return (struct navifly_wiphy_priv_context *)wiphy_priv(wiphy);
}

/* get priv context from netdev ds*/
static struct navifly_ndev_priv_context
*ndev_get_navi_context(struct net_device *ndev)
{
	return (struct navifly_ndev_priv_context *)netdev_priv(ndev);
}

/* prepare a dummy BSS response for system
 * cfg80211_inform_bss_data cotnain information for basic service set: channel,
 * signal strength etc.
 * inform kernel about new bss
 */
static void inform_dummy_bss(struct navifly_context *navi)
{
	struct cfg80211_bss *bss = NULL;
	struct cfg80211_inform_bss data = {
		.chan = &navi->wiphy->bands[NL80211_BAND_2GHZ]->channels[0],
		.scan_width = NL80211_BSS_CHAN_WIDTH_20,
		/* signal "type" not specified can be a % 0-100 or mBm
		 * before wiphy registration using .signal_type*/
		.signal = 1337,
	};
	char bssid[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	/* array of tags obtained from beacon frame or probe response */
	char ie[SSID_DUMMY_SIZE + 2] = {WLAN_EID_SSID, SSID_DUMMY_SIZE}; /*informatio element packs ssid it is a  frame taken from wifi management frame*/
	memcpy(ie + 2, SSID_DUMMY, SSID_DUMMY_SIZE);
	/* bss known to the systems*/
	/* can use cfg80211_inform_bss() instead */
	bss = cfg80211_inform_bss_data(navi->wiphy, &data,
				       CFG80211_BSS_FTYPE_UNKNOWN, bssid, 0,
				       WLAN_CAPABILITY_ESS, 100, ie, sizeof(ie),
				       GFP_KERNEL);
	/*since bss is no longer needed, put it back to avoid memory leak*/
	/* returning cfg80211_bss ds refcounter, to be decremented if not used
	 * */
	cfg80211_put_bss(navi->wiphy, bss);
}

/* bss data can be obtained from cfg80211_inform_bss()
 * this function can be called outside scanning if it is unplanned scanning,
 * when scanning is done, we call cfg80211_scan_done()
 * for aborted scan aborted() funtion is called
 * inform kernel about BSS and finish scan
 * called through workqueue, when kernel asks about cfg80211_ops*/
static void navifly_scan_routine(struct work_struct *w)
{
	struct navifly_context *navi = container_of(w, struct navifly_context, ws_scan); 
	struct cfg80211_scan_info info = {
		/* set true if user aborts or hw issues */
		.aborted = false,
	};
	/* seems like a bug where cfg80211_ops->scan() can't be called before
	 * scan_done immediately */
	msleep(100);
	inform_dummy_bss(navi);
	if (down_interruptible(&navi->sem)) {
	    return;
	}
	
	cfg80211_scan_done(navi->scan_request, &info);
	navi->scan_request = NULL;
	up(&navi->sem);
}

/*
 * To check if SSID is found before calling cfg80211_connect_bss,
 * we inform that we have scanned for ssid
 * called through workqueue when kernel asks about connect through
 * cfg80211_ops()
 */
static void navifly_connect_routine(struct work_struct *w)
{
	struct navifly_context *navi = container_of(w,
						    struct navifly_context,
						    ws_connect);
	if (down_interruptible(&navi->sem)) {
		return;
	    }
	if (memcmp(navi->connecting_ssid, SSID_DUMMY, sizeof(SSID_DUMMY))!= 0) {
		cfg80211_connect_timeout(navi->ndev, NULL, NULL, 0, GFP_KERNEL,
					 NL80211_TIMEOUT_SCAN);
	} else {
		/* send dummy bss to kernel */
		inform_dummy_bss(navi);
		cfg80211_connect_bss(navi->ndev,  NULL, NULL, NULL, 0, NULL, 0,
				     WLAN_STATUS_SUCCESS, GFP_KERNEL,
				     NL80211_TIMEOUT_UNSPECIFIED);
	}
	navi->connecting_ssid[0] = 0;
	up(&navi->sem);
}

/* cfg80211_disconnected can be called any time wiphy context is connected*/
/*  cfg80211_disconnected informs the kernel that disconnect is complete
 *  on interruption  cfg80211_connect_timeout() is called, not featured here
 *  called rorm disconnect work_queue through cfg802011_ops*/
static void navifly_disconnect_routine(struct work_struct *w)
{
	struct navifly_context *navi = container_of(w, struct navifly_context,
						    ws_disconnect);
	if (down_interruptible(&navi->sem)) {
		return;
	}
	cfg80211_disconnected(navi->ndev, navi->disconnect_reason_code, NULL,
			      0 , true, GFP_KERNEL);
	navi->disconnect_reason_code = 0;
	up(&navi->sem);
}

/* init disconnect routines through work_struct. Call cfg80211_disconnected on
 * disconnection completion */
static int nvf_disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{
	struct navifly_context *navi = wiphy_get_navi_context(wiphy)->navi;

	if (down_interruptible(&navi->sem)) {
                  return -ERESTARTSYS;
              }

	navi->disconnect_reason_code = reason_code;

	up(&navi->sem);

	if (!schedule_work(&navi->ws_disconnect)) {
		return -EBUSY;
	}
	return 0;
}

/* connect and disconnect funtion are implemented together
 * initiate connect routine with cfg80211_connect_
 * bss()
 * result()
 * done()
 * timeout()
 */
static int nvf_connect(struct wiphy *wiphy, struct net_device *dev,
		       struct cfg80211_connect_params *sme)
{
	/* sme struct contains a other information about connection, but we are
	 * only using ssid */
	struct navifly_context *navi = wiphy_get_navi_context(wiphy)->navi;
	size_t ssid_len = sme->ssid_len > 15 ? 15: sme->ssid_len;

	if (sme->ssid == NULL || sme->ssid_len == 0) {
		return -EBUSY;
	}

	if (down_interruptible(&navi->sem)) {
	    return -ERESTARTSYS;
	}

	memcpy(navi->connecting_ssid, sme->ssid, ssid_len);
	navi->connecting_ssid[ssid_len] = 0; /* nul terminate */

	up(&navi->sem);

	if (!schedule_work(&navi->ws_connect)) {
		return -EBUSY;
	}
	return 0;
}

/* if user requests a scan cfg80211_ops will call scan function
 * uses work_queue to start scan routine.
 * *
 */
static int nvf_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct navifly_context *navi = wiphy_get_navi_context(wiphy)->navi;
	/* synchronising scan_request using semaphore */
	if (down_interruptible(&navi->sem)) {
	    return -ERESTARTSYS;
	}

	if (navi->scan_request != NULL) {
		up(&navi->sem);
		return -EBUSY;
	}
	navi->scan_request = request;
	up(&navi->sem);
	/* we request ws_scan, which executes navifly_scan_routine */
	if (!schedule_work(&navi->ws_scan)) {
	    return -EBUSY;
	}

	return 0;
}

/* functions needs for fullMAc driver
 * to be implemented with wiphy struct fields
 * connect and disconnect are always in pair
 */
static const struct cfg80211_ops nvf_cfg_ops = {
	.scan = nvf_scan,
	.connect = nvf_connect,
	.disconnect = nvf_disconnect,
};

/* transmit function for network packets */
static netdev_tx_t nvf_ndo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	/* free the skb and transfer its ownership to callee */
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

/* net dev ops */
static const struct net_device_ops nvf_ndev_ops = {
	.ndo_start_xmit = nvf_ndo_start_xmit,
};

/* Supported channels, required for wiphy */
static struct ieee80211_channel nvf_supported_channel_2ghz[] = {
	{
		.band = NL80211_BAND_2GHZ,
		.hw_value = 0,
		.center_freq = 2437,
	}
};

/* supported rates  for 2ghz band*/
static struct ieee80211_rate nvf_supported_rates_2ghz[] = {
	{
		.bitrate = 10,
		.hw_value = 0x1,
	},
	{
		.bitrate = 20,
		.hw_value = 0x2,
	},
	{
		.bitrate = 55,
		.hw_value = 0x4,
	},
	{
		.bitrate = 110,
		.hw_value = 0x8,
	},
};

/* supported bands */
static struct ieee80211_supported_band nvf_band_2ghz = {
	.ht_cap.cap = IEEE80211_HT_CAP_SGI_20,
	.ht_cap.ht_supported = false,

	.channels = nvf_supported_channel_2ghz,
	.n_channels = ARRAY_SIZE(nvf_supported_channel_2ghz),

	.bitrates = nvf_supported_rates_2ghz,
	.n_bitrates = ARRAY_SIZE(nvf_supported_rates_2ghz),

};

/* on unloading the device, it will clean the context
 * virtual device will also disappear */
static void navifly_free(struct navifly_context *ctx)
{
	if (ctx == NULL) {
		return;
	}
	unregister_netdev(ctx->ndev);
	free_netdev(ctx->ndev);
	wiphy_unregister(ctx->wiphy);
	wiphy_free(ctx->wiphy);
	kfree(ctx);
 }

/* create context for wiphy & net_device with wireless_dev
 *  these interfaces are used by kernel to interact with driver */
static struct navifly_context *navifly_create_context(void)
{
	struct navifly_context *ret = NULL;
	struct navifly_wiphy_priv_context *wiphy_data = NULL;
	struct navifly_ndev_priv_context *ndev_data = NULL;

	ret = kmalloc(sizeof(struct navifly_context), GFP_KERNEL);
	if (!ret) {
		goto l_error;
	}

	/* wiphy represent physical wireles device
	 * one wiphy can have multiple interfaces, requires add_virtual_intf()
	 * in cfg80211_ops.
	 */
	ret->wiphy = wiphy_new_nm(&nvf_cfg_ops, sizeof(struct navifly_wiphy_priv_context), WIPHY_NAME);
	if (ret->wiphy == NULL) {
		goto l_error_wiphy;
	}

	/*wiphy structure require cfg80211_ops structure represents feaures of
	 * wireles devices*/
	wiphy_data = wiphy_get_navi_context(ret->wiphy);
	wiphy_data->navi = ret;

	/* modes supported by device */
	ret->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	/* random channel */
	ret->wiphy->bands[NL80211_BAND_2GHZ] = &nvf_band_2ghz;
	/* how many ssids can this device scan */
	ret->wiphy->max_scan_ssids = 69;

	/* register physical wireless device, iw list will now show the device
	 * this has no network interface yet */
	if (wiphy_register(ret->wiphy) < 0) {
		goto l_error_wiphy_register;
	}
	/* register network device */
	ret->ndev = alloc_netdev(sizeof(struct net_device), NDEV_NAME, NET_NAME_ENUM, ether_setup);
	if (ret->ndev == NULL) {
		goto l_error_alloc_ndev;
	}

	/* set private data structure */
	ndev_data = ndev_get_navi_context(ret->ndev);
	ndev_data->navi = ret;
	/* wireless_dev with net_device can be represented as interited class of
	 * single net device */
	ndev_data->wdev.wiphy = ret->wiphy;
	ndev_data->wdev.netdev = ret->ndev;
	ndev_data->wdev.iftype = NL80211_IFTYPE_STATION;
	ret->ndev->ieee80211_ptr = &ndev_data->wdev; /* this is make this device recognised as wifi device */
	/* implement methods for net_device */
	ret->ndev->netdev_ops = &nvf_ndev_ops;

	/* register the network device */
	if (register_netdev(ret->ndev)) {
		goto l_error_ndev_register;
	}

	return ret;

l_error_ndev_register:
	free_netdev(ret->ndev);
l_error_alloc_ndev:
	wiphy_unregister(ret->wiphy);
l_error_wiphy_register:
	wiphy_free(ret->wiphy);
l_error_wiphy:
	kfree(ret);
l_error:
	return NULL;

}
static struct navifly_context *g_ctx = NULL;

static int __init virtual_wifi_init(void)
{
	g_ctx = navifly_create_context();
	if (g_ctx != NULL) {
		/* adding outside context of navifly, workqueues */
		sema_init(&g_ctx->sem, 1);
		INIT_WORK(&g_ctx->ws_connect, navifly_connect_routine);
		g_ctx->connecting_ssid[0] = 0;
		INIT_WORK(&g_ctx->ws_disconnect, navifly_disconnect_routine);
		g_ctx->disconnect_reason_code = 0;
		INIT_WORK(&g_ctx->ws_scan, navifly_scan_routine);
		g_ctx->scan_request = NULL;
	}
	return g_ctx == NULL;
}

static void __exit virtual_wifi_exit(void)
{
	cancel_work_sync(&g_ctx->ws_connect);
	cancel_work_sync(&g_ctx->ws_disconnect);
	cancel_work_sync(&g_ctx->ws_scan);
	navifly_free(g_ctx);
}

module_init(virtual_wifi_init);
module_exit(virtual_wifi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("fullmac driver");
