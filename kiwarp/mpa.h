/*
 * marker PDU aligned framing layer, definitions
 *
 * $Id: mpa.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#ifndef __MPA_H
#define __MPA_H

#include <linux/types.h>
#include "iwsk.h"
#include "priv.h"

typedef uint16_t ulpdu_len_t; /* ULP (ddp) length */
typedef uint16_t pd_len_t; /* private data length for MPA negotiation phase */

typedef enum {
	MPA_ECONNRESET=0x1, 	/* underlying tcp connection closed */
	MPA_EINVCRC=0x2, 	/* crc does not match */
	MPA_EMRKR=0x3,		/* mrkr does not point to fpdu start */
	MPA_EINVRRF=0x4		/* invalid MPA req reply frame */
} mpa_err_t;

#define mpa_get_M(c) (((c) & 0x80) >> 7)
#define mpa_get_C(c) (((c) & 0x40) >> 6)
#define mpa_get_R(c) (((c) & 0x20) >> 5)
#define mpa_get_Res(c) (((c) & 0x1f))
#define mpa_get_Rev(c) (((c) & 0xff00) >> 8)
#define mpa_get_PD_Length(c) (ntohs((c) >> 16))

#define mpa_set_M(c) ((c) = ((c) | 0x80))
#define mpa_unset_M(c) ((c) = ((c) & ~(0x80)))
#define mpa_set_C(c) ((c) = ((c) | 0x40))
#define mpa_unset_C(c) ((c) = ((c) & ~(0x40)))
#define mpa_set_R(c) ((c) = ((c) | 0x20))
#define mpa_unset_R(c) ((c) = ((c) & ~(0x20)))
#define mpa_set_Res(c) ((c) = ((c) & 0xe0))
#define mpa_set_Rev(c, v) ((c) = ((c) | ((v) << 8)))
#define mpa_set_PD_Length(c, v) ((c) = ((c) | (htons(v) << 16)))

int mpa_open(void);

void mpa_close(void);

int mpa_register_sock(iwsk_t *s);

void mpa_deregister_sock(iwsk_t *s);

int mpa_init_startup(iwsk_t *iwsk, int is_initiator, const char __user *pd_in,
		     pd_len_t in_len, char __user *pd_out, pd_len_t out_len);

int mpa_send(iwsk_t *iwsk, void *ddp_hdr, const uint32_t ddp_hdr_len,
		    const struct kvec *ddp_payldv, int num_ddp_payldv,
		    uint32_t ddp_payld_len);

int mpa_poll(struct user_context *uc, iwsk_t *iwsk);

#endif /* __MPA_H */
