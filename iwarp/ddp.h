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

#include <stdint.h>
#include "mem.h"
#include "common.h"
#include "iwsk.h"
#include "mpa.h"

typedef uint16_t llp_hdr_t;
typedef uint8_t ddp_control_field_t;
typedef uint64_t tag_offset_t;
typedef uint32_t msg_offset_t;

#define DDP_CF_DV 0x1
#define DDP_CF_TAGGED 0x80

/* parsing ddp control field assuming tagged bit is least sig bit */
#define ddp_is_TAGGED(c)	((c) & 0x80)
#define ddp_get_TAGGED(c)	(((c) & 0x80) >> 7)
#define ddp_is_LAST(c)		((c) & 0x40)
#define ddp_get_LAST(c)		(((c) & 0x40) >> 6)
#define ddp_get_RSVD(c)		(((c) & 0x3c) >> 2) /* this can be ignored */
#define ddp_get_DV(c)		((c) >> 0x3)

/* ddp_control_field fill up macros */
#define ddp_set_TAGGED(c)	((c) = ((c) | 0x80))
#define ddp_set_UNTAGGED(c) ((c) = ((c) & ~(0x80)))
#define ddp_set_LAST(c)		((c) = ((c) | 0x40))
#define ddp_set_NOTLAST(c)  ((c) = ( (c) & ~(0x40)))
#define ddp_set_RSVD(c)  ((c) = ((c) & 0xc3)) /* per specs RSVD = 0 */
#define ddp_set_DV(c)  	((c) = ((c) | 0x1)) /* DV = 1 */

/* See ddp rfc Sec 6.2 */
typedef struct ddp_tagged_hdr {
	llp_hdr_t llp_hdr;	    /* mpa hdr */
	ddp_control_field_t cf;	    /* control field */
	uint8_t rsvdulp; 	    /* reserved for ULP */
	stag_t stag;  		    /* steering tag for data sink's buffer */
	tag_offset_t to; 	    /* offset within the tagged buffer */
} ddp_tagged_hdr_t;

/* See ddp rfc Sec 6.3 */
typedef struct ddp_untagged_hdr {
	llp_hdr_t llp_hdr;	    /* mpa hdr */
	ddp_control_field_t cf;     /* control field */
	uint8_t ulp_ctrl;  	    /* reserved for ULP, it is 40-bits long */
	uint32_t ulp_payld; 	    /* reserved for ULP, rest of rsvdulp */
	qnum_t qn; 		    /* queue number of sink's buffer*/
	msn_t msn; 		    /* message number */
	msg_offset_t mo; 	    /* message offset*/
} ddp_untagged_hdr_t;

typedef struct ddp_hdr_start {
	llp_hdr_t llp_hdr;
	ddp_control_field_t cf;
	uint8_t ulp_ctrl;
} ddp_hdr_start_t;

/* initialize structures */
void ddp_init(void);
void ddp_fin(void);
int ddp_register_sock(iwsk_t *iwsk);
void ddp_deregister_sock(iwsk_t *iwsk);

int ddp_set_sock_attrs(iwsk_t *iwsk);

static inline int
ddp_init_startup(iwsk_t *iwsk, bool_t is_initiator,
		 const char *pd_in, char *pd_out, pd_len_t rpd_len)
{
	return mpa_init_startup(iwsk, is_initiator, pd_in, pd_out, rpd_len);
}

int ddp_send_untagged(iwsk_t *iwsk, const void *msg, const uint32_t msg_len,
                      const qnum_t qn, const uint8_t ulp_ctrl,
		      const uint32_t ulp_payld);

int ddp_send_tagged(iwsk_t *iwsk, const void *msg, const uint32_t msg_len,
                    const uint8_t rsvdulp, const stag_t stag,
		    const tag_offset_t to);

static inline int ddp_poll(void) { return mpa_poll(); }
uint32_t ddp_get_max_hdr_sz(void);
uint32_t ddp_get_hdr_sz(void *b);
int ddp_get_sink(iwsk_t *sk, void *hdr, buf_t *b);
uint32_t ddp_get_ddpseg_len(const iwsk_t *iwsk);
void ddp_process_ulpdu(iwsk_t *iwsk, void *hdr);

#endif /* __DDP_H */
