// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "aster-blk: " fmt

#include <asm/cpu_host.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "rbuf.h"

struct rbuf_item {
	union {
		/* In: */
		struct {
			unsigned long i_dma : 63, i_read : 1;
			unsigned long i_off;
			signed int i_virt;
			unsigned int i_len;
		};
		/* Out: */
		struct {
			long o_res;
			long o_idx;
			long o_rsv;
		};
	};
};

static struct block_device *bdev;
static struct rbuf_in in_q;
static struct rbuf_out out_q;
static spinlock_t out_lock;

static int ablk_set_dma(const struct rbuf_item *item)
{
	unsigned long dma, dma_end;
	void *virt;
	struct page *page;

	if (offset_in_page(item->i_dma) != offset_in_page(item->i_virt))
		return -EINVAL;

	dma = item->i_dma;
	dma_end = item->i_dma + item->i_len;
	virt = (void *)(signed long)item->i_virt;

	while (dma < dma_end) {
		page = virt_to_page(virt);
		if (!PageReserved(page))
			return -EINVAL;

		page->private = PAGE_ALIGN_DOWN(dma);

		dma += PAGE_SIZE;
		virt += PAGE_SIZE;
	}

	return 0;
}

static void ablk_fini_serve(int err, long index)
{
	struct rbuf_item item = {};
	unsigned long flags;

	item.o_res = err;
	item.o_idx = index;

	spin_lock_irqsave(&out_lock, flags);
	WARN_ON(rbuf_enqueue(&out_q, &item) < 0);
	spin_unlock_irqrestore(&out_lock, flags);

	lkl_cpu_notify(LKL_CPU_EVENT_BLOCK);
}

static enum rq_end_io_ret ablk_complete_serve(struct request *rq,
					      blk_status_t err)
{
	ablk_fini_serve(err, (long)rq->end_io_data);

	return RQ_END_IO_FREE;
}

enum serve_nowait_t {
	SERVE_NOWAIT = 1,
	SERVE_WAIT = 0,
};

static int ablk_serve_one(const struct rbuf_item *item, unsigned long id,
			  enum serve_nowait_t nowait)
{
	int err;
	struct request_queue *q;
	struct request *rq;

	err = ablk_set_dma(item);
	if (WARN_ON_ONCE(err < 0))
		return err;

	if (WARN_ON_ONCE(!IS_ALIGNED(item->i_off, SECTOR_SIZE)))
		return -EINVAL;

	q = bdev_get_queue(bdev);

	if (item->i_read)
		rq = blk_mq_alloc_request(q, REQ_OP_READ,
					  nowait ? BLK_MQ_REQ_NOWAIT : 0);
	else
		rq = blk_mq_alloc_request(q, REQ_OP_WRITE,
					  nowait ? BLK_MQ_REQ_NOWAIT : 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	err = blk_rq_map_kern(q, rq, (void *)(signed long)item->i_virt,
			      item->i_len, nowait ? GFP_ATOMIC : GFP_KERNEL);
	if (err)
		goto out;

	rq->__sector = item->i_off >> SECTOR_SHIFT;

	rq->end_io = &ablk_complete_serve;
	rq->end_io_data = (void *)id;

	blk_execute_rq_nowait(rq, false);

	return 0;

out:
	blk_mq_free_request(rq);

	return err;
}

struct ablk_wq_req {
	struct rbuf_item item;
	unsigned long index;
	struct work_struct work;
};

static void ablk_serve_wq(struct work_struct *work)
{
	struct ablk_wq_req *req;
	int err;

	req = container_of(work, struct ablk_wq_req, work);

	err = ablk_serve_one(&req->item, req->index, SERVE_WAIT);
	if (err)
		ablk_fini_serve(err, req->index);

	kfree(req);
}

static struct workqueue_struct *ablk_workqueue;

static void ablk_serve_bh(struct tasklet_struct *tasklet)
{
	static unsigned long counter = 0;

	struct rbuf_item item;
	unsigned long index;
	int err;
	struct ablk_wq_req *req;

	while (rbuf_dequeue(&in_q, &item) >= 0) {
		index = counter++;

		/* hack to avoid dead locks */
		err = (preempt_count() & PREEMPT_MASK) ?
			      -EAGAIN :
			      ablk_serve_one(&item, index, SERVE_NOWAIT);
		if (err >= 0)
			continue;

		req = kmalloc(sizeof(*req), GFP_ATOMIC);
		if (!req) {
			ablk_fini_serve(-ENOMEM, index);
			continue;
		}

		req->item = item;
		req->index = index;
		INIT_WORK(&req->work, &ablk_serve_wq);

		queue_work(ablk_workqueue, &req->work);
	}
}

static DECLARE_TASKLET(ablk_tasklet, &ablk_serve_bh);

#define ABLK_DOORBELL_IRQ 31

static enum irqreturn ablk_doorbell(int irq, void *unused)
{
	tasklet_schedule(&ablk_tasklet);

	return IRQ_HANDLED;
}

struct block_descriptor {
	int is_ready;
	int doorbell_irq;
	void *in_q;
	void *out_q;
};

extern struct block_descriptor asterinas_block_descriptor;

static void ablk_setup(struct work_struct *work)
{
	int err;
	struct block_descriptor *desc = &asterinas_block_descriptor;

	BUILD_BUG_ON(RBUF_ITEM_SIZE != sizeof(struct rbuf_item));

	/* 259 blkext */
	bdev = blkdev_get_by_dev(MKDEV(259, 0), BLK_OPEN_READ | BLK_OPEN_WRITE,
				 NULL, NULL);
	if (IS_ERR(bdev)) {
		/* 254 virtblk */
		bdev = blkdev_get_by_dev(MKDEV(254, 0),
					 BLK_OPEN_READ | BLK_OPEN_WRITE, NULL,
					 NULL);
	}
	if (IS_ERR(bdev)) {
		pr_warn("device not available\n");
		return;
	}

	ablk_workqueue = create_workqueue("ablk");
	if (!ablk_workqueue)
		goto err_wq;

	err = rbuf_init_in(&in_q);
	if (err < 0)
		goto err_in;

	err = rbuf_init_out(&out_q);
	if (err < 0)
		goto err_out;

	desc->doorbell_irq = request_irq(ABLK_DOORBELL_IRQ, &ablk_doorbell, 0,
					 NULL, "aster-blk");
	if (desc->doorbell_irq < 0) {
		pr_warn("irq not available\n");
		goto err_irq;
	}

	desc->in_q = in_q.base;
	desc->out_q = out_q.base;
	__atomic_store_n(&desc->is_ready, 1, __ATOMIC_RELEASE);

	return;

err_irq:
	rbuf_del_out(&out_q);

err_out:
	rbuf_del_in(&in_q);

err_in:
	destroy_workqueue(ablk_workqueue);
	ablk_workqueue = NULL;

err_wq:
	blkdev_put(bdev, NULL);

	return;
}

static struct delayed_work setup_work;

static __init int ablk_init(void)
{
	INIT_DELAYED_WORK(&setup_work, ablk_setup);

	/* Wait until the device is up */
	schedule_delayed_work(&setup_work, 100);

	return 0;
}
late_initcall_sync(ablk_init);
