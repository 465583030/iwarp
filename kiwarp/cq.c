/*
 * Completion queue functions.
 *
 * $Id: cq.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "priv.h"
#include "util.h"

static inline int
next_index(int cur, int num_cqe)
{
    int next = cur+1;
    if (unlikely(next == num_cqe))
	    next = 0;
    return next;
}

cq_t *cq_create(struct user_context *uc, int num)
{
	cq_t *cq;

	iwarp_debug("%s: uc %p num %d", __func__, uc, num);
	if (num <= 0)
		return NULL;
	cq = kmalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return NULL;
	cq->cqe = kmalloc(num * sizeof(*cq->cqe), GFP_KERNEL);
	if (!cq->cqe) {
		kfree(cq);
		return NULL;
	}
	cq->num_cqe = num;
	cq->handle = uc->cq_list_next_handle++;
	cq->prod = 0;
	cq->cons = 0;
	cq->refcnt = 0;
	list_add(&cq->list, &uc->cq_list);
	return cq;
}

int cq_destroy(struct user_context *uc, cq_t *cq)
{
	if (cq->refcnt != 0)
		return -EBUSY;
	list_del(&cq->list);
	kfree(cq->cqe);
	kfree(cq);
	return 0;
}

static int cq_num_occupied(cq_t *cq)
{
	int prod = cq->prod;

	if (prod < cq->cons)
		prod += cq->num_cqe;
	return prod - cq->cons;
}

int cq_isfull(cq_t *cq)
{
	return cq_num_occupied(cq) >= cq->num_cqe - 1;
}

/*
 * Put an entry into cqe[prod].  But if that would cause the
 * prod index to advance so that prod == cons, declare overflow.
 */
int cq_produce(cq_t *cq, const cqe_t *cqe)
{
	int nextprod = next_index(cq->prod, cq->num_cqe);

	if (unlikely(nextprod == cq->cons))
		return -ENOSPC;
	cq->cqe[cq->prod] = *cqe;  /* struct copy */
	cq->prod = nextprod;
	return 0;
}

int cq_consume(cq_t *cq, cqe_t *cqe)
{
	if (cq->prod == cq->cons)
		return -EAGAIN;
	*cqe = cq->cqe[cq->cons];  /* struct copy */
	cq->cons = next_index(cq->cons, cq->num_cqe);
	return 0;
}

void cq_get(cq_t *cq)
{
	++cq->refcnt;
}

void cq_put(cq_t *cq)
{
	--cq->refcnt;
}

cq_t *cq_lookup(struct user_context *uc, u64 handle)
{
	cq_t *cq;

	list_for_each_entry(cq, &uc->cq_list, list)
		if (cq->handle == handle)
			return cq;

	return NULL;
}

