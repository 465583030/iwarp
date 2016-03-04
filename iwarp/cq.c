/*
 * Completion queue functions.
 *
 * $Id: cq.c 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <stdlib.h>
#include <errno.h>
#include "util.h"
#include "cq.h"
#include "common.h"

static inline int
next_index(int cur, int num_cqe)
{
    int next = cur+1;
    if (unlikely(next == num_cqe))
	next = 0;
    return next;
}

cq_t *
cq_create(int num)
{
    cq_t *cq;

    if (num <= 0)
	return 0;
    cq = malloc(sizeof(*cq));
    if (!cq)
	return 0;
    cq->cqe = malloc(num * sizeof(*cq->cqe));
    if (!cq->cqe) {
	free(cq);
	return 0;
    }
    cq->num_cqe = num;
    cq->prod = 0;
    cq->cons = 0;
    return cq;
}

void
cq_destroy(cq_t *cq)
{
    free(cq->cqe);
    free(cq);
}

static int
cq_num_occupied(cq_t *cq)
{
	int prod = cq->prod;

	if (prod < cq->cons)
	    prod += cq->num_cqe;
	return prod - cq->cons;
}

int
cq_isfull(cq_t *cq)
{
	return cq_num_occupied(cq) >= cq->num_cqe - 1;
}

/*
 * Put an entry into cqe[prod].  But if that would cause the
 * prod index to advance so that prod == cons, declare overflow.
 */
int
cq_produce(cq_t *cq, const cqe_t *cqe)
{
    int nextprod = next_index(cq->prod, cq->num_cqe);

    if (unlikely(nextprod == cq->cons))
	return -ENOSPC;

    cq->cqe[cq->prod] = *cqe;  /* struct copy */
    cq->prod = nextprod;
    return 0;
}

int
cq_consume(cq_t *cq, cqe_t *cqe)
{
    if (cq->prod == cq->cons)
	return -ENOENT;
    *cqe = cq->cqe[cq->cons];  /* struct copy */
    cq->cons = next_index(cq->cons, cq->num_cqe);
    return 0;
}

