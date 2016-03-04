/*
 * RDMAP impl.
 *
 * $Id: rdmap.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include "util.h"
#include "user.h"
#include "rdmap.h"
#include "priv.h"
#include "iwsk.h"
#include "ddp.h"
#include "mem.h"

typedef enum{
	RDMAP_WR_PENDING,
	RDMAP_WR_COMPLETE
} rdmap_wr_status_t;

typedef struct {
	struct list_head list;
	rdmap_rdma_rd_req_hdr_t h;
	buf_t buf;
} rdmap_rdma_read_req_t;

/*typedef struct {
	struct list_head list;
	buf_t buf;
	rdmap_term_hdr_t m;
} rdmap_term_msg_container_t;
*/
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


static stag_acc_t rdmap_acc[NUMOPS];
static rdmap_op_t rdmap_sink_op[NUMOPS];
static rdmap_op_t rdmap_src_op[NUMOPS];

int rdmap_open(void)
{
	int ret = 0;

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

	ret = ddp_open();

	return ret;
}

void rdmap_close(void)
{
	ddp_close();
}

int rdmap_register_sock(struct user_context *uc, int fd, cq_t *scq, cq_t *rcq)
{
	struct file *filp;
	struct inode *inode;
	struct socket *sock;
	struct iwarp_sock *iwsk;
	int ret = 0;

	iwarp_debug("%s", __func__);
	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	inode = filp->f_dentry->d_inode;
	if (!S_ISSOCK(inode->i_mode)) {
		ret = -ENOTSOCK;
		goto out_fput;
	}
	sock = SOCKET_I(inode);
	if (sock->type != SOCK_STREAM) {
		ret = -EPROTOTYPE;
		goto out_fput;
	}

	iwsk = kmalloc(sizeof(*iwsk), GFP_KERNEL);
	if (!iwsk)
		goto out_fput;
	iwsk->state = IWSK_VALID;
	spin_lock_init(&(iwsk->lock));
	iwsk->filp = filp;
	iwsk->sock = sock;
	iwsk->scq = scq;
	iwsk->rcq = rcq;
	cq_get(scq);
	cq_get(rcq);
	INIT_LIST_HEAD(&(iwsk->rdmapsk.buf_qs[SEND_Q]));
	INIT_LIST_HEAD(&(iwsk->rdmapsk.buf_qs[RDMAREQ_Q]));
	INIT_LIST_HEAD(&(iwsk->rdmapsk.buf_qs[TERM_Q]));
	INIT_LIST_HEAD(&(iwsk->rdmapsk.rwrq));
	iwsk->rdmapsk.sink_msn = 1; /* 1 mesg is consumed by handshake */

	ret = ddp_register_sock(iwsk);
	if (ret < 0) {
		kfree(iwsk);
		goto out_fput;
	}

	ret = ht_insert(filp, iwsk, uc->fdhash);
	if (ret < 0) {
		kfree(iwsk);
		cq_put(scq);
		cq_put(rcq);
		goto out_fput;
	}
	/* hold onto the reference of struct file while it is in the hash */
	goto out;

    out_fput:
    	fput(filp);
    out:
	return ret;
}

int rdmap_set_sock_attrs(struct user_context *uc, int fd, int use_crc,
			 int use_mrkr)
{
	int ret;
	struct file *filp;
	iwsk_t *iwsk = NULL;

	iwarp_debug("%s: uc %p fd %d crc %d marker %d", __func__, uc, fd,
	            use_crc, use_mrkr);
	filp = fget(fd);
	if (!filp) {
		ret = -EINVAL;
		goto out;
	}
	ret = ht_lookup((void *)filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0) {
		ret = -ENOENT;
		goto out;
	}

	iwsk->mpask.use_crc = use_crc;
	iwsk->mpask.use_mrkr = use_mrkr;
	fput(filp); /* release local reference */
out:
	return ret;
}

void rdmap_release_sock_res(void *x)
{
	struct iwarp_sock *iwsk = x;
	recv_buf_t *rb, *rbnext;
	rdmap_tag_wrd_t *d, *dnext;

	/* free any pending resources allocated by ddp */
	ddp_deregister_sock(iwsk);

	/* delete posted recv entries. */
	list_for_each_entry_safe(rb, rbnext, &(iwsk->rdmapsk.buf_qs[SEND_Q]),
				 list) {
		kfree(rb);
	}
	/* delete any pending recv from tag list */
	list_for_each_entry_safe(d, dnext, &(iwsk->rdmapsk.rwrq), list) {
		kfree(d);
	}
	cq_put(iwsk->scq);
	cq_put(iwsk->rcq);
	fput(iwsk->filp); /* release our long-standing reference.*/
	kfree(iwsk);
}

int rdmap_deregister_sock(struct user_context *uc, int fd)
{
	int ret = 0;
	struct file *filp;

	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_delete_callback(filp, uc->fdhash, rdmap_release_sock_res);
	fput(filp); /* release local reference */
out:
	return ret;
}

int rdmap_init_startup(struct user_context *uc, int fd, int is_initiator,
		       const char __user *pd_in, pd_len_t len_in,
		       char __user *pd_out, pd_len_t len_out)
{
	int ret = 0;
	struct file *filp = NULL;
	iwsk_t *iwsk = NULL;

	iwarp_debug("%s", __func__);
	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_lookup((void *)filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0) {
		ret = -ENOENT;
		goto out;
	}
	ret = ddp_init_startup(iwsk, is_initiator, pd_in, len_in, pd_out,
			       len_out);
out:
	return ret;
}


int rdmap_send(struct user_context *uc, int fd, uint64_t id,
	       void __user *ubuf, size_t len, stag_t stag)
{
	int ret;
	struct file *filp;
	struct iwarp_sock *iwsk;
	cqe_t cqe;
	stag_desc_t *sd;
	rdmap_cf_t cf = 0;

	iwarp_debug("%s: uc %p fd %d id %Ld ubuf %p len %zu stag %d", __func__,
	            uc, fd, id, ubuf, len, stag);
	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_lookup(filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0)
		goto out_fput;

	sd = mem_stag_desc(stag, ubuf, len, STAG_R, uc->mm);
	if (!sd) {
		ret = -EINVAL;
		goto out_fput;
	}

	rdmap_set_RV(cf);
	rdmap_set_OPCODE(cf, SEND);
	ret = ddp_send_utm(iwsk, sd, ubuf, len, SEND_Q, cf, NULL_STAG);
	if (ret < 0)
		goto out_fput;
	cqe.id = id;
	cqe.status = RDMAP_SUCCESS;
	cqe.op = rdmap_src_op[SEND];
	cqe.msg_len = len;
	ret = cq_produce(iwsk->scq, &cqe);

out_fput:
	fput(filp);
out:
	return ret;
}


int rdmap_post_recv(struct user_context *uc, int fd, uint64_t id,
		     void __user *ubuf, size_t len, stag_t stag)
{
	int ret;
	struct file *filp;
	struct iwarp_sock *iwsk;
	recv_buf_t *rb;
	struct stag_desc *sd;

	iwarp_debug("%s: uc %p fd %d id %Ld ubuf %p len %zu stag %d", __func__,
	            uc, fd, id, ubuf, len, stag);
	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_lookup(filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0)
		goto out_fput;

	/*
	 * Make sure buffer fits in stag and store stag descriptor
	 * in structure for use at receive time.
	 */
	sd = mem_stag_desc(stag, ubuf, len, STAG_W, uc->mm);
	if (!sd) {
		ret = -EINVAL;
		goto out_fput;
	}

	/* rb is freed normally in rdmap_untag_recv &
	 * finally in rdmap_forget_sock_cb*/
	rb = kmalloc(sizeof(*rb), GFP_KERNEL);
	if (!rb) {
		ret = -ENOMEM;
		goto out_fput;
	}
	memset(rb, 0, sizeof(rb));
	rb->id = id;
	rb->ubuf = ubuf;
	rb->sd = sd;
	rb->len = len;
	list_add_tail(&rb->list, &(iwsk->rdmapsk.buf_qs[SEND_Q]));

out_fput:
	fput(filp);
out:
	return ret;
}


int rdmap_rdma_write(struct user_context *uc, int fd, uint64_t id,
		     void __user *ubuf, size_t len, stag_t local_stag,
		     stag_t sink_stag, tag_offset_t sink_to)
{
	int ret;
	struct file *filp;
	struct iwarp_sock *iwsk;
	cqe_t cqe;
	stag_desc_t *sd;
	rdmap_cf_t cf;

	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_lookup(filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0)
		goto out_fput;

	sd = mem_stag_desc(local_stag, ubuf, len, STAG_R, uc->mm);
	if (!sd) {
		ret = -EINVAL;
		goto out_fput;
	}

	cf = 0;
	rdmap_set_RV(cf);
	rdmap_set_OPCODE(cf, RDMA_WRITE);
	ret = ddp_send_tm(iwsk, sd, ubuf, len, cf, sink_stag, sink_to);
	if (ret < 0)
		goto out_fput;
	cqe.id = id;
	cqe.status = RDMAP_SUCCESS;
	cqe.op = rdmap_src_op[RDMA_WRITE];
	cqe.msg_len = len;
	ret = cq_produce(iwsk->scq, &cqe);

out_fput:
	fput(filp);
out:
	return ret;

}

/*
 * Issue an RDMA read request.  Add an entry to the list of outstanding
 * requests to be completed eventually by rdmap_reap_rwr().
 */
int rdmap_rdma_read(struct user_context *uc, int fd, uint64_t id,
		    stag_t sink_stag, tag_offset_t sink_to, msg_len_t len,
		    stag_t src_stag, tag_offset_t src_to)
{
	int ret = 0;
	struct file *filp;
	rdmap_rdma_rd_req_hdr_t h;
	rdmap_cf_t cf;
	rdmap_tag_wrd_t *d = NULL;
	iwsk_t *iwsk = NULL;

	iwarp_debug("%s", __func__);
	filp = fget(fd);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}
	ret = ht_lookup(filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0)
		goto out_fput;

	memset(&h, 0, sizeof(h));
	h.sink_stag = sink_stag;
	h.sink_to = sink_to;
	h.len = len;
	h.src_stag = src_stag;
	h.src_to = src_to;

	cf = 0;
	rdmap_set_RV(cf);
	rdmap_set_RSVD(cf);
	rdmap_set_OPCODE(cf, RDMA_READ_REQ);

	d = kmalloc(sizeof(*d), GFP_KERNEL); /* freed in rdmap_reap_rwr */
	if (!d) {
		ret = -ENOMEM;
		goto out_fput;
	}
	d->id = id;
	d->stag = sink_stag;
	d->wr_status = RDMAP_WR_PENDING;
	d->len = 0;
	list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);
	ret = ddp_send_utm(iwsk, NULL, &h, sizeof(h), RDMAREQ_Q, cf, NULL_STAG);
out_fput:
	fput(filp);
out:
	return ret;
}


static recv_buf_t *rdmap_get_send_sink(iwsk_t *iwsk, qnum_t qn, msn_t msn)
{
	struct list_head *cur, *next;
	uint32_t diff = 0;
	recv_buf_t *rb;

	cur = &(iwsk->rdmapsk.buf_qs[qn]);
	if (list_empty(cur))
		return NULL;

	diff = iwsk->rdmapsk.sink_msn - msn;
	next = cur->next;
	while (diff > 0) {
		--diff;
		next = cur->next;
		if (next == cur)
			return NULL;
	}
	rb = list_entry(next, recv_buf_t, list);
	return rb;
}

/* rrr: rdma read request */
static recv_buf_t *rdmap_get_rrr_sink(iwsk_t *iwsk, qnum_t qn, msn_t msn)
{
	recv_buf_t *rb;

	/* will be freed in rdmap_recv_rrr */
	rdmap_rdma_rd_req_hdr_t *r = kmalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;
	memset(r, 0, sizeof(*r));

	/* will be freed in rdmap_recv_rrr */
	rb = kmalloc(sizeof(*rb), GFP_KERNEL);
	if (!rb)
		return NULL;
	memset(rb, 0, sizeof(*rb));
	rb->id = (uint64_t)(unsigned long) r;
	rb->kbuf = r;
	rb->len = sizeof(*r);
	list_add_tail(&rb->list, &iwsk->rdmapsk.buf_qs[qn]);
	return rb;
}

static recv_buf_t *rdmap_get_term_sink(iwsk_t *iwsk, qnum_t qn, msn_t msn)
{
	recv_buf_t *rb;

	/* will be freed in rdmap_recv_term_msg */
	rdmap_term_hdr_t *th = kmalloc(sizeof(*th), GFP_KERNEL);
	if (!th)
		return NULL; /* TODO: surface RDMAP_ECATASGLOBAL */
	memset(th, 0, sizeof(*th));

	/* will be freed in rdmap_recv_term_msg */
	rb = kmalloc(sizeof(*rb), GFP_KERNEL);
	if (!rb)
		return NULL; /* TODO: surface RDMAP_ECATASGLOBAL */
	memset(rb, 0, sizeof(*rb));
	rb->id = (uint64_t)(unsigned long) th;
	rb->kbuf = th;
	rb->len = sizeof(*th);
	list_add_tail(&rb->list, &iwsk->rdmapsk.buf_qs[qn]);
	return rb;
}

recv_buf_t *rdmap_get_untag_sink(iwsk_t *iwsk, qnum_t qn, msn_t msn)
{
	if (qn == SEND_Q) {
		return rdmap_get_send_sink(iwsk, qn, msn);
	} else if (qn == RDMAREQ_Q) {
		return rdmap_get_rrr_sink(iwsk, qn, msn);
	} else if (qn == TERM_Q) {
		return rdmap_get_term_sink(iwsk, qn, msn);
	} else {
		return NULL; /* invalid qn */
	}
}

recv_buf_t *rdmap_get_tag_sink(struct user_context *uc, stag_t stag,
				tag_offset_t to, size_t len, rdmap_cf_t cf,
				uint32_t *offset)
{
	struct stag_desc *sd;
	recv_buf_t *rb;
	int op = rdmap_get_OPCODE(cf);

	iwarp_debug("%s: stag %d to %Lx len %zu cf %d op %d", __func__, stag,
	            to, len, cf, op);
	if (!(op == RDMA_WRITE || op == RDMA_READ_RESP)) {
		iwarp_info("%s: bad op %d", __func__, op);
		return NULL;
	}

	sd = mem_stag_desc(stag, (void *)(unsigned long)to, len,
	                   rdmap_acc[op], uc->mm);
	if (!sd) {
		iwarp_info("%s: no sd for stag %d to %Lx len %zu op %d",
		           __func__, stag, to, len, op);
		return NULL;
	}

	rb = kmalloc(sizeof(*rb), GFP_KERNEL);
	rb->ubuf = sd->start;
	rb->sd = sd;
	rb->len = sd->len;
	*offset = to - (uint64_t)(unsigned long) sd->start;
	return rb;
}

static int rdmap_recv_send(iwsk_t *iwsk, rdmap_cf_t cf, qnum_t qn, msn_t msn,
			   msg_len_t len)
{
	int ret = 0;
	struct list_head *first = iwsk->rdmapsk.buf_qs[qn].next;
	recv_buf_t *rb = list_entry(first, recv_buf_t, list);

	if (iwsk->rcq) {
		cqe_t cqe;
		cqe.id = rb->id;
		cqe.status = RDMAP_SUCCESS;
		cqe.op = rdmap_sink_op[rdmap_get_OPCODE(cf)];
		cqe.msg_len = len;
		ret = cq_produce(iwsk->rcq, &cqe);
	}
	list_del(first);
	kfree(rb);
	iwsk_inc_rdmapsk_sink_msn(iwsk);
	return ret;
}

/* rrr: rdma read request */
static int rdmap_recv_rrr(struct user_context *uc, iwsk_t *iwsk, rdmap_cf_t cf,
			  qnum_t qn, msn_t msn, msg_len_t len)
{
	int ret = 0;
	int op = rdmap_get_OPCODE(cf);
	struct list_head *first = iwsk->rdmapsk.buf_qs[qn].next;
	recv_buf_t *rb = list_entry(first, recv_buf_t, list);
	rdmap_rdma_rd_req_hdr_t *r = rb->kbuf;
	rdmap_cf_t resp_cf;
	stag_desc_t *sd;

	iwarp_info("%s", __func__);
	if (op != RDMA_READ_REQ) {
		ret = -EBADMSG; /* TODO: surface RDMAP_EUNXOP */
		goto out_free;
	}
	sd = mem_stag_desc(r->src_stag, (void *)(unsigned long) r->src_to,
	                   r->len, rdmap_acc[op], uc->mm);
	if (!sd) {
		ret = -EINVAL; /* TODO: surface RDMAP_ECATASGLOBAL */
		goto out_free;
	}
	resp_cf = 0;
	rdmap_set_RV(resp_cf);
	rdmap_set_RSVD(resp_cf);
	rdmap_set_OPCODE(resp_cf, RDMA_READ_RESP);
	ret = ddp_send_tm(iwsk, sd, (void *)(unsigned long) r->src_to, r->len,
	                  resp_cf, r->sink_stag, r->sink_to);
out_free:
	kfree(r);
	kfree(rb);
	iwsk_inc_rdmapsk_sink_msn(iwsk);
	return ret;

}

/* recv terminate message. generate a cqe & invalidate iwsk */
static int rdmap_recv_term_msg(struct user_context *uc, iwsk_t *iwsk,
			       rdmap_cf_t cf, qnum_t qn, msn_t msn,
			       msg_len_t len)
{
	int ret = 0;
	int op = rdmap_get_OPCODE(cf);
	struct list_head *first = iwsk->rdmapsk.buf_qs[qn].next;
	recv_buf_t *rb = list_entry(first, recv_buf_t, list);
	rdmap_term_hdr_t *th = rb->kbuf;
	cqe_t cqe;

	iwarp_info("%s", __func__);
	if (op != TERMINATE) {
		ret = -EBADMSG; /* TODO: surface RDMAP_EUNXOP */
		goto out_free;
	}
	cqe.op = OP_TERMINATE;
	cqe.status = RDMAP_TERMINATE;
	cqe.id = 0;
	cqe.msg_len = 0;
	if (iwsk->rcq)
		ret = cq_produce(iwsk->rcq, &cqe); /* post on rcq */
	if (!ret && iwsk->scq && iwsk->scq != iwsk->scq)
		ret = cq_produce(iwsk->scq, &cqe); /* FIXME: also on scq */
	iwsk->state = IWSK_TERMINATED; /* terminate iwsk */
out_free:
	kfree(th);
	kfree(rb);
	iwsk_inc_rdmapsk_sink_msn(iwsk);
	return ret;


}

int rdmap_untag_recv(struct user_context *uc, iwsk_t *iwsk, rdmap_cf_t cf,
		     stag_t stag, qnum_t qn, msn_t msn, msg_len_t len)
{
	/* XXX: ignore stag for now; will be used in SEND_*INV */
	stag = 0;

	if (qn == SEND_Q) {
		return rdmap_recv_send(iwsk, cf, qn, msn, len);
	} else if (qn == RDMAREQ_Q) {
		return rdmap_recv_rrr(uc, iwsk, cf, qn, msn, len);
	} else if (qn == TERM_Q) {
		return rdmap_recv_term_msg(uc, iwsk, cf, qn, msn, len);
	} else {
		return -EINVAL; /* TODO: surface DDP_EQN */
	}
}

/*
 * RDMA read requests may be satisfied by the remote peer in any order, but
 * we must "complete" them to the local user in the order they were
 * submitted.  Hence the different behavior depending on if it was the first
 * on the list or not.
 */
static int rdmap_reap_rwr(iwsk_t *iwsk, rdmap_cf_t cf, stag_t stag,
			  msg_len_t len)
{
	int ret = 0;
	int found = 0;
	int op = rdmap_get_OPCODE(cf);
	rdmap_tag_wrd_t *d, *dp;

	if (op != RDMA_READ_RESP) {
		ret = -EBADMSG; /* TODO: surface the error */
		goto out;
	}
	/* find the just completed entry, mark it complete and update len */
	list_for_each_entry(d, &iwsk->rdmapsk.rwrq, list) {
		if (d->stag == stag) {
			found = 1;
			d->wr_status = RDMAP_WR_COMPLETE;
			d->len = len;
		}
	}
	if (!found) {
		ret = -EBADMSG;  /* TODO: surface the error */
		goto out;
	}

	/* Try to generate completion queue entries for any that are
	 * finished, but they must complete in order.  */
	list_for_each_entry_safe(d, dp, &iwsk->rdmapsk.rwrq, list) {
		if (d->wr_status != RDMAP_WR_COMPLETE)
			break;
		if (iwsk->rcq) {
			cqe_t cqe;
			cqe.op = rdmap_sink_op[op];
			cqe.status = RDMAP_SUCCESS;
			cqe.id = d->id;
			cqe.msg_len = d->len;
			ret = cq_produce(iwsk->scq, &cqe);
			if (ret < 0)
				goto out;
		}
		list_del(&d->list);
		kfree(d);
	}
out:
	return ret;
}


/* recv for tagged messages */
inline int rdmap_tag_recv(iwsk_t *iwsk, rdmap_cf_t cf, stag_t stag,
			  msg_len_t len)
{
	iwarp_debug("%s", __func__);
	if (rdmap_get_OPCODE(cf) == RDMA_WRITE) {
		return 0;
	} else if (rdmap_get_OPCODE(cf) == RDMA_READ_RESP) {
		if (list_empty(&iwsk->rdmapsk.rwrq))
			return -EBADMSG;
		return rdmap_reap_rwr(iwsk, cf, stag, len);
	} else {
		return -EINVAL; /* TODO: Surface this error */
	}
}

static inline int rdmap_check_cq(cq_t *cq, struct work_completion __user *uwc)
{
	int ret = 0;
	cqe_t cqe;
	struct work_completion wc;

	ret = cq_consume(cq, &cqe);
	if (ret == 0) {
		wc.id = cqe.id;
		wc.op = cqe.op;
		wc.status = cqe.status;
		wc.msg_len = cqe.msg_len;
		if (copy_to_user(uwc, &wc, sizeof(wc)))
			ret = -EFAULT;
	}
	return ret;
}


/* poll cq; if there is cqe return it to user */
int rdmap_poll(struct user_context *uc, cq_t *cq,
	       struct work_completion __user *uwc)
{
	int ret = 0;
	iwarp_debug("%s", __func__);

	ret = rdmap_check_cq(cq, uwc); /* check for pre-existing event */
	if (ret != -EAGAIN)
		goto out;  /* got an event or other error */
	ret = rdmap_encourage(uc, NULL);
	if (ret)
		goto out; /* got error */
	ret = rdmap_check_cq(cq, uwc); /* grab cqe generated by ddp_poll */

out:
	return ret;
}

/* poll cq; if there is cqe return it to user, if not, block on fd */
int rdmap_poll_block(struct user_context *uc, cq_t *cq, int fd,
	             struct work_completion __user *uwc)
{
	int ret = 0;
	struct file *filp;
	struct iwarp_sock *iwsk = NULL;

	iwarp_debug("%s", __func__);

	ret = rdmap_check_cq(cq, uwc); /* check for pre-existing event */
	if (ret != -EAGAIN)
		goto out;  /* got an event or other error */

	filp = fget(fd);
	if (!filp) {
		ret = -EINVAL;
		goto out;
	}
	ret = ht_lookup((void *)filp, (void **)&iwsk, uc->fdhash);
	if (ret < 0) {
		ret = -ENOENT;
		goto out_fput;
	}

	for (;;) {
	    ret = rdmap_encourage(uc, iwsk);
	    if (ret)
		    break;  /* error */

	    ret = rdmap_check_cq(cq, uwc);
	    if (ret != -EAGAIN)
		    break;
	}

out_fput:
	fput(filp);
out:
	return ret;
}


/* generate terminate message
 * iwsk : iwarp socket
 * layer : error generating layer
 * etype : type of error
 * ecode : code of error
 * ddphdr : if == NULL no DDP header else there is DDP header
 * ddpsglen : ddp segment length. M bit is set iff ddpsglen > 0
 * rdmahdr : rdma read request header if != NULL
 */
static int rdmap_send_term_msg(iwsk_t *iwsk, uint8_t layer, uint8_t etype,
			       uint8_t ecode, void *ddphdr, size_t ddphdrsz,
			       ssize_t ddpsglen, void *rdmahdr)
{
	rdmap_term_hdr_t th;
	rdmap_cf_t cf = 0;

	memset(&th, 0, sizeof(th));
	rdmap_term_set_layer(th.tcf, layer);
	rdmap_term_set_etype(th.tcf, etype);
	rdmap_term_set_ecode(th.tcf, ecode);

	if (ddphdr && ddpsglen >= 0) { /* ddpsglen is valid; set M bit */
		rdmap_term_set_hdrct_m(th.tcf);
		rdmap_term_set_hdrct_d(th.tcf);
		memcpy(&(th.ddphdr), ddphdr, ddphdrsz);
	} else if (ddphdr && ddpsglen < 0) { /* unset M bit */
		rdmap_term_unset_hdrct_m(th.tcf);
		rdmap_term_set_hdrct_d(th.tcf);
		memcpy(&(th.ddphdr), ddphdr, ddphdrsz);
	}
	if (rdmahdr) {
		rdmap_term_set_hdrct_r(th.tcf); /* set R bit */
		memcpy(&(th.rdmahdr), rdmahdr, sizeof(th.rdmahdr));
	}
	rdmap_set_RV(cf);
	rdmap_set_OPCODE(cf, TERMINATE);
	return ddp_send_utm(iwsk, NULL, &th, sizeof(th), TERM_Q, cf, NULL_STAG);
}

/* send a terminate message; invalidate associated iwsk; post cqe */
int rdmap_surface_ddp_err(iwsk_t *iwsk, uint8_t layer, uint8_t etype,
			  uint8_t ecode, void *ddphdr, size_t ddphdrsz,
			  ssize_t ddpsglen, void *rdmahdr)
{
	int ret = 0;
	cqe_t cqe;
	ret = rdmap_send_term_msg(iwsk, layer, etype, ecode, ddphdr, ddphdrsz,
				  ddpsglen, rdmahdr);
	if (ret)
		goto out;
	cqe.op = OP_TERMINATE;
	cqe.status = RDMAP_TERMINATE;
	cqe.id = 0;
	cqe.msg_len = 0;
	if (iwsk->scq)
		ret = cq_produce(iwsk->scq, &cqe); /* post on scq */
	if (!ret && iwsk->scq && iwsk->scq != iwsk->rcq)
		ret = cq_produce(iwsk->rcq, &cqe); /* FIXME: also on rcq */
	iwsk->state = IWSK_TERMINATED;
out:
	return ret;
}

