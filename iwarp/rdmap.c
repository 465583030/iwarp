/*
 * RDMAP impl.
 *
 * $Id: rdmap.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>  /* offsetof */
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <netinet/in.h>

#include "common.h"
#include "iwsk.h"
#include "rdmap.h"
#include "ddp.h"
#include "util.h"
#include "mem.h"
#include "cq.h"

typedef enum {
	RDMAP_WR_PENDING,
	RDMAP_WR_COMPLETE
} rdmap_wr_status_t;

typedef struct {
	struct list_head list;
	rdmap_rdma_rd_req_hdr_t h;
	buf_t buf;
} rdmap_rdma_read_req_t;

typedef struct {
	struct list_head list;
	buf_t buf;
	rdmap_term_msg_t m;
} rdmap_term_msg_container_t;

typedef struct {
	struct list_head list;
	cq_wrid_t id;
	buf_t buf;
} rdmap_untag_wrd_t;

typedef struct {
	struct list_head list;
	cq_wrid_t id;
	stag_t stag;
	rdmap_wr_status_t wr_status;
	msg_len_t len;
} rdmap_tag_wrd_t;

static const uint32_t NULL_STAG = 0;
static iwsk_t *last_send_sk = NULL;
static iwsk_t *last_recv_sk = NULL;
static stag_acc_t rdmap_acc[8];
static rdmap_op_t rdmap_sink_op[8];
static rdmap_op_t rdmap_src_op[8];

static void rdmap_reap_rwr(iwsk_t *iwsk, rdmap_control_field_t cf,
                           stag_t stag, msg_len_t len);

int
rdmap_init(void)
{
	iwsk_init();
	ddp_init();

	last_send_sk = last_recv_sk = NULL;

	rdmap_acc[RDMA_WRITE] = STAG_W;
	rdmap_acc[RDMA_READ_REQ] = STAG_R;
	rdmap_acc[RDMA_READ_RESP] = STAG_W;
	rdmap_acc[SEND] = STAG_RW; /* TODO: Sends should be STAQ_NOACC */
	rdmap_acc[SEND_INV] = STAG_RW;
	rdmap_acc[SEND_SE] = STAG_RW;
	rdmap_acc[SEND_SE_INV] = STAG_RW;
	rdmap_acc[TERMINATE] = STAG_R; /* terminates dont go above rdmap */

	rdmap_sink_op[RDMA_WRITE] = OP_ERR;
	rdmap_sink_op[RDMA_READ_REQ] = OP_ERR;
	rdmap_sink_op[RDMA_READ_RESP] = OP_RDMA_READ;
	rdmap_sink_op[SEND] = OP_RECV;
	rdmap_sink_op[SEND_INV] = OP_RECV; /* TODO: change when inv is done */
	rdmap_sink_op[SEND_SE] = OP_RECV;  /* TODO: change when se is done */
	rdmap_sink_op[SEND_SE_INV] = OP_RECV; /* TODO: change when done */
	rdmap_sink_op[TERMINATE] = OP_ERR;

	rdmap_src_op[RDMA_WRITE] = OP_RDMA_WRITE;
	rdmap_src_op[RDMA_READ_REQ] = OP_ERR;
	rdmap_src_op[RDMA_READ_RESP] = OP_ERR;
	rdmap_src_op[SEND] = OP_SEND;
	rdmap_src_op[SEND_INV] = OP_SEND; /* TODO: change when inv is done */
	rdmap_src_op[SEND_SE] = OP_SEND;  /* TODO: change when se is done */
	rdmap_src_op[SEND_SE_INV] = OP_SEND; /* TODO: change when done */
	rdmap_src_op[TERMINATE] = OP_ERR;

	return 0;
}

int
rdmap_fin(void)
{
	ddp_fin();
	iwsk_fin();
	last_send_sk = last_recv_sk = NULL;

	return 0;
}

int
rdmap_register_sock(socket_t sock, cq_t *scq, cq_t *rcq)
{
	int i;
	rdmap_sk_t s;
	iwsk_t *iwsk;
	int ret = -1;

	debug(4, "Inside registesr sock");

	iwsk_insert(sock);
	iwsk = iwsk_lookup(sock);
	iwsk->scq = scq;
	iwsk->rcq = rcq;

	ret = ddp_register_sock(iwsk);
	if (ret < 0)
		return ret;

	s.ent = &(iwsk->rdmapsk);
	for (i=0; i< NUM_Q; i++)
		INIT_LIST_HEAD(&s.ent->buf_qs[i]);
	INIT_LIST_HEAD(&s.ent->rwrq);
	s.ent->sink_msn = 1; /* init msn. FIXME: 1 due to Ammasso */

	return 0;
}

int
rdmap_deregister_sock(socket_t sock)
{
	iwsk_t *iwsk = iwsk_lookup(sock);
	if (!iwsk)
		return -EINVAL;

	ddp_deregister_sock(iwsk);

	iwsk_delete(sock);
	if (last_send_sk == iwsk)
		last_send_sk = NULL;
	if (last_recv_sk == iwsk)
		last_recv_sk = NULL;

	return 0;
}

/*
 * Control mpa markers and crc settings.
 */
int
rdmap_mpa_use_markers(socket_t sock, int use)
{
	iwsk_t *iwsk = iwsk_lookup(sock);
	if (!iwsk)
		return -EINVAL;
	iwsk->mpask.use_mrkr = use;
	return 0;
}

int
rdmap_mpa_use_crc(socket_t sock, int use)
{
	iwsk_t *iwsk = iwsk_lookup(sock);
	if (!iwsk)
		return -EINVAL;
	iwsk->mpask.use_crc = use;
	return 0;
}

int
rdmap_set_sock_attrs(socket_t sock, int use_mrkr, int use_crc)
{
	iwsk_t *iwsk = iwsk_lookup(sock);
	if (!iwsk)
		return -EINVAL;
	iwsk->mpask.use_crc = use_crc;
	iwsk->mpask.use_mrkr = use_mrkr;
	return 0;
}

int
rdmap_init_startup(socket_t sock, bool_t is_initiator, const char *pd_in,
                   char *pd_out, pd_len_t rpd_len)
{
    iwsk_t *iwsk;
    iwsk = iwsk_lookup(sock);  /*look up the socket object */
    if (!iwsk)
		return -EINVAL;
    return ddp_init_startup(iwsk, is_initiator, pd_in, pd_out, rpd_len);
}

int
rdmap_send(socket_t sock, const void *msg, uint32_t msg_len, cq_wrid_t id)
{
	int ret;
	rdmap_control_field_t cf = 0;
	cqe_t cqe;

	rdmap_set_RV(cf);
	rdmap_set_OPCODE(cf, SEND);

	if (!last_send_sk || last_send_sk->sk != sock)
		last_send_sk = iwsk_lookup(sock);
	if (!last_send_sk)
		return -EINVAL;
	if (last_send_sk->scq && cq_isfull(last_send_sk->scq))
		return -ENOSPC;

	debug(3, "%s: sock %d msg %p len %d cf 0x%x", __func__,
	  last_send_sk->sk, msg, msg_len, cf);

	ret = ddp_send_untagged(last_send_sk, msg, msg_len, SEND_Q, cf,
							NULL_STAG);
	if (ret < 0)
		return ret;

	cqe.id = id;
	cqe.status = RDMAP_SUCCESS;
	cqe.op = rdmap_src_op[SEND];
	cqe.msg_len = msg_len;
	if (last_send_sk->scq)
		cq_produce(last_send_sk->scq, &cqe);

	return 0;
}

int
rdmap_post_recv(socket_t sock, void *buf, msg_len_t len, cq_wrid_t id)
{
	/* d is freed in rdmap_untag_recv */
	rdmap_untag_wrd_t *d = Malloc(sizeof(*d));

	if (!last_recv_sk || last_recv_sk->sk != sock)
		last_recv_sk = iwsk_lookup(sock);
	if (!last_recv_sk)
		return -EINVAL;
	if(last_recv_sk->rcq && cq_isfull(last_recv_sk->rcq))
		return -ENOSPC;

	d->id = id;
	d->buf.buf = buf;
	d->buf.len = len;

	/* recv ==> untagged buffer ==> send_q ==> Q num 0/SEND_Q */
	list_add_tail(&d->list, &last_recv_sk->rdmapsk.buf_qs[SEND_Q]);

	return 0;
}

inline stag_acc_t
rdmap_get_acc(rdmap_control_field_t cf)
{
	return rdmap_acc[rdmap_get_OPCODE(cf)];
}

void
rdmap_untag_recv(iwsk_t *iwsk, rdmap_control_field_t cf, stag_t stag,
                 qnum_t qn, msn_t msn __attribute__((unused)), msg_len_t len)
{
	cqe_t cqe;
	struct list_head *l;

	/* TODO: Ignore stag for now. It is to be used in SEND_*INV */
	stag = 0;

	/* ddp rfc Sec. 5.4 rdmap always receives messages in order */
	iw_assert(iwsk->rdmapsk.sink_msn == msn, "sink_msn (%d), msn (%d)",
			  iwsk->rdmapsk.sink_msn, msn);

	/* dequeue first entry */
	iw_assert(!list_empty(&iwsk->rdmapsk.buf_qs[qn]), "%s: empty qs %d",
			  __func__, qn);
	l = iwsk->rdmapsk.buf_qs[qn].next;
	list_del(l);

	if (qn == SEND_Q) {
		rdmap_untag_wrd_t *d = list_entry(l, rdmap_untag_wrd_t, list);
		if (iwsk->rcq) {
			cqe.id = d->id;
			cqe.status = RDMAP_SUCCESS;
			cqe.op = rdmap_sink_op[rdmap_get_OPCODE(cf)];
			cqe.msg_len = len;
			cq_produce(iwsk->rcq, &cqe);
		}
		free(d);

	} else if (qn == RDMAREQ_Q) {
		rdmap_rdma_read_req_t *d =
			list_entry(l, rdmap_rdma_read_req_t, list);
		rdmap_rdma_rd_req_hdr_t *h = &d->h;
		rdmap_control_field_t new_cf;

		void *b = mem_stag_location(iwsk, h->src_stag, h->src_to,
					    h->rdma_rd_sz,
					    rdmap_acc[rdmap_get_OPCODE(cf)]);
		/* TODO: Surface this error */
		if (!b)
			error("%s:%d b == NULL for rdma_rd_req, stag(%u),"
			      " to(%Lx), len(%u)", __FILE__, __LINE__,
			      h->src_stag, Lu(h->src_to), h->rdma_rd_sz);

		new_cf = 0;
		rdmap_set_RV(new_cf);
		rdmap_set_RSVD(new_cf);
		rdmap_set_OPCODE(new_cf, RDMA_READ_RESP);

		ddp_send_tagged(iwsk, b, h->rdma_rd_sz, new_cf, h->sink_stag,
						h->sink_to);

		free(d);
	} else if (qn == TERM_Q) {
	    rdmap_term_msg_container_t *td;
	    uint32_t control;

	    td = list_entry(l, typeof(*td), list);
	    control = ntohl(td->m.term_control);
	    warning("%s: terminate message: layer %d, etype %d\n"
	      "  ecode %d %s %s %s",
	      __func__,
	      rdmap_term_get_layer(control),
	      rdmap_term_get_etype(control),
	      rdmap_term_get_ecode(control),
	      rdmap_term_is_hdrct_m(control) ? "msglen-valid" : "",
	      rdmap_term_is_hdrct_d(control) ? "ddp-hdr-incl" : "",
	      rdmap_term_is_hdrct_r(control) ? "rdma-hdr-incl" : "");
	    /* allocated earlier in placement */
	    free(td);
	}

	iwsk->rdmapsk.sink_msn++;
}

inline void *
rdmap_get_tag_sink(iwsk_t *s, stag_t stag, tag_offset_t to, size_t len,
		   rdmap_control_field_t cf)
{
	return mem_stag_location(s, stag, to, len,
	                         rdmap_acc[rdmap_get_OPCODE(cf)]);
}

/* MSN arithmetic is module 2^32, i.e. unsigned int arith. */
buf_t *
rdmap_get_untag_sink(iwsk_t *s, qnum_t qn, msn_t msn)
{
	if (qn == SEND_Q) {
		rdmap_untag_wrd_t *d;
		struct list_head *l, *h;
		uint32_t diff;

		h = &s->rdmapsk.buf_qs[qn];
		if (list_empty(h))
			return NULL;

		diff = s->rdmapsk.sink_msn - msn;
		l = h->next;
		while (diff > 0) {
			--diff;
			l = h->next;
			if (l == h)
				return NULL;
		}

		d = list_entry(l, rdmap_untag_wrd_t, list);
		return &d->buf;

	} else if (qn == RDMAREQ_Q) {
		rdmap_rdma_read_req_t *d;
		iw_assert(list_empty(&s->rdmapsk.buf_qs[qn]),
			  "%s: expected empty list for RDMAREQ_Q", __func__);
		/*
		 * Auto-generate an entry to hold the incoming RDMA read
		 * request; freed in rdmap_untag_recv.
		 */
		d = Malloc(sizeof(*d));
		d->buf.buf = &d->h;
		d->buf.len = sizeof(d->h);
		memset(&d->h, 0, sizeof(d->h));
		list_add_tail(&d->list, &s->rdmapsk.buf_qs[qn]);
		return &d->buf;
	} else if (qn == TERM_Q) {
		rdmap_term_msg_container_t *td;
		td = Malloc(sizeof(*td));
		td->buf.buf = &td->m;
		td->buf.len = sizeof(td->m);
		list_add_tail(&td->list, &s->rdmapsk.buf_qs[qn]);
		return &td->buf;
	}
	return NULL;
}

/* rdma write */
int
rdmap_rdma_write(socket_t sock, stag_t stag, tag_offset_t to, const void *msg,
		 uint32_t msg_len, cq_wrid_t id)
{
	int ret;
	rdmap_control_field_t cf = 0;
	cqe_t cqe;

	rdmap_set_RV(cf);
	rdmap_set_OPCODE(cf, RDMA_WRITE);

	if (!last_send_sk || last_send_sk->sk != sock)
		last_send_sk = iwsk_lookup(sock);
	if (!last_send_sk)
		return -EINVAL;
	if (last_send_sk->scq && cq_isfull(last_send_sk->scq))
		return -ENOSPC;

	debug(3, "%s: sock %d msg %p len %d cf 0x%x", __func__,
	  last_send_sk->sk, msg, msg_len, cf);

	ret = ddp_send_tagged(last_send_sk, msg, msg_len, cf, stag, to);
	if (ret < 0)
		return ret;

	cqe.id = id;
	cqe.status = RDMAP_SUCCESS;
	cqe.op = rdmap_src_op[RDMA_WRITE];
	cqe.msg_len = msg_len;
	if (last_send_sk->scq)
		cq_produce(last_send_sk->scq, &cqe);

	return 0;
}

/* recv for tagged messages */
void
rdmap_tag_recv(iwsk_t *iwsk, rdmap_control_field_t cf, stag_t stag,
               msg_len_t len)
{
	if (rdmap_get_OPCODE(cf) == RDMA_WRITE) {
		/* TODO: handle rdma write */
	}
	else if (rdmap_get_OPCODE(cf) == RDMA_READ_RESP) {
		if (list_empty(&iwsk->rdmapsk.rwrq))
			error("%s:%d rwrq is empty", __FILE__, __LINE__);
		rdmap_reap_rwr(iwsk, cf, stag, len);
	} else {
		/* TODO: Surface this error */
		error("%s:%d Invalid opcode (%d) for tagged msg",
			  __FILE__, __LINE__, rdmap_get_OPCODE(cf));
	}
}

/*
 * RDMA read requests may be satisfied by the remote peer in any order, but
 * we must "complete" them to the local user in the order they were
 * submitted.  Hence the different behavior depending on if it was the first
 * on the list or not.
 */
static void
rdmap_reap_rwr(iwsk_t *iwsk, rdmap_control_field_t cf, stag_t stag,
               msg_len_t len)
{

	rdmap_tag_wrd_t *d, *dp;
	int found = 0;
	/* find the just completed entry, mark it complete and update len */
	list_for_each_entry(d, &iwsk->rdmapsk.rwrq, list) {
		if (d->stag == stag) {
			found = 1;
			d->wr_status = RDMAP_WR_COMPLETE;
			d->len = len;
		}
	}

	if (!found)
		error("%s: no matching recv work request entry for stag %d",
		      __func__, stag);

	/*
	 * Try to generate completion queue entries for any that are
	 * finished, but they must complete in order.
	 */
	list_for_each_entry_safe(d, dp, &iwsk->rdmapsk.rwrq, list) {
		if (d->wr_status != RDMAP_WR_COMPLETE)
			break;

		if (iwsk->rcq) {
			cqe_t cqe;
			cqe.op = rdmap_sink_op[rdmap_get_OPCODE(cf)];
			cqe.status = RDMAP_SUCCESS;
			cqe.id = d->id;
			cqe.msg_len = d->len;
			cq_produce(iwsk->scq, &cqe); /* XXX: RDMA Read CQE on SCQ */
		}
		list_del(&d->list);
		free(d);
	}
}

/*
 * Issue an RDMA read request.  Add an entry to the list of outstanding
 * requests to be completed eventually by rdmap_reap_rwr().
 */
int
rdmap_rdma_read(socket_t sk, stag_t sink_stag, tag_offset_t sink_to,
		msg_len_t rdma_rd_sz, stag_t src_stag, tag_offset_t src_to,
		cq_wrid_t id)
{
	int ret;
	rdmap_rdma_rd_req_hdr_t h;
	rdmap_control_field_t cf;
	rdmap_tag_wrd_t *d;

	memset(&h, 0, sizeof(rdmap_rdma_rd_req_hdr_t));
	h.sink_stag = sink_stag;
	h.sink_to = sink_to;
	h.rdma_rd_sz = rdma_rd_sz;
	h.src_stag = src_stag;
	h.src_to = src_to;

	cf = 0;
	rdmap_set_RV(cf);
	rdmap_set_RSVD(cf);
	rdmap_set_OPCODE(cf, RDMA_READ_REQ);

	if (!last_send_sk || last_send_sk->sk != sk)
		last_send_sk = iwsk_lookup(sk);
	if (!last_send_sk)
		return -EINVAL;
	if (last_send_sk->scq && cq_isfull(last_send_sk->scq))
		return -ENOSPC;

	d = Malloc(sizeof(*d));
	d->id = id;
	d->stag = sink_stag;
	d->wr_status = RDMAP_WR_PENDING;
	d->len = 0;
	list_add_tail(&d->list, &last_send_sk->rdmapsk.rwrq);

	ret = ddp_send_untagged(last_send_sk, &h, sizeof(h), RDMAREQ_Q, cf,
							NULL_STAG);
	if (ret < 0)
		return ret;

	return 0;
}


