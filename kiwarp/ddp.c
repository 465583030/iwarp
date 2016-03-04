/*
 * direct data placement layer
 *
 * $Id: ddp.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/byteorder.h>
#include "priv.h"
#include "mpa.h"
#include "ddp.h"
#include "rdmap.h"
#include "mem.h"
#include "util.h"

typedef struct {
	struct list_head list;
	msg_len_t len;
	stag_t stag;
} ddp_tag_msg_t;

typedef struct {
	struct list_head list;
	msg_len_t len;
	msn_t msn;
	qnum_t qn;
} ddp_untag_msg_t;

static const ulpdu_len_t UNTAGGED_HDR_SZ = sizeof(ddp_untag_hdr_t);
static const ulpdu_len_t TAGGED_HDR_SZ = sizeof(ddp_tag_hdr_t);
static const ulpdu_len_t max_hdr_sz =
sizeof(ddp_untag_hdr_t) > sizeof(ddp_tag_hdr_t)
	? sizeof(ddp_untag_hdr_t) : sizeof(ddp_tag_hdr_t);

int ddp_open(void)
{
	return mpa_open();
}

void ddp_close(void)
{
	mpa_close();
}

int ddp_register_sock(iwsk_t *iwsk)
{
	int ret;

	iwsk->ddpsk.recv_msn = 1;
	iwsk->ddpsk.send_msn = 1;
	iwsk->ddpsk.ddp_sgmnt_len = DDP_SEGLEN;
	INIT_LIST_HEAD(&(iwsk->ddpsk.outst_tag));
	INIT_LIST_HEAD(&(iwsk->ddpsk.outst_untag));

	ret = mpa_register_sock(iwsk);

	return ret;
}

void ddp_deregister_sock(iwsk_t *iwsk)
{
	ddp_untag_msg_t *utm, *utmnext;
	ddp_tag_msg_t *tm, *tmnext;

	mpa_deregister_sock(iwsk);

	list_for_each_entry_safe(utm, utmnext, &(iwsk->ddpsk.outst_untag),
				 list) {
		kfree(utm);
	}
	list_for_each_entry_safe(tm, tmnext, &(iwsk->ddpsk.outst_tag), list) {
		kfree(tm);
	}
}

inline uint32_t ddp_get_max_hdr_sz(void)
{
	return max_hdr_sz;
}

inline uint32_t ddp_get_hdr_sz(void *b)
{
	ddp_hdr_start_t *s = (ddp_hdr_start_t *)b;
	if (ddp_is_TAGGED(s->cf))
		return TAGGED_HDR_SZ;
	else
		return UNTAGGED_HDR_SZ;
}

int ddp_send_utm(iwsk_t *iwsk, stag_desc_t *sd, const void __user *msg,
                 uint32_t msg_len, qnum_t qn, uint8_t ulp_ctrl,
		 uint32_t ulp_payld)
{
	int ret = 0;
	int i, mo, num_sgmnts;
	ulpdu_len_t ddp_payld_len;
	ddp_untag_hdr_t uth;

	memset(&uth, 0, sizeof(uth));
	uth.cf = DDP_CF_DV;
	uth.ulp_ctrl = ulp_ctrl;
	uth.ulp_payld = htonl(ulp_payld);
	uth.qn = htonl(qn);
	uth.msn = htonl(iwsk_get_ddpsk_send_msn(iwsk));

	/* payload len */
	ddp_payld_len = iwsk->ddpsk.ddp_sgmnt_len - UNTAGGED_HDR_SZ;

	num_sgmnts = (msg_len + ddp_payld_len-1) / ddp_payld_len;
	if (unlikely(num_sgmnts == 0))
	    num_sgmnts = 1;

	/*
	 * Walk the entries in sd, doing kmap_atomic() on each.  Pass
	 * to mpa_send.  kunmap.  Can accept kernel data too:  if sd is NULL
	 * then msg is already in kernel.
	 */
	mo = 0;
	for (i = 0; i < num_sgmnts; i++) {

		struct kvec iov[5];
		int numiov;

		uth.mo = htonl(mo);

		if (i == num_sgmnts - 1) {
			ddp_set_LAST(uth.cf);
			ddp_payld_len = msg_len - mo;
		}

		numiov = 0;
		if (sd == NULL) {
			/* kernel message, not a __user pointer really */
			iov[numiov].iov_base = (char *) msg + mo;
			iov[numiov].iov_len = ddp_payld_len;
			++numiov;
		} else {
			/* from user space */
			ret = mem_fill_iovec(msg, ddp_payld_len, mo, sd, iov,
					     sizeof(iov)/sizeof(iov[0]),
					     &numiov);
			if (ret < 0) {
				iwarp_info("%s: mem_fill_iovec error %d",
					   __func__, ret);
				return ret;
			}
		}

		ret = mpa_send(iwsk, &uth, sizeof(uth), iov, numiov,
		               ddp_payld_len);

		/* XXX: this should be done when we know the bytes have gone
		 * out and come back acknowledged */
		if (sd)
			mem_unmap_iovec(msg, ddp_payld_len, mo, sd);

		if (ret < 0)
			return ret;

		mo += ddp_payld_len;
	}

	iwsk_inc_ddpsk_send_msn(iwsk);
	return ret;
}

/* send tagged message, never from kernel data. */
int ddp_send_tm(iwsk_t *iwsk, stag_desc_t *sd, const void __user *msg,
                uint32_t msg_len, uint8_t rsvdulp, stag_t sink_stag,
		tag_offset_t sink_to)
{
	int ret = 0;
	int i, num_sgmnts;
	msg_len_t mo;
	ulpdu_len_t ddp_payld_len;
	ddp_tag_hdr_t th;
	unsigned long buf = (unsigned long) msg;
	unsigned long ms = (unsigned long) sd->mr->start >> PAGE_SHIFT;

	memset(&th, 0, sizeof(th));
	th.cf = DDP_CF_TAGGED | DDP_CF_DV;
	th.rsvdulp = rsvdulp;
	th.stag = htonl(sink_stag);

	ddp_payld_len = iwsk->ddpsk.ddp_sgmnt_len - TAGGED_HDR_SZ;

	num_sgmnts = (msg_len + ddp_payld_len-1) / ddp_payld_len;
	if (unlikely(num_sgmnts == 0))
	    num_sgmnts = 1;

	/* premap pages */
	for (i=0; i<sd->mr->npages; i++)
		sd->mr->caddr[i] = kmap(sd->mr->page_list[i]);

	mo = 0;
	for (i=0; i<num_sgmnts; i++) {

		struct kvec iov[5];
		int numiov;
		int left;

		numiov = 0;
		th.to = swab64(sink_to + mo);

		if (i == num_sgmnts - 1) {
			ddp_set_LAST(th.cf);
			ddp_payld_len = msg_len - mo;
		}

		/*
		 * Fill iov based on send buffer page boundaries.
		 */
		left = ddp_payld_len;
		while (left > 0) {
			int page_index = ((buf + mo) >> PAGE_SHIFT) - ms;
			int page_offset = (buf + mo) & ~PAGE_MASK;
			int page_avail = PAGE_SIZE - page_offset;
			int numbytes = left;
			if (numbytes > page_avail)
				numbytes = page_avail;

			if (numiov == sizeof(iov)/sizeof(iov[0])) {
				iwarp_info("%s: iov overflow", __func__);
				return -EOVERFLOW;
			}
			iov[numiov].iov_base = sd->mr->caddr[page_index]
			                       + page_offset;
			iov[numiov].iov_len = numbytes;
			++numiov;
			left -= numbytes;
			mo += numbytes;
		}

		ret = mpa_send(iwsk, &th, sizeof(th), iov, numiov,
		               ddp_payld_len);
		if (ret < 0)
			return ret;
	}

	/* unmap pages; XXX: do this after send completion */
	for (i=0; i<sd->mr->npages; i++)
		kunmap(sd->mr->page_list[i]);

	return ret;
}

#define ntohq(x) __be64_to_cpu(x)

#if 0
static void hexdump(const void *pv, int len)
{
	int i, bol = 1;
	const unsigned char *p = pv;

	while (len) {
		if (bol) {
			printk(KERN_INFO "%s: %p: ", __func__, p);
			bol = 0;
		}
		for (i=0; i<8; i++) {
			if (len == 0) break;
			if (len == 1) {
				printk(" %02x", *p);
				--len;
				++p;
			} else {
				printk(" %02x%02x", *p, *(p+1));
				len -= 2;
				p += 2;
			}
		}
		if (i == 8) {
			printk("\n");
			bol = 1;
		}
	}
	if (!bol)
		printk("\n");
}
#endif

/*
 * Return 0 if successful or an error.  Fills blks[] and adjusts bidx
 * and cp.
 */
int ddp_get_sink(struct user_context *uc, iwsk_t *iwsk, void *hdr,
                 struct kvec *blks, int num_blks_alloc, uint32_t *bidx,
		 uint32_t *cp)
{
	rdmap_cf_t cf = ((ddp_hdr_start_t *)hdr)->cf;
	int payload_len;
	recv_buf_t *rbuf;
	uint32_t offset;

	if (ddp_is_TAGGED(cf)) {
		ddp_tag_hdr_t *h = hdr;
		tag_offset_t to;
		stag_t stag;

		/* first 2 bytes makeup  mpa hdr */
		payload_len = ntohs(h->llp_hdr) - (sizeof(*h) - 2);
		to = ntohq(h->to);
		stag = ntohl(h->stag);

		/*
		 * Space validation happens within.  Also calculates the
		 * linear offset in userspace relative to beginning of
		 * tagged buffers.
		 */
		rbuf = rdmap_get_tag_sink(uc, stag, to, payload_len, h->rsvdulp,
		                          &offset);
		if (!rbuf) {
			iwarp_info("%s: rdmap_get_tag_sink no buf", __func__);
			return -ENOBUFS;
		}

		iwarp_debug("%s: to %Lx stag %d len %d", __func__, to, stag,
		            payload_len);
	} else {
		ddp_untag_hdr_t *h = hdr;
		qnum_t qn;
		msn_t msn;

		qn = ntohl(h->qn);
		msn = ntohl(h->msn);
		offset = ntohl(h->mo);

		/*
		 * h->llp_hdr == ddp_payld_len + ddphdr_len. It does not
		 * include markers, pad and crc len. first 2 bytes makeup
		 * mpa hdr.
		 */
		payload_len = ntohs(h->llp_hdr) - (sizeof(*h) - 2);

		rbuf = rdmap_get_untag_sink(iwsk, qn, msn);
		if (!rbuf) {
			iwarp_info("%s: rdmap_get_untag_sink no buf", __func__);
			return -ENOBUFS;
		}
		iwarp_debug("%s: got rbuf %p ubuf %p sd %p", __func__,
		            rbuf, rbuf->ubuf, rbuf->sd);

		/*
		 * Verify enough space to put incoming message.
		 */
		if ((offset + payload_len) > rbuf->len) {
			iwarp_info("%s: untag mo %u + len %u > rbuf->len %zu",
				   __func__, offset, payload_len, rbuf->len);
			return -EBADMSG;
		}

		iwarp_debug("%s: msn %x mo %d len %d", __func__, msn, offset,
			    payload_len);
	}

	/*
	 * Do the common blk filling for both tagged and untagged paths.
	 */
	if (rbuf->ubuf) {
		/* append buffer pointers like tag case using rbuf->sd
		 * to map kernel pages*/
		int ret = mem_fill_iovec(rbuf->ubuf, payload_len,
		                         offset, rbuf->sd, blks,
					 num_blks_alloc, bidx);
		if (ret < 0) {
			iwarp_info("%s: mem_fill_iovec err %d", __func__, ret);
			return ret;
		}
		*cp += payload_len;
	} else {
		/* kernel buffer, ignore sd, straight to contig buf */
		blks[*bidx].iov_base = rbuf->kbuf + offset;
		blks[*bidx].iov_len = payload_len;
		++*bidx;
		*cp += payload_len;
	}

	/*
	 * rbuf is left hanging on whatever queue that created it.
	 */

	return 0;
}

/*
 * Message has already been placed.  Do just possible delivery bookkeeping here.
 * XXX: kunmap() pages and free recv_buf.
 */
static int ddp_process_untag_ulpdu(struct user_context *uc, iwsk_t *iwsk,
				   void *hdr)
{
	int len = 0;
	int found = 0;
	int ret = 0;
	ddp_untag_hdr_t *uth = (ddp_untag_hdr_t *)hdr;
	ddp_untag_msg_t *utm = NULL;

	uth->ulp_payld = ntohl(uth->ulp_payld);
	uth->qn = ntohl(uth->qn);
	uth->msn = ntohl(uth->msn);
	uth->mo = ntohl(uth->mo);
	uth->llp_hdr = ntohs(uth->llp_hdr);
	len = uth->llp_hdr - (UNTAGGED_HDR_SZ - 2);

	list_for_each_entry(utm, &(iwsk->ddpsk.outst_untag), list) {
		if (utm->qn == uth->qn && utm->msn == uth->msn) {
			utm->len += len;
			found = 1;
			break;
		}
	}
	if (!found) {
		if (ddp_is_LAST(uth->cf)) {
			utm = NULL;
		} else {
			/* build a place holder for the msg. freed in either
			 * ddp_untag_recv or finally in ddp_deregister_sock */
			utm = kmalloc(sizeof(*utm), GFP_KERNEL);
			if (!utm) {
				ret = -ENOMEM;
				goto out;
			}
			utm->qn = uth->qn;
			utm->msn = uth->msn;
			utm->len = len;
			list_add_tail(&(utm->list),
				      &(iwsk->ddpsk.outst_untag));
		}
	}
	if (ddp_is_LAST(uth->cf)) {
		if (utm)
			len = utm->len;
		ret = rdmap_untag_recv(uc, iwsk, uth->ulp_ctrl, uth->ulp_payld,
				       uth->qn, uth->msn, len);
		if (ret)
			goto out;
		iwsk_inc_ddpsk_recv_msn(iwsk);
		if (utm) {
			list_del(&(utm->list));
			kfree(utm);
		}
	}
out:
	return ret;

}

static int ddp_process_tag_ulpdu(struct user_context *uc, iwsk_t *iwsk,
				 void *hdr)
{
	int len = 0;
	int found = 0;
	int ret = 0;
	ddp_tag_hdr_t *th = (ddp_tag_hdr_t *)hdr;
	ddp_tag_msg_t *tm = NULL;

	th->stag = ntohl(th->stag);
	th->llp_hdr = ntohs(th->llp_hdr);
	th->to = swab64(th->to);
	len = th->llp_hdr - (TAGGED_HDR_SZ - 2);

	list_for_each_entry(tm, &iwsk->ddpsk.outst_tag, list) {
		if (tm->stag == th->stag) {
			tm->len += len;
			found = 1;
			break;
		}
	}

	if (!found) {
		if (ddp_is_LAST(th->cf))
			tm = NULL; /* we will complete it immediately */
		else {
			/* build a new entry for later. it is freed either in
			 * ddp_tag_recv or finally ddp_deregister_sock  */
			tm = kmalloc(sizeof(*tm), GFP_KERNEL);
			if (!tm) {
				ret = -ENOMEM;
				goto out;
			}
			tm->stag = th->stag;
			tm->len = len;
			list_add_tail(&tm->list, &iwsk->ddpsk.outst_tag);
		}
	}

	if (ddp_is_LAST(th->cf)) {
		/* use total saved length, or len of just this one
		 * frame that is the first and last of the message */
		if (tm)
			len = tm->len;
		ret = rdmap_tag_recv(iwsk, th->rsvdulp, th->stag, len);
		if (ret)
			goto out;
		if (tm) {
			list_del(&tm->list);
			kfree(tm);
		}
	}
out:
	return ret;
}

int ddp_process_ulpdu(struct user_context *uc, iwsk_t *iwsk, void *hdr)
{
	ddp_hdr_start_t *s = (ddp_hdr_start_t *)hdr;

	if (ddp_is_TAGGED(s->cf)) {
		return ddp_process_tag_ulpdu(uc, iwsk, hdr);
	} else {
		return ddp_process_untag_ulpdu(uc, iwsk, hdr);
	}
}


inline int ddp_surface_llp_err(iwsk_t *iwsk, iwsk_layer_t layer, uint8_t etype,
			       uint8_t ecode)
{
	return rdmap_surface_ddp_err(iwsk, layer, etype, ecode, NULL, 0,
				     -1, NULL);
}

static inline int ddp_surface_err(iwsk_t *iwsk, iwsk_layer_t layer,
				  uint8_t etype, uint8_t ecode, void *ddphdr,
				  size_t ddphdrsz, ssize_t ddpsglen)
{
	return rdmap_surface_ddp_err(iwsk, layer, etype, ecode, ddphdr,
				     ddphdrsz, ddpsglen, NULL);
}

