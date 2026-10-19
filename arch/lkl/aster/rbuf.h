/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>

struct rbuf_hdr;

struct rbuf_in {
	struct rbuf_hdr *base;
	unsigned int tail;
};

struct rbuf_out {
	struct rbuf_hdr *base;
	unsigned int head;
};

/* This can be redefined, as long as it matches RBUF_ITEM_SIZE */
struct rbuf_item;
#define RBUF_ITEM_SIZE (3 * sizeof(long))

#define RBUF_ITEM_NUM 128

unsigned int rbuf_num_dequeuing(struct rbuf_in *rb);
unsigned int rbuf_num_enqueued(struct rbuf_out *rb);

bool rbuf_is_empty(struct rbuf_in *rb);
bool rbuf_is_full(struct rbuf_out *rb);

void __rbuf_dequeue(struct rbuf_in *rb, struct rbuf_item *item);
void __rbuf_enqueue(struct rbuf_out *rb, struct rbuf_item *item);

int rbuf_dequeue(struct rbuf_in *rb, struct rbuf_item *item);
int rbuf_enqueue(struct rbuf_out *rb, struct rbuf_item *item);

int rbuf_init_in(struct rbuf_in *rb);
int rbuf_init_out(struct rbuf_out *rb);

void rbuf_del_in(struct rbuf_in *rb);
void rbuf_del_out(struct rbuf_out *rb);
