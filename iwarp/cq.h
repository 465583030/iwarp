/*
 * Completion queue header.
 *
 * $Id: cq.h 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __CQ_H
#define __CQ_H

#include <stdint.h>
#include "common.h"

typedef uint64_t cq_wrid_t;

/*
 * We might return this to users directly, perhaps with a renaming, or we
 * might keep this the private structure only.
 */
typedef struct {
    int op;
    int status;
    cq_wrid_t id;
    uint32_t msg_len;
} cqe_t;

/*
 * Real CQs will be passed around with this structure, but do not manipulate
 * the fields except via calls in cq.c.  User code above the verbs layer will
 * not see this type.
 *
 * A CQ is a circular array with producer and consumer indices that chase
 * each other around the ring.
 */
typedef struct {
    cqe_t *cqe;   /* array */
    int num_cqe;
    int prod;     /* pointers into array */
    int cons;
} cq_t;

cq_t *cq_create(int num);
void cq_destroy(cq_t *cq);
int cq_isfull(cq_t *cq);
int cq_produce(cq_t *cq, const cqe_t *cqe);
int cq_consume(cq_t *cq, cqe_t *cqe);

#endif  /* __CQ_H */

