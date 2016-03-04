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

typedef u64 cq_wrid_t;

/*
 * Private structure only.
 */
typedef struct {
    int op;
    int status;
    u64 id;
    u32 msg_len;
} cqe_t;

/* forward decl */
struct user_context;

/*
 * Real CQs will be passed around with this structure, but do not manipulate
 * the fields except via calls in cq.c.  User code above the verbs layer will
 * not see this type.
 *
 * A CQ is a circular array with producer and consumer indices that chase
 * each other around the ring.
 */
typedef struct {
    struct list_head list;  /* chained onto a given user_context */
    u64 handle;   /* cookie for userspace */
    cqe_t *cqe;   /* array */
    int num_cqe;
    int prod;     /* pointers into array */
    int cons;
    int refcnt;   /* users of this CQ */
} cq_t;

cq_t *cq_create(struct user_context *uc, int num);
int cq_destroy(struct user_context *uc, cq_t *cq);
int cq_isfull(cq_t *cq);
int cq_produce(cq_t *cq, const cqe_t *cqe);
int cq_consume(cq_t *cq, cqe_t *cqe);
void cq_get(cq_t *cq);
void cq_put(cq_t *cq);
cq_t *cq_lookup(struct user_context *uc, u64 handle);

#endif  /* __CQ_H */

