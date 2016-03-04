/*
 * DDP impl.
 *
 * $Id: ddp.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/socket.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <endian.h>
#include <errno.h>

/* IP_MTU is defined in linux/in.h, but linux/in.h conflicts with
 * netinit/in.h so we do a brute force redefinition of IP_MTU.
 */
#ifdef linux
#	ifndef IP_MTU
#		define IP_MTU 14
#	endif
#endif

#include "common.h"
#include "iwsk.h"
#include "mpa.h"
#include "ddp.h"
#include "rdmap.h"
#include "util.h"
#include "ht.h"

typedef struct {
	struct list_head list;
	msg_len_t len;
	stag_t stag;
}ddp_tag_msg_t;

typedef struct {
	struct list_head list;
	msg_len_t len;
	msn_t msn;
	qnum_t qn;
}ddp_untag_msg_t;


extern int rdma_write_count;

static ulpdu_len_t UNTAGGED_PAYLD_LEN = 0;
static ulpdu_len_t TAGGED_PAYLD_LEN = 0;
static const ulpdu_len_t UNTAGGED_HDR_SZ = sizeof(ddp_untagged_hdr_t);
static const ulpdu_len_t TAGGED_HDR_SZ = sizeof(ddp_tagged_hdr_t);
static const ulpdu_len_t max_hdr_sz =
sizeof(ddp_untagged_hdr_t) > sizeof(ddp_tagged_hdr_t)
	? sizeof(ddp_untagged_hdr_t) : sizeof(ddp_tagged_hdr_t);

static inline uint64_t swab64(uint64_t x)
{
	uint32_t h = x >> 32;
	uint32_t l = x & ((1ULL<<32)-1);
	return (((uint64_t) ntohl(l)) << 32) | ((uint64_t) ntohl(h));
}

#if (__BYTE_ORDER == __LITTLE_ENDIAN)
#  define ntohq(x) swab64(x)
#  define htonq(x) swab64(x)
#else
#  define ntohq(x) (x)
#  define htonq(x) (x)
#endif

inline void
ddp_init(void)
{
	mpa_init();
}

inline void
ddp_fin(void)
{
	mpa_fin();
}

int
ddp_register_sock(iwsk_t *iwsk)
{
	ddp_sk_ent_t *skent = &iwsk->ddpsk;
	int ret = -1;

	ret = mpa_register_sock(iwsk);
	if (ret < 0)
		return ret;

	skent->recv_msn = 1; /* init msn. FIXME: 1 due to Ammasso */
	skent->send_msn = 1; /* init msn. FIXME: 1 due to Ammasso */
	skent->ddp_sgmnt_len = mpa_get_mulpdu(iwsk);

	UNTAGGED_PAYLD_LEN = iwsk->ddpsk.ddp_sgmnt_len - UNTAGGED_HDR_SZ;
	TAGGED_PAYLD_LEN = iwsk->ddpsk.ddp_sgmnt_len - TAGGED_HDR_SZ;
	INIT_LIST_HEAD(&skent->outst_tag);
	INIT_LIST_HEAD(&skent->outst_untag);

	return 0;
}

void
ddp_deregister_sock(iwsk_t *iwsk)
{
	/* TODO: Surface this error */
	if (!list_empty(&iwsk->ddpsk.outst_tag))
		error("%s: outstanding tagged messages exist", __func__);
	if (!list_empty(&iwsk->ddpsk.outst_untag))
		error("%s: outstanding untagged messages exist", __func__);

	mpa_deregister_sock(iwsk);
}

int
ddp_set_sock_attrs(iwsk_t *iwsk)
{
	iwsk = NULL;
	return 0;
}

int
ddp_send_untagged(iwsk_t *iwsk, const void *msg, const uint32_t msg_len,
				  const qnum_t qn, const uint8_t ulp_ctrl, const uint32_t
				  ulp_payld)
{
	uint32_t i = 0;
	uint32_t mo = 0; /* ddp rfc Sec. 4.3 */
	const void *ddp_payld = NULL;
	ulpdu_len_t ddp_payld_len = 0;
	int ret;
	uint32_t num_sgmnts;
	ddp_untagged_hdr_t ut_hdr;

	memset(&ut_hdr, 0, UNTAGGED_HDR_SZ);
	ut_hdr.cf = DDP_CF_DV;
	ut_hdr.ulp_ctrl = ulp_ctrl;
	ut_hdr.ulp_payld = htonl(ulp_payld);
	ut_hdr.qn = htonl(qn);
	ut_hdr.msn = htonl(iwsk->ddpsk.send_msn);

	num_sgmnts = (msg_len + UNTAGGED_PAYLD_LEN-1) / UNTAGGED_PAYLD_LEN;
	if (unlikely(num_sgmnts == 0))
	    num_sgmnts = 1;

	for (i=0; i<num_sgmnts; i++) {

		ut_hdr.mo = htonl(mo);
		ddp_payld = (const uint8_t *) msg + mo;

		if (num_sgmnts - 1 == i) {
			ddp_set_LAST(ut_hdr.cf);
			ddp_payld_len = msg_len - mo;
		} else {
			ddp_payld_len = UNTAGGED_PAYLD_LEN;
			mo += UNTAGGED_PAYLD_LEN;
		}

		ret = mpa_send(iwsk, &ut_hdr, UNTAGGED_HDR_SZ, ddp_payld,
					   ddp_payld_len);
		if (ret < 0)
			return ret;
	}

	iwsk->ddpsk.send_msn++; /* update send msn */
	return 0;
}


int
ddp_send_tagged(iwsk_t *iwsk, const void *msg, const uint32_t msg_len,
				const uint8_t rsvdulp, const stag_t stag,
				const tag_offset_t to)
{
	uint32_t i = 0;
	tag_offset_t off = 0;
	const void *pp = NULL; /* payload pointer */
	ulpdu_len_t len = 0;
	int ret;
	uint32_t num_sgmnts = (msg_len + TAGGED_PAYLD_LEN-1)
		/ TAGGED_PAYLD_LEN;
	ddp_tagged_hdr_t t_hdr;

	memset(&t_hdr, 0, TAGGED_HDR_SZ);
	t_hdr.cf = DDP_CF_TAGGED | DDP_CF_DV;
	t_hdr.rsvdulp = rsvdulp;
	t_hdr.stag = htonl(stag);

	for (i=0; i<num_sgmnts; i++) {

		t_hdr.to = htonq(to + off);
		pp = (const uint8_t *) msg + off;

		if (num_sgmnts - 1 == i) {
			ddp_set_LAST(t_hdr.cf);
			len = msg_len - off;
		} else {
			len = TAGGED_PAYLD_LEN;
			off += TAGGED_PAYLD_LEN;
		}

		debug(4, "%s: to %Lx stag %d len %d", __func__,
		  ntohq(t_hdr.to), stag, len);

		ret = mpa_send(iwsk, &t_hdr, TAGGED_HDR_SZ, pp, len);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* TODO */
inline uint32_t
ddp_get_ddpseg_len(const iwsk_t *iwsk)
{
	debug(2, "ut_len = %d", iwsk->ddpsk.ddp_sgmnt_len);
	return iwsk->ddpsk.ddp_sgmnt_len;
}

inline uint32_t
ddp_get_max_hdr_sz(void)
{
	return max_hdr_sz;
}

inline uint32_t
ddp_get_hdr_sz(void *b)
{
	ddp_hdr_start_t *s = (ddp_hdr_start_t *)b;
	if (ddp_is_TAGGED(s->cf))
		return TAGGED_HDR_SZ;
	else
		return UNTAGGED_HDR_SZ;
}

int
ddp_get_sink(iwsk_t *sk, void *hdr, buf_t *b)
{
	ddp_hdr_start_t *s = (ddp_hdr_start_t *)hdr;
	if (ddp_get_TAGGED(s->cf)) {
		tag_offset_t to;
		stag_t stag;

		ddp_tagged_hdr_t *h = (ddp_tagged_hdr_t *)hdr;

		/* first 2 bytes makeup  mpa hdr */
		b->len = ntohs(h->llp_hdr) - (TAGGED_HDR_SZ - 2);
		to = ntohq(h->to);
		stag = ntohl(h->stag);

		b->buf = rdmap_get_tag_sink(sk, stag, to, b->len, h->rsvdulp);
		/* TODO: Surface this error */
		if (!b->buf) {
			printerr("%s: no sink for stag %d (%x) to 0x%Lx (0x%Lx) len %zu",
			  __func__, stag, h->stag, Ld(to), Lu(h->to), b->len);
			return -EBADMSG;
		}
		debug(4, "%s (TAGGED): to %Lx stag %d len %d", __func__, to, stag,
		  b->len);
	}
	else {
		qnum_t qn;
		msg_offset_t mo;
		msn_t msn;

		ddp_untagged_hdr_t *h = (ddp_untagged_hdr_t *)hdr;

		qn = ntohl(h->qn);
		msn = ntohl(h->msn);
		mo = ntohl(h->mo);

		buf_t *utbuf = rdmap_get_untag_sink(sk, qn, msn);
		/* TODO: Surface this error */
		if (utbuf == NULL) {
			printerr("%s utbuf is NULL", __func__);
			return -EBADMSG;
		}

		/* h->llp_hdr == ddp_payld_len + ddphdr_len. It does not include
		 * markers, pad and crc len. first 2 bytes makeup mpa hdr.
		 */
		b->len = ntohs(h->llp_hdr) - (UNTAGGED_HDR_SZ - 2);
		/* TODO: Surface this error */
		if ((mo + b->len) > utbuf->len) {
			printerr("%s:%d (mo + b->len)(%zu) > utbuf->len(%zu)",
					 __FILE__, __LINE__, mo + b->len,  utbuf->len);
			return -EBADMSG;
		}
		b->buf = (uint8_t *)utbuf->buf + mo;
	}

	return 0;
}

void
ddp_process_ulpdu(iwsk_t *iwsk, void *hdr)
{
	ddp_hdr_start_t *s = (ddp_hdr_start_t *)hdr;
	msg_len_t len;
	int found;

	if (ddp_is_TAGGED(s->cf)) {
		ddp_tagged_hdr_t *h = (ddp_tagged_hdr_t *)hdr;
		ddp_tag_msg_t *tm;

		h->stag = ntohl(h->stag);
		/* XXX: h->to is unused; important? */
		h->to = ntohq(h->to);
		/* first 2 bytes makeup mpa hdr */
		h->llp_hdr = ntohs(h->llp_hdr);
		len = h->llp_hdr - (TAGGED_HDR_SZ-2);

		/*
		 * See if it already exists on the list of outstanding
		 * tagged messages; if not, add it.  Unless if this happens
		 * to be the last frame in a message, in which case it
		 * can be completed (sent to rdmap) immediately.
		 */
		found = 0;
		list_for_each_entry(tm, &iwsk->ddpsk.outst_tag, list) {
			if (tm->stag == h->stag) {
				tm->len += len;
				found = 1;
				break;
			}
		}
		if (!found) {
			if (ddp_is_LAST(s->cf))
				/* we will complete it immediately */
				tm = NULL;
			else {
				/* build a new entry for later */
				tm = Malloc(sizeof(*tm));
				tm->stag = h->stag;
				tm->len = len;
				list_add_tail(&tm->list, &iwsk->ddpsk.outst_tag);
			}
		}
		if (ddp_is_LAST(s->cf)) {
			/* use total saved length, or len of just this one
			 * frame that is the first and last of the message */
			if (tm)
				len = tm->len;
			rdmap_tag_recv(iwsk, h->rsvdulp, h->stag, len);
			if (tm) {
				list_del(&tm->list);
				free(tm);
			}
			rdma_write_count++;

		}

	} else
	{
		/* TODO: Untagged message is completed */
		ddp_untagged_hdr_t *h = (ddp_untagged_hdr_t *)hdr;
		ddp_untag_msg_t *utm;

		h->ulp_payld = ntohl(h->ulp_payld);
		h->qn = ntohl(h->qn);
		h->msn = ntohl(h->msn);
		/* XXX: h->mo is unused; important? */
		h->mo = ntohl(h->mo);
		/* first 2 bytes makeup mpa hdr */
		h->llp_hdr = ntohs(h->llp_hdr);
		len = h->llp_hdr - (UNTAGGED_HDR_SZ-2);

		/* XXX: just surface the error, do not assert and fail */
		iw_assert(iwsk->ddpsk.recv_msn == h->msn,
				  "%s:%d iwsk->ddpsk.recv_msn (%d) != h->msn(%d)",
				  __FILE__, __LINE__, iwsk->ddpsk.recv_msn, h->msn);
		found = 0;
		list_for_each_entry(utm, &iwsk->ddpsk.outst_untag, list) {
			if (utm->qn == h->qn && utm->msn == h->msn) {
				utm->len += len;
				found = 1;
				break;
			}
		}
		if (!found) {
			if (ddp_is_LAST(s->cf))
				utm = NULL;
			else {
				utm = Malloc(sizeof(*utm));
				utm->qn = h->qn;
				utm->msn = h->msn;
				utm->len = len;
				list_add_tail(&utm->list, &iwsk->ddpsk.outst_untag);
			}
		}

		if (ddp_is_LAST(s->cf)) {
			if (utm)
				len = utm->len;
			/* XXX: ulp_ctrl and ulp_payld arrive in the last
			 * packet of a message?  or the first?  */
			rdmap_untag_recv(iwsk, h->ulp_ctrl, h->ulp_payld,
							 h->qn, h->msn, len);
			iwsk->ddpsk.recv_msn++; /* update recv msn */
			if (utm) {
				list_del(&utm->list);
				free(utm);

			}
		}
	}
}

