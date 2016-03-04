/*
 * DDP header.
 *
 * $Id: ddp.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __DDP_H
#define __DDP_H

#include <linux/types.h>
#include "priv.h"
#include "iwsk.h"
#include "mpa.h"
#include "mem.h"


typedef uint16_t llp_hdr_t; /* lower layer protocol header */
typedef uint8_t ddp_cf_t; /* ddp control field */
typedef uint64_t tag_offset_t; /* tag offset */
typedef uint32_t msg_offset_t; /* message offset */

/*
 * Header for tagged messages. See ddp rfc Sec 6.2
 */
typedef struct ddp_tag_hdr {
	llp_hdr_t	llp_hdr;	/* mpa hdr */
	ddp_cf_t cf;			/* control field */
	uint8_t rsvdulp;		/* reserved for ULP */
	stag_t stag;  		/* steering tag for data sink's buffer */
	tag_offset_t to; 		/* offset within the tagged buffer */
} ddp_tag_hdr_t;

/*
 * Header for untagged messages. See ddp rfc Sec 6.3
 */
typedef struct ddp_untag_hdr {
	llp_hdr_t llp_hdr;		/* mpa hdr */
	ddp_cf_t cf; 			/* control field */
	uint8_t ulp_ctrl;	/* reserved for ULP, it is 40-bits long */
	uint32_t ulp_payld; 	/* reserved for ULP, rest of rsvdulp */
	qnum_t qn; 			/* queue number of sink's buffer*/
	msn_t msn; 			/* message number */
	msg_offset_t mo; 		/* message offset*/
} ddp_untag_hdr_t;

/*
 * utility struct to capture start of header of either kind of message
 */
typedef struct ddp_hdr_start {
	llp_hdr_t llp_hdr;
	ddp_cf_t cf;
	uint8_t ulp_ctrl;
} ddp_hdr_start_t;

typedef enum {
	DDP_ELOCAL=0x0,	/* local catastrophic error */
	DDP_ETAG=0x1,	/* tagged buffer error */
	DDP_EUNTAG=0x2,	/* untag buffer error */
	DDP_ELLP=0x3	/* llp (mpa) error */
} ddp_err_t;

typedef enum {
	DDP_ESTAG=0x0,		/* invalid stag */
	DDP_EBOUNDS=0x1,	/* base and bounds violation */
	DDP_ESTAGASSOC=0x2,	/* stag stream association error :TODO */
	DDP_ETOWRAP=0x3,	/* tagged offset is wrapped */
	DDP_ETAGVER=0x4		/* invalid version */
} ddp_tagerr_t;

typedef enum {
	DDP_EQN=0x1,		/* invalid q num */
	DDP_EMSNBUFF=0x2, 	/* invalid msn, no buff availabale */
	DDP_EMSNRNG=0x3,	/* invalid msn, out of range */
	DDP_EMO=0x4,		/* invalid message offset (mo) */
	DDP_ELONG=0x5,		/* message too long */
	DDP_EUNTAGVER=0x6 	/* invalid version */
} ddp_untagerr_t;

#define DDP_CF_DV 0x1
#define DDP_CF_TAGGED 0x80

/* parsing utils for ddp control field assuming tagged bit is least sig bit */
#define ddp_is_TAGGED(c)	((c) & 0x80)
#define ddp_get_TAGGED(c)	(((c) & 0x80) >> 7)
#define ddp_is_LAST(c)		((c) & 0x40)
#define ddp_get_LAST(c)		(((c) & 0x40) >> 6)
#define ddp_get_RSVD(c)		(((c) & 0x3c) >> 2) /* this can be ignored */
#define ddp_get_DV(c)		((c) >> 0x3)

/* ddp_control_field filler utils */
#define ddp_set_TAGGED(c)	((c) = ((c) | 0x80))
#define ddp_set_UNTAGGED(c) ((c) = ((c) & ~(0x80)))
#define ddp_set_LAST(c)		((c) = ((c) | 0x40))
#define ddp_set_NOTLAST(c)  ((c) = ( (c) & ~(0x40)))
#define ddp_set_RSVD(c)  ((c) = ((c) & 0xc3)) /* per specs RSVD = 0 */
#define ddp_set_DV(c)  	((c) = ((c) | 0x1)) /* DV = 1 */

/* Exact fit for 1500-byte frames: 1444 = 1500 - 40 - 12 - 4 */
#define DDP_SEGLEN (1444)

int ddp_open(void);

void ddp_close(void);

int ddp_register_sock(iwsk_t *iwsk);

void ddp_deregister_sock(iwsk_t *iwsk);

static inline int ddp_init_startup(iwsk_t *iwsk, int is_initiator,
				   const char __user *pd_in, pd_len_t len_in,
				   char __user *pd_out, pd_len_t len_out)
{
	return mpa_init_startup(iwsk, is_initiator, pd_in, len_in, pd_out,
				len_out);
}

uint32_t ddp_get_max_hdr_sz(void);

uint32_t ddp_get_hdr_sz(void *b);

int ddp_get_sink(struct user_context *uc, iwsk_t *iwsk, void *hdr,
                 struct kvec *blks, int num_blks_alloc, uint32_t *bidx,
		 uint32_t *cp);

int ddp_process_ulpdu(struct user_context *uc, iwsk_t *iwsk, void *hdr);

int ddp_send_utm(iwsk_t *iwsk, stag_desc_t *sd, const void __user *msg,
                 uint32_t msg_len, qnum_t qn, uint8_t ulp_ctrl,
		 uint32_t ulp_payld);

int ddp_send_tm(iwsk_t *iwsk, stag_desc_t *sd, const void __user *msg,
                uint32_t msg_len, uint8_t rsvdulp, stag_t sink_stag,
		tag_offset_t sink_to);

static inline int ddp_poll(struct user_context *uc, iwsk_t *iwsk)
{
	return mpa_poll(uc, iwsk);
}

int ddp_surface_llp_err(iwsk_t *iwsk, iwsk_layer_t layer, uint8_t etype,
		        uint8_t ecode);

#endif /* __DDP_H */
