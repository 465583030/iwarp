/*
 * iwarp socket definition
 *
 * $Id: iwsk.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#ifndef __IWSK_H
#define __IWSK_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include "cq.h"

typedef int socket_t;
typedef uint32_t qnum_t;
typedef uint32_t msn_t;
typedef uint32_t marker_pos_t;
typedef uint32_t stream_pos_t;

typedef enum {
	IWSK_TERMINATED = 0,
	IWSK_VALID
} iwsk_state_t;

typedef enum {
	IWSK_RDMAP=0, 	/* RDMAP layer */
	IWSK_DDP=1,	/* DDP layer */
	IWSK_MPA=2,	/* MPA layer */
} iwsk_layer_t;

/*
 * logical end point from rdmap's perspective.
 */
typedef struct {
	struct list_head buf_qs[3]; /* buf Qs for send, rdma req, & term msg */
	struct list_head rwrq; 	/* q for pend. recv work req; tagged msg */
	msn_t sink_msn; /* cur msn at sink. only for untagged messages */
} rdmap_sk_ent_t;

/*
 * logical end point from ddp's perspective.
 */
typedef struct ddp_sk_ent{
	msn_t recv_msn; /* recv msn seq num */
	msn_t send_msn; /* send msn seq num */
	uint32_t ddp_sgmnt_len; /* size of ddp segment on this socket */
	struct list_head outst_tag; /* outst. untagged mesg. placed in-order */
	struct list_head outst_untag; /* outstanding tagged messages */
} ddp_sk_ent_t;

/*
 * logical end point of a connection, from mpa's perspective.
 */
typedef struct mpa_sk_ent {
	int use_crc;	 /* is crc_used ? */
	int use_mrkr; /* are markers used? */
	marker_pos_t recv_mp; /* recv marker position */
	marker_pos_t send_mp; /* send marker position */
	stream_pos_t recv_sp; /* recv stream position */
	stream_pos_t send_sp; /* send stream position */
} mpa_sk_ent_t;

/* iwsk: stores all info about an iwarp socket */
typedef struct iwarp_sock {
	iwsk_state_t state;		/* valid or terminated state */
	struct file *filp;
	struct socket *sock;
	cq_t *scq; /* send comp q */
	cq_t *rcq; /* recv comp q */;
	spinlock_t lock;
	rdmap_sk_ent_t rdmapsk;
	ddp_sk_ent_t ddpsk;
	mpa_sk_ent_t mpask;
} iwsk_t;

iwsk_state_t iwsk_get_state(iwsk_t *iwsk);

void iwsk_set_state(iwsk_t *iwsk, iwsk_state_t state);

void iwsk_inc_rdmapsk_sink_msn(iwsk_t *iwsk);

void iwsk_inc_ddpsk_recv_msn(iwsk_t *iwsk);

msn_t iwsk_get_ddpsk_recv_msn(iwsk_t *iwsk);

void iwsk_inc_ddpsk_send_msn(iwsk_t *iwsk);

msn_t iwsk_get_ddpsk_send_msn(iwsk_t *iwsk);

void iwsk_set_mpask_crc(iwsk_t *iwsk, int use_crc);

void iwsk_set_mpask_mrkr(iwsk_t *iwsk, int use_mrkr);

void iwsk_set_mpask_crc_mrkr(iwsk_t *iwsk, int use_crc, int use_mrkr);

stream_pos_t iwsk_get_mpask_recv_sp(iwsk_t *iwsk);

void iwsk_add_mpask_recv_sp(iwsk_t *iwsk, stream_pos_t delta);

stream_pos_t iwsk_get_mpask_send_sp(iwsk_t *iwsk);

void iwsk_add_mpask_send_sp(iwsk_t *iwsk, stream_pos_t delta);

#endif /* __IWSK_H */
