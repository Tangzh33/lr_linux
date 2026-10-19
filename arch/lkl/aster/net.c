// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "aster-net: " fmt

#include <asm/cpu_host.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/xdp.h>
#include "rbuf.h"

static struct net_device *n_dev;
static struct rbuf_out rxq;
static struct rbuf_in rxq_done;
static struct rbuf_in txq;
static struct rbuf_out txq_prep;

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

	if (skb->dev != n_dev)
		return RX_HANDLER_PASS;

	/* FIXME: Is this racy? */
	num_free += anet_rx_cleanup();

	pr_debug("packet received\n");

	if (!num_free) {
		consume_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	item.priv = skb;
	item.buf = skb_mac_header(skb);
	item.len = skb_tail_pointer(skb) - item.buf;

	/* FIXME: Is this racy? */
	--num_free;
	rbuf_enqueue(&rxq, &item);

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
		skb->dev->netdev_ops->ndo_start_xmit(skb, n_dev);

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

extern struct netdev_descriptor asterinas_netdev_descriptor;

static __init int anet_init(void)
{
	struct netdev_bpf xdp;
	struct net_device *dev;
	int err;
	int is_xdp;
	struct netdev_descriptor *desc = &asterinas_netdev_descriptor;

	BUILD_BUG_ON(RBUF_ITEM_SIZE != sizeof(struct rbuf_item));

	memset(&xdp, 0, sizeof(xdp));
	xdp.command = XDP_SETUP_PROG;
	xdp.prog = bpf_prog_alloc(PAGE_SIZE, GFP_KERNEL);
	if (!xdp.prog)
		return -ENOMEM;
	/* we're going to leak xdp.prog */

	dev = dev_get_by_name(&init_net, "eth0");
	if (!dev) {
		pr_warn("device not available\n");
		return -ENODEV;
	}

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
