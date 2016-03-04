/*
 * iwarp socket end point
 *
 * $Id: iwsk.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#ifndef __IWSK_H
#define __IWSK_H

#include <stdint.h>

#include "util.h"
#include "common.h"
#include "list.h"
#include "cq.h"

#define MAX_SOCKETS (128)

typedef int socket_t;
typedef uint32_t qnum_t;
typedef uint32_t msn_t;
typedef uint32_t marker_pos_t;
typedef uint32_t stream_pos_t;

/*
 * iwarp is designed for message oriented communication paradigm. An end
 * point is represented by socket, send/recv marker positions, buffer queues
 * message sequence numbers etc.
 *
 * TODO: Should buffers be associated with a stream? Or be multiplexed
 * between different streams? What about multithreaded programs?
 */

/* This struct represents a stream connection end point from rdmap's
 * perspective.
 *
 * XXX: recv work req q simulates q in threaded model where verbs
 * enqueues the requests and rdmap consumes the request. Correspoding send
 * work req q is currently not implemented since, the rdmap send calls are
 * blocking and calls have sufficient info for generating corresponding cqes.
 */
typedef struct rdmap_sk_ent {
	struct list_head buf_qs[3]; /* buffer Qs for send, rdma req, & term messages */
	struct list_head rwrq; /* q for pending recv work request. For tagged messages */
	msn_t sink_msn; /* cur msn at sink. only for untagged messages */
} rdmap_sk_ent_t;

/* This struct represents a stream connection end point from ddp's
 * perspective.
 */
typedef struct ddp_sk_ent {
	msn_t recv_msn; /* recv msn seq num */
	msn_t send_msn; /* send msn seq num */
	uint32_t ddp_sgmnt_len; /* size of ddp segment on this socket */
	struct list_head outst_tag; /* outst. untagged mesg. placed in-order */
	struct list_head outst_untag; /* outstanding tagged messages */
} ddp_sk_ent_t;

/* This struct represents a logical end point of a connection, from mpa's
 * perspective.
 */
typedef struct mpa_sk_ent {
	bool_t use_crc;	 /* is crc_used ? */
	bool_t use_mrkr; /* are markers used? */
	marker_pos_t recv_mp; /* recv marker position */
	marker_pos_t send_mp; /* send marker position */
	stream_pos_t recv_sp; /* recv stream position */
	stream_pos_t send_sp; /* send stream position */
	uint32_t mss;		/* max seg. size on this socket */
	uint32_t skidx;		/* index of this socket in pollsks array */
} mpa_sk_ent_t;

/* socket from iwarp protocol perspective */
typedef struct iwsk {
	socket_t sk;
	cq_t *scq;	/* send comp q */
	cq_t *rcq;	/* recv comp q */
	rdmap_sk_ent_t rdmapsk;
	ddp_sk_ent_t ddpsk;
	mpa_sk_ent_t mpask;
} iwsk_t;

inline void iwsk_init(void);
inline void iwsk_fin(void);
inline void iwsk_insert(socket_t sock);
inline void iwsk_delete(socket_t sock);
inline iwsk_t *iwsk_lookup(socket_t sock);

#endif /* __IWSK_H */
