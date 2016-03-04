/*
 * RDMAP header.
 *
 * $Id: rdmap.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __RDMAP_H
#define __RDMAP_H

#include <stdint.h>

#include "common.h"
#include "iwsk.h"
#include "mem.h"
#include "ddp.h"

/* opcodes accoring to spec */
typedef enum {
	RDMA_WRITE = 0,
	RDMA_READ_REQ,
	RDMA_READ_RESP,
	SEND,
	SEND_INV,
	SEND_SE,
	SEND_SE_INV,
	TERMINATE
} rdmap_t;
#define NUMOPS (TERMINATE + 1)

typedef enum {
	OP_RDMA_WRITE = 0,
	OP_RDMA_READ,
	OP_SEND,
	OP_RECV,
	OP_ERR
} rdmap_op_t;

typedef enum {
	RDMAP_SUCCESS = 0,
	RDMAP_FAILURE = -1,
} rdmap_compl_t;

typedef enum {
	SEND_Q=0,
	RDMAREQ_Q=1,
	TERM_Q=2,
} qn_t;
#define NUM_Q (3)

/* IWarp spec says messages can be maximum 2^32 */
typedef uint32_t msg_len_t;

typedef struct rdmap_sk {
	socket_t sk; /* socket */
	rdmap_sk_ent_t *ent; /* rdmap fields */
} rdmap_sk_t;

typedef uint8_t rdmap_control_field_t;

typedef struct rdmap_rdma_rd_req_hdr {
	stag_t sink_stag;
	tag_offset_t sink_to;
	msg_len_t rdma_rd_sz;
	stag_t src_stag;
	tag_offset_t src_to;
} rdmap_rdma_rd_req_hdr_t;

typedef struct {
	uint32_t term_control;
	uint16_t ddp_segment_len;
	uint8_t  headers[46];  /* max ddp + rdma possible */
} rdmap_term_msg_t;

/* control field in term messages */
#define rdmap_term_get_layer(c)  (((c) & 0xf0000000) >> 28)
#define rdmap_term_get_etype(c)  (((c) & 0x0f000000) >> 24)
#define rdmap_term_get_ecode(c)  (((c) & 0x00ff0000) >> 16)
#define rdmap_term_is_hdrct_m(c)  ((c) & 0x00008000)
#define rdmap_term_is_hdrct_d(c)  ((c) & 0x00004000)
#define rdmap_term_is_hdrct_r(c)  ((c) & 0x00002000)

/* parsing rdmap control field, assuming RV is in least significant bits */
#define rdmap_get_RV(c) (((c) & 0xc0) >> 6)	/* rdmap version number */
#define rdmap_get_RSVD(c) (((c) & 0x30) >> 4)/* reserved; set to 0 at src */
#define rdmap_get_OPCODE(c) ((c) & 0xf)		/* opcode */

/* filling up rdmap control field */
#define rdmap_set_RV(c) ((c) = ((c) | 0x40))	/* rdmap version number */
#define rdmap_set_RSVD(c) ((c) = ((c) & 0xcf))  /* reserved = 0 at src */
#define rdmap_set_OPCODE(c, o) ((c) = (((c) & 0xf0) | ((o) & 0x0f)))

int rdmap_init(void);

int rdmap_fin(void);

int rdmap_register_sock(socket_t sock,cq_t *scq, cq_t *rcq);

int rdmap_deregister_sock(socket_t sock);

int rdmap_mpa_use_markers(socket_t sock, int use);

int rdmap_mpa_use_crc(socket_t sock, int use);

int rdmap_set_sock_attrs(socket_t sock, int use_mrkr, int use_crc);

int rdmap_init_startup(socket_t sock, bool_t is_initiator, const char *pd_in,
		       char *pd_out, pd_len_t rpd_len);

int rdmap_send(socket_t sock, const void *msg, uint32_t msg_len, cq_wrid_t id);

int rdmap_post_recv(socket_t sock, void *buf, msg_len_t len, cq_wrid_t id);

static inline int rdmap_poll(void) { return ddp_poll();}

int rdmap_rdma_read(socket_t sk, stag_t sink_stag, tag_offset_t sink_to,
		    msg_len_t rdma_rd_sz, stag_t src_stag,
		    tag_offset_t src_to, cq_wrid_t id);

int rdmap_rdma_write(socket_t sock, stag_t stag, tag_offset_t to,
                     const void *msg, uint32_t msg_len, cq_wrid_t id);

void rdmap_process_recv(iwsk_t *s, qnum_t qn, msn_t msn);

inline void *rdmap_get_tag_sink(iwsk_t *s, stag_t stag, tag_offset_t to,
                                size_t len, rdmap_control_field_t cf);

buf_t *rdmap_get_untag_sink(iwsk_t *s, qnum_t qn, msn_t msn);

inline stag_acc_t rdmap_get_acc(rdmap_control_field_t cf);

void rdmap_untag_recv(iwsk_t *s, rdmap_control_field_t cf, stag_t stag,
		      qnum_t qn, msn_t msn, msg_len_t len);

void rdmap_tag_recv(iwsk_t *iwsk, rdmap_control_field_t cf, stag_t stag,
		    msg_len_t len);


#endif /* __RDMAP_H */
