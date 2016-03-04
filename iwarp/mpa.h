/*
 * MPA header.
 *
 * $Id: mpa.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#ifndef __MPA_H
#define __MPA_H

#include <stdint.h>
#include "common.h"
#include "iwsk.h"

/*#define MAX_PRIV_DATA 256*/


typedef uint16_t ulpdu_len_t;

typedef struct mpa_sk {
	socket_t sk; /* socket */
	mpa_sk_ent_t *ent; /* mpa fields */
} mpa_sk_t;

/* typedef for initial MPA regotiation */
typedef uint16_t pd_len_t;

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

void mpa_init(void);

void mpa_fin(void);

int mpa_register_sock(iwsk_t *s);

void mpa_deregister_sock(iwsk_t *s);

int mpa_init_startup(iwsk_t *iwsk, bool_t is_initiator, const char *pd_in,
                     char *pd_out, pd_len_t rpd_len);

int mpa_get_mulpdu(iwsk_t *iwsk);

int mpa_set_sock_attrs(iwsk_t *iwsk);

int mpa_send(iwsk_t *iwsk, void *ddp_hdr, uint32_t ddp_hdr_len,
             const void *ddp_payld, ulpdu_len_t ddp_payld_len);

int mpa_recv(iwsk_t *iwsk);

int mpa_poll_generic(int timeout);
static inline int mpa_poll(void)  { return mpa_poll_generic(0); }
static inline int mpa_block(void) { return mpa_poll_generic(-1); }

#endif /* __MPA_H */
