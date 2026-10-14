// SPDX-License-Identifier: GPL-2.0

#include "rbuf.h"

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/gfp.h>

struct rbuf_item {
	long data[3];
};

struct rbuf_hdr {
	unsigned int tail;
	unsigned int head;
	struct rbuf_item items[0];
};

#define ITEM_NUM RBUF_ITEM_NUM
#define IDX_MASK (RBUF_ITEM_NUM - 1)

static inline unsigned int rbuf_load_head(struct rbuf_in *rb)
{
	return __atomic_load_n(&rb->base->head, __ATOMIC_ACQUIRE);
}

static inline unsigned int rbuf_load_tail(struct rbuf_out *rb)
{
	return __atomic_load_n(&rb->base->tail, __ATOMIC_ACQUIRE);
}

static inline void rbuf_advance_tail(struct rbuf_in *rb)
{
	++rb->tail;
	__atomic_store_n(&rb->base->tail, rb->tail, __ATOMIC_RELEASE);
}

static inline void rbuf_advance_head(struct rbuf_out *rb)
{
	++rb->head;
	__atomic_store_n(&rb->base->head, rb->head, __ATOMIC_RELEASE);
}

unsigned int rbuf_num_dequeuing(struct rbuf_in *rb)
{
	return rbuf_load_head(rb) - rb->tail;
}

unsigned int rbuf_num_enqueued(struct rbuf_out *rb)
{
	return rb->head - rbuf_load_tail(rb);
}

bool rbuf_is_empty(struct rbuf_in *rb)
{
	return rbuf_num_dequeuing(rb) == 0;
}

bool rbuf_is_full(struct rbuf_out *rb)
{
	return rbuf_num_enqueued(rb) == ITEM_NUM;
}

void __rbuf_dequeue(struct rbuf_in *rb, struct rbuf_item *item)
{
	*item = rb->base->items[rb->tail & IDX_MASK];
	rbuf_advance_tail(rb);
}

void __rbuf_enqueue(struct rbuf_out *rb, struct rbuf_item *item)
{
	rb->base->items[rb->head & IDX_MASK] = *item;
	rbuf_advance_head(rb);
}

int rbuf_dequeue(struct rbuf_in *rb, struct rbuf_item *item)
{
	if (rbuf_is_empty(rb))
		return -EAGAIN;

	__rbuf_dequeue(rb, item);
	return 0;
}

int rbuf_enqueue(struct rbuf_out *rb, struct rbuf_item *item)
{
	if (rbuf_is_full(rb))
		return -EAGAIN;

	__rbuf_enqueue(rb, item);
	return 0;
}

int rbuf_init_in(struct rbuf_in *rb)
{
	BUILD_BUG_ON(ITEM_NUM & (ITEM_NUM - 1));
	BUILD_BUG_ON(RBUF_ITEM_SIZE != sizeof(struct rbuf_item));
	BUILD_BUG_ON(sizeof(struct rbuf_hdr) + RBUF_ITEM_SIZE * ITEM_NUM >
		     PAGE_SIZE);

	rb->base = (struct rbuf_hdr *)__get_free_page(GFP_KERNEL);
	if (!rb->base)
		return -ENOMEM;

	rb->tail = 0;

	return 0;
}

int rbuf_init_out(struct rbuf_out *rb)
{
	BUILD_BUG_ON(ITEM_NUM & (ITEM_NUM - 1));
	BUILD_BUG_ON(RBUF_ITEM_SIZE != sizeof(struct rbuf_item));
	BUILD_BUG_ON(sizeof(struct rbuf_hdr) + RBUF_ITEM_SIZE * ITEM_NUM >
		     PAGE_SIZE);

	rb->base = (struct rbuf_hdr *)__get_free_page(GFP_KERNEL);
	if (!rb->base)
		return -ENOMEM;

	rb->head = 0;

	return 0;
}

void rbuf_del_in(struct rbuf_in *rb)
{
	free_page((unsigned long)rb->base);
	rb->base = 0;
}

void rbuf_del_out(struct rbuf_out *rb)
{
	free_page((unsigned long)rb->base);
	rb->base = 0;
}
