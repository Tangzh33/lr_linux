// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "aster-net: " fmt

#include <asm/cpu_host.h>
#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
#include <net/cfg80211.h>
#include <net/xdp.h>
#include "rbuf.h"

static struct net_device *n_dev;
static struct rbuf_out rxq;
static struct rbuf_in rxq_done;
static struct rbuf_in txq;
static struct rbuf_out txq_prep;
static DECLARE_COMPLETION(anet_device_registered);

struct rbuf_item {
	void *priv;
	unsigned char *buf;
	unsigned long len;
};

static unsigned int anet_rx_cleanup(void)
{
	struct rbuf_item item;
	struct sk_buff *skb;
	unsigned int cnt = 0;

	while (rbuf_dequeue(&rxq_done, &item) >= 0) {
		skb = item.priv;
		consume_skb(skb);
		++cnt;
	}

	return cnt;
}

rx_handler_result_t anet_rx_callback(struct sk_buff **pskb)
{
	static unsigned int num_free = RBUF_ITEM_NUM;

	struct sk_buff *skb = *pskb;
	struct rbuf_item item;
	int err;

	if (skb->dev != n_dev)
		return RX_HANDLER_PASS;

	/* FIXME: Is this racy? */
	num_free += anet_rx_cleanup();

	pr_debug("packet received\n");

	if (!num_free) {
		consume_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	err = skb_linearize(skb);
	if (err < 0) {
		pr_warn_ratelimited("packet linearization failed: %d\n", err);
		consume_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	item.priv = skb;
	item.buf = skb_mac_header(skb);
	item.len = skb_tail_pointer(skb) - item.buf;

	/* FIXME: Is this racy? */
	--num_free;
	err = rbuf_enqueue(&rxq, &item);
	if (err < 0) {
		++num_free;
		consume_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	/* FIXME: Consider more about memory orders */
	if (rbuf_num_enqueued(&rxq) <= 1)
		lkl_cpu_notify(LKL_CPU_EVENT_NETWORK);

	return RX_HANDLER_CONSUMED;
}

/* FIXME: How to synchronize with Asterinas code? */
#define ASTER_HEADROOM_LEN (12 + 16 + 60 + 60)

static void anet_tx_prefill(void)
{
	static struct napi_struct napi;

	struct sk_buff *skb;
	struct rbuf_item item;
	int num_enqueued = 0;

	while (!rbuf_is_full(&txq_prep)) {
		/* Use NAPI for efficient allocation (it has a SKB pool) */
		skb = napi_alloc_skb(&napi, ETH_FRAME_LEN + ASTER_HEADROOM_LEN);
		if (!skb)
			return;

		item.priv = skb;
		item.buf = skb->data;
		item.len = ETH_FRAME_LEN + ASTER_HEADROOM_LEN;
		__rbuf_enqueue(&txq_prep, &item);
		++num_enqueued;
	}

	/* FIXME: Consider more about memory orders */
	if (rbuf_num_enqueued(&txq_prep) <= num_enqueued)
		lkl_cpu_notify(LKL_CPU_EVENT_NETWORK);
}

static void anet_tx_work(void)
{
	struct rbuf_item item;
	struct sk_buff *skb;

	while (rbuf_dequeue(&txq, &item) >= 0) {
		skb = item.priv;
		skb_reserve(skb, item.buf - skb->data);
		__skb_put(skb, item.len);

		skb->dev = n_dev;
		skb->protocol = skb_eth_hdr(skb)->h_proto;
		dev_queue_xmit(skb);

		pr_debug("packet transmitted\n");
	}

	anet_tx_prefill();
}

static unsigned int anet_rx_cleanup_xdp(void)
{
	struct rbuf_item item;
	struct xdp_frame *xdpf;
	unsigned int cnt = 0;

	while (rbuf_dequeue(&rxq_done, &item) >= 0) {
		xdpf = item.priv;
		xdp_return_frame(xdpf);
		++cnt;
	}

	return cnt;
}

int anet_rx_callback_xdp(struct net_device *dev, struct xdp_buff *xdp)
{
	static unsigned int num_free = RBUF_ITEM_NUM;

	struct xdp_frame *xdpf = xdp_convert_buff_to_frame(xdp);
	struct rbuf_item item;

	if (dev != n_dev) {
		xdp_return_frame(xdpf);
		return 0;
	}

	/* FIXME: Is this racy? */
	num_free += anet_rx_cleanup_xdp();

	pr_debug("packet received\n");

	if (!num_free) {
		xdp_return_frame(xdpf);
		return 0;
	}

	item.priv = xdpf;
	item.buf = xdpf->data;
	item.len = xdpf->len;

	/* FIXME: Is this racy? */
	--num_free;
	rbuf_enqueue(&rxq, &item);

	/* FIXME: Consider more about memory orders */
	if (rbuf_num_enqueued(&rxq) <= 1)
		lkl_cpu_notify(LKL_CPU_EVENT_NETWORK);

	return 0;
}

#define HEADROOM_LEN 16

static struct xdp_frame *alloc_xdp_frame(void)
{
	static struct page_frag_cache pf_cache;

	struct xdp_frame *xdpf;

	xdpf = page_frag_alloc(&pf_cache,
			       sizeof(*xdpf) + ETH_FRAME_LEN + HEADROOM_LEN +
				       ASTER_HEADROOM_LEN,
			       GFP_ATOMIC);
	if (!xdpf)
		return NULL;

	memset(xdpf, 0, sizeof(*xdpf));
	xdpf->mem.type = MEM_TYPE_PAGE_SHARED;

	return xdpf;
}

static void anet_tx_prefill_xdp(void)
{
	struct xdp_frame *xdpf;
	struct rbuf_item item;
	int num_enqueued = 0;

	while (!rbuf_is_full(&txq_prep)) {
		xdpf = alloc_xdp_frame();
		if (!xdpf)
			return;

		item.priv = xdpf;
		item.buf = (unsigned char *)(xdpf + 1) + HEADROOM_LEN;
		item.len = ETH_FRAME_LEN + ASTER_HEADROOM_LEN;
		__rbuf_enqueue(&txq_prep, &item);
		++num_enqueued;
	}

	/* FIXME: Consider more about memory orders */
	if (rbuf_num_enqueued(&txq_prep) <= num_enqueued)
		lkl_cpu_notify(LKL_CPU_EVENT_NETWORK);
}

static void anet_tx_work_xdp(void)
{
	struct rbuf_item item;
	struct xdp_frame *xdpf;
	int cnt;

	while (rbuf_dequeue(&txq, &item) >= 0) {
		xdpf = item.priv;

		xdpf->data = item.buf;
		xdpf->headroom = item.buf - (unsigned char *)(xdpf + 1);
		xdpf->len = item.len;

		cnt = n_dev->netdev_ops->ndo_xdp_xmit(n_dev, 1, &xdpf,
						      XDP_XMIT_FLUSH);
		if (cnt < 1) {
			xdp_return_frame(xdpf);
			break;
		}

		pr_debug("packet transmitted\n");
	}

	anet_tx_prefill_xdp();
}

struct netdev_descriptor {
	int is_ready;
	char mac_addr[ETH_ALEN];
	int doorbell_irq;
	void *rxq;
	void *rxq_done;
	void *txq;
	void *txq_prep;
};

struct wlandev_descriptor {
	u32 is_ready;
	u32 lkl_ifindex;
	char lkl_ifname[IFNAMSIZ];
	u32 lkl_wiphy;
	u64 lkl_wdev;
	u8 mac[ETH_ALEN];
	u32 mtu;
};

extern struct netdev_descriptor asterinas_netdev_descriptor;
extern struct wlandev_descriptor asterinas_wlan_descriptor;

#define ANET_DEVICE_WAIT_MS 10000

static bool anet_is_target_device(const struct net_device *dev)
{
	return !strcmp(dev->name, "eth0") || !strcmp(dev->name, "wlan0");
}

static struct net_device *anet_get_device(void)
{
	struct net_device *dev;

	dev = dev_get_by_name(&init_net, "eth0");
	if (dev)
		return dev;

	return dev_get_by_name(&init_net, "wlan0");
}

static int anet_device_event(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event == NETDEV_REGISTER && anet_is_target_device(dev))
		complete_all(&anet_device_registered);

	return NOTIFY_DONE;
}

static struct notifier_block anet_device_notifier = {
	.notifier_call = anet_device_event,
};

static struct net_device *anet_wait_device(void)
{
	struct net_device *dev;
	int err;

	dev = anet_get_device();
	if (dev)
		return dev;

	if (!IS_ENABLED(CONFIG_IWLWIFI))
		return NULL;

	err = register_netdevice_notifier(&anet_device_notifier);
	if (err < 0) {
		pr_warn("device notifier not available: %d\n", err);
		return NULL;
	}

	dev = anet_get_device();
	if (!dev) {
		pr_info("waiting for network device\n");
		wait_for_completion_timeout(&anet_device_registered,
					    msecs_to_jiffies(ANET_DEVICE_WAIT_MS));
		dev = anet_get_device();
	}

	unregister_netdevice_notifier(&anet_device_notifier);

	return dev;
}

#if IS_ENABLED(CONFIG_CFG80211)
static int anet_wiphy_index(struct wiphy *wiphy, u32 *idx)
{
	const char *name = wiphy_name(wiphy);

	if (strncmp(name, "phy", 3))
		return -EINVAL;

	return kstrtou32(name + 3, 10, idx);
}

static int anet_wlan_ids(struct net_device *dev, u32 *wiphy_id, u64 *wdev_id)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	if (!wdev || !wdev->wiphy)
		return -ENODEV;

	err = anet_wiphy_index(wdev->wiphy, wiphy_id);
	if (err < 0)
		return err;

	*wdev_id = ((u64)*wiphy_id << 32) | wdev->identifier;
	return 0;
}
#else
static int anet_wlan_ids(struct net_device *dev, u32 *wiphy_id, u64 *wdev_id)
{
	return -ENODEV;
}
#endif

static void anet_fill_wlan_descriptor(struct net_device *dev)
{
	struct wlandev_descriptor *wdesc = &asterinas_wlan_descriptor;
	u32 wiphy_id;
	u64 wdev_id;

	__atomic_store_n(&wdesc->is_ready, 0, __ATOMIC_RELAXED);

	if (anet_wlan_ids(dev, &wiphy_id, &wdev_id) < 0) {
		pr_warn("wlan cfg80211 identity unavailable\n");
		return;
	}

	wdesc->lkl_ifindex = dev->ifindex;
	strscpy(wdesc->lkl_ifname, dev->name, IFNAMSIZ);
	wdesc->lkl_wiphy = wiphy_id;
	wdesc->lkl_wdev = wdev_id;
	memcpy(wdesc->mac, dev->dev_addr, ETH_ALEN);
	wdesc->mtu = dev->mtu;

	__atomic_store_n(&wdesc->is_ready, 1, __ATOMIC_RELEASE);
}


static __init int anet_init(void)
{
	struct netdev_bpf xdp;
	struct net_device *dev;
	int err;
	int is_xdp;
	struct netdev_descriptor *desc = &asterinas_netdev_descriptor;

	BUILD_BUG_ON(RBUF_ITEM_SIZE != sizeof(struct rbuf_item));

	dev = anet_wait_device();
	if (!dev) {
		pr_warn("device not available\n");
		return -ENODEV;
	}

	memset(&xdp, 0, sizeof(xdp));
	xdp.command = XDP_SETUP_PROG;
	xdp.prog = bpf_prog_alloc(PAGE_SIZE, GFP_KERNEL);
	if (!xdp.prog) {
		dev_put(dev);
		return -ENOMEM;
	}
	/* we're going to leak xdp.prog */

	rtnl_lock();

	err = dev_open(dev, NULL);
	if (err < 0) {
		rtnl_unlock();
		pr_warn("device not up: %d\n", err);
		goto err_open;
	}

	if (dev->netdev_ops->ndo_bpf) {
		err = dev->netdev_ops->ndo_bpf(dev, &xdp);
		if (err < 0)
			pr_warn("xdp fail: %d\n", err);
		is_xdp = err >= 0;
	} else {
		is_xdp = false;
	}
	pr_info("is_xdp = %d\n", is_xdp);

	/*
	 * As a fast path, we have a hook that calls anet_rx_callback() directly
	 * in napi_gro_receive(). However, not all devices use it, so a fallback
	 * is still necessary.
	 */
	err = netdev_rx_handler_register(dev, &anet_rx_callback, NULL);
	if (err < 0) {
		rtnl_unlock();
		pr_warn("device rx busy: %d\n", err);
		goto err_open;
	}

	rtnl_unlock();

	err = rbuf_init_out(&rxq);
	if (err < 0)
		goto err_rxq;
	err = rbuf_init_in(&rxq_done);
	if (err < 0)
		goto err_rxq_done;
	err = rbuf_init_in(&txq);
	if (err < 0)
		goto err_txq;
	err = rbuf_init_out(&txq_prep);
	if (err < 0)
		goto err_txq_prep;

	if (is_xdp)
		err = lkl_set_doorbell_irq_handler(&anet_tx_work_xdp);
	else
		err = lkl_set_doorbell_irq_handler(&anet_tx_work);
	if (err < 0) {
		pr_warn("irq not available\n");
		goto err_irq;
	}

	n_dev = dev;

	if (is_xdp)
		anet_tx_prefill_xdp();
	else
		anet_tx_prefill();

	memcpy(desc->mac_addr, dev->dev_addr, ETH_ALEN);
	desc->doorbell_irq = LKL_DOORBELL_IRQ;
	desc->rxq = rxq.base;
	desc->rxq_done = rxq_done.base;
	desc->txq = txq.base;
	desc->txq_prep = txq_prep.base;
	if (!strcmp(dev->name, "wlan0"))
		anet_fill_wlan_descriptor(dev);
	__atomic_store_n(&desc->is_ready, 1, __ATOMIC_RELEASE);

	return 0;

err_irq:
	rbuf_del_out(&txq_prep);
err_txq_prep:
	rbuf_del_in(&txq);
err_txq:
	rbuf_del_in(&rxq_done);
err_rxq_done:
	rbuf_del_out(&rxq);

err_rxq:
	rtnl_lock();
	dev_close(dev);
	rtnl_unlock();
err_open:
	dev_put(dev);

	return err;
}
late_initcall_sync(anet_init);
