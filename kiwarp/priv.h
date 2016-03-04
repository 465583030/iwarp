/*
 * Private kernel shared data structures.
 *
 * $Id: priv.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __PRIV_H
#define __PRIV_H

#include <linux/list.h>
#include <net/sock.h>
#include "cq.h"
#include "ht.h"
#include "mem.h"

/*
 * Keep track of who has what open to kill data structures on release.  One
 * user per files structure.  Threads that share fd space should only have one
 * iwarp connection open.
 */
struct user_context {
	struct list_head list;
	struct list_head cq_list;
	u64 cq_list_next_handle;
	ht_t *fdhash;
	mem_manager_t *mm;
	int tgid;
};

/*
 * A posted receive buffer. There is user buffer (ubuf) and kernel buffer
 * (kbuf). A receive buffer can have either ubuf or kbuf as target but not
 * both.
 */
typedef struct {
	struct list_head list;
	uint64_t id; 		/* work request id */
	void __user *ubuf; 	/* user buf */
	struct stag_desc *sd;
	void *kbuf;		/* kernel buf */
	size_t len;
} recv_buf_t;

#endif  /* __PRIV_H */
