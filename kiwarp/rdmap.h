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

#include "priv.h"
#include "iwsk.h"
#include "ddp.h"
#include "user.h"
#include "mem.h"

typedef uint8_t rdmap_cf_t;
typedef uint32_t msg_len_t; /* iwarp spec says messages can be maximum 2^32 */

/*
 * opcodes according to spec
 */
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

/* operations to be executed at source and sink */
typedef enum {
	OP_RDMA_WRITE = 0,
	OP_RDMA_READ,
	OP_SEND,
	OP_RECV,
	OP_TERMINATE,
	OP_ERR
} rdmap_op_t;

typedef enum {
	SEND_Q=0,
	RDMAREQ_Q=1,
	TERM_Q=2,
} qn_t;
#define NUM_Q (3)

typedef enum {
	RDMAP_SUCCESS=0, /* on success */
	RDMAP_FAILURE=1, /* on failure */
	RDMAP_TERMINATE=2 /* when terminate message is involved */
} rdmap_compl_t;

/* RDMAP error types */
typedef enum {
	RDMAP_ELOCAL=0x0,	/* local catastrophic error */
	RDMAP_EREMPROT=0x1,	/* remote protection error */
	RDMAP_EREMOP=0x2	/* remote operation error */
} rdmap_err_t;

/* RDMAP remote protection error codes */
typedef enum {
	RDMAP_ESTAG=0x0,	/* invalid stag */
	RDMAP_EBOUNDS=0x1,	/* base/bounds violation */
	RDMAP_EACCES=0x2,	/* access violation error */
	RDMAP_ESTAGASSOC=0x3,	/* stag stream association err :TODO*/
	RDMAP_ETOWRAP=0x4,	/* tag offset has wrapped */
	RDMAP_ERPSTAGDEST=0x9,	/* stag cannot be invalidated */
	RDMAP_ERPUNSPEC=0xFF	/* unspecified error */
} rdmap_rpe_t;

/* RDMAP remote operation error codes */
typedef enum {
	RDMAP_EVER=0x5,		/* invalid RDMAP version */
	RDMAP_EUNXOP=0x6,	/* unexpected opcode */
	RDMAP_ECATASLOCAL=0x7,	/* catastrophic error local to RDMAP stream */
	RDMAP_ECATASGLOBAL=0x8, /* global catastrophic error */
	RDMAP_EOPSTAGDEST=0x9,	/* stag cannot be invalidated */
	RDMAP_EOPUNSPEC=0xFF	/* unspecified error */
} rdmap_rope_t;

typedef struct rdmap_rdma_rd_req_hdr {
	stag_t sink_stag;
	tag_offset_t sink_to;
	msg_len_t len;
	stag_t src_stag;
	tag_offset_t src_to;
} rdmap_rdma_rd_req_hdr_t;

typedef struct {
	uint32_t tcf; /* terminate control field */
	uint16_t ddp_segment_len;
	uint8_t  ddphdr[18];  /* max ddp hdr */
	uint8_t  rdmahdr[28]; /* rdma read req header */
} rdmap_term_hdr_t;

#define NULL_STAG (-1) /* invalid stag */

/* control field parser term messages */
#define rdmap_term_get_layer(c)  (((c) & 0xf0000000) >> 28)
#define rdmap_term_get_etype(c)  (((c) & 0x0f000000) >> 24)
#define rdmap_term_get_ecode(c)  (((c) & 0x00ff0000) >> 16)
#define rdmap_term_is_hdrct_m(c)  ((c) & 0x00008000)
#define rdmap_term_is_hdrct_d(c)  ((c) & 0x00004000)
#define rdmap_term_is_hdrct_r(c)  ((c) & 0x00002000)

/* control field setters */
#define rdmap_term_set_layer(c, l)\
	((c) = (((c) & 0x0fffffff) | ((l | 0x00000000) << 28)))
#define rdmap_term_set_etype(c, e)\
	((c) = (((c) & 0xf0ffffff) | ((e | 0x00000000) << 24)))
#define rdmap_term_set_ecode(c, e)\
	((c) = (((c) & 0xff00ffff) | ((e | 0x00000000) << 16)))
#define rdmap_term_set_hdrct_m(c)   ((c) = ((c) | 0x00008000))
#define rdmap_term_unset_hdrct_m(c) ((c) = ((c) & 0xffff7fff))
#define rdmap_term_set_hdrct_d(c)   ((c) = ((c) | 0x00004000))
#define rdmap_term_unset_hdrct_d(c) ((c) = ((c) & 0xffffb000))
#define rdmap_term_set_hdrct_r(c)   ((c) = ((c) | 0x00002000))
#define rdmap_term_unset_hdrct_r(c) ((c) = ((c) & 0xffffd000))

/* parsing utils for rdmap control field, assuming RV is in least sig. bits */
#define rdmap_get_RV(c) (((c) & 0xc0) >> 6)	/* rdmap version number */
#define rdmap_get_RSVD(c) (((c) & 0x30) >> 4)/* reserved; set to 0 at src */
#define rdmap_get_OPCODE(c) ((c) & 0xf)		/* opcode */

/* filler utils for rdmap control field */
#define rdmap_set_RV(c) ((c) = ((c) | 0x40))	/* rdmap version number */
#define rdmap_set_RSVD(c) ((c) = ((c) & 0xcf))  /* reserved = 0 at src */
#define rdmap_set_OPCODE(c, o) ((c) = (((c) & 0xf0) | ((o) & 0x0f)))

int rdmap_open(void);

void rdmap_close(void);

int rdmap_register_sock(struct user_context *uc, int fd, cq_t *scq,
			cq_t *rcq);

int rdmap_set_sock_attrs(struct user_context *uc, int fd, int use_crc,
			 int use_mrkr);

void rdmap_release_sock_res(void *x);

int rdmap_deregister_sock(struct user_context *uc, int fd);

int rdmap_init_startup(struct user_context *uc, int fd, int is_initiator,
		       const char __user *pd_in, pd_len_t len_in,
		       char __user *pd_out, pd_len_t len_out);

int rdmap_poll(struct user_context *uc, cq_t *cq,
	       struct work_completion __user *uwc);
int rdmap_poll_block(struct user_context *uc, cq_t *cq,
	             int fd, struct work_completion __user *uwc);

recv_buf_t *rdmap_get_untag_sink(iwsk_t *iwsk, qnum_t qn, msn_t msn);

recv_buf_t *rdmap_get_tag_sink(struct user_context *uc, stag_t stag,
			       tag_offset_t to, size_t len, rdmap_cf_t cf,
			       uint32_t *offset);

int rdmap_send(struct user_context *uc, int fd, uint64_t id,
	       void __user *ubuf, size_t len, stag_t stag);

int rdmap_post_recv(struct user_context *uc, int fd, uint64_t id,
		    void __user *ubuf, size_t len, stag_t stag);

int rdmap_untag_recv(struct user_context *uc, iwsk_t *iwsk, rdmap_cf_t cf,
		     stag_t stag, qnum_t qn, msn_t msn, msg_len_t len);

int rdmap_tag_recv(iwsk_t *iwsk, rdmap_cf_t cf, stag_t stag, msg_len_t len);

int rdmap_rdma_write(struct user_context *uc, int fd, uint64_t id,
		     void __user *ubuf, size_t len, stag_t local_stag,
		     stag_t sink_stag, tag_offset_t sink_to);

int rdmap_rdma_read(struct user_context *uc, int fd, uint64_t id,
		    stag_t sink_stag, tag_offset_t sink_to, msg_len_t len,
		    stag_t src_stag, tag_offset_t src_to);

static inline int rdmap_encourage(struct user_context *uc,
                                  struct iwarp_sock *iwsk)
{
	return ddp_poll(uc, iwsk);
}

int rdmap_surface_ddp_err(iwsk_t *iwsk, uint8_t layer, uint8_t etype,
			  uint8_t ecode, void *ddphdr, size_t ddphdrsz,
			  ssize_t ddpsglen, void *rdmahdr);

#endif /* __RDMAP_H */
