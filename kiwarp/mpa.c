/*
 * marker PDU aligned framing layer
 *
 * $Id: mpa.c 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <linux/types.h>	/* uint32_t etc */
#include <linux/uio.h>		/* iovec */
#include <linux/slab.h>		/* kmalloc */
#include <linux/string.h>	/* memcpy etc */
#include <linux/errno.h> 	/* errnos */
#include <linux/net.h> 		/* for kernel_*msg */
#include <linux/socket.h>	/* MSG_NOSIGNAL */
#include <asm/uaccess.h>	/* copy_*_user */
#include "iwsk.h"
#include "mpa.h"
#include "ddp.h"
#include "crc32c.h"
#include "util.h"
#include "priv.h"

typedef struct {
	uint16_t reserved;
	uint16_t fpduptr;
} marker_t;

typedef uint32_t crc_t;
typedef uint32_t word_t;

/* all the globals will be read-only, so they are not guarded */
static const char MPA_REQ_KEY[] = "MPA ID Req Frame";
static const char MPA_REP_KEY[] = "MPA ID Rep Frame";
static const uint16_t MPA_RRF_KEY_SZ = 16;
static uint32_t MSS = 0;
static uint32_t FPDU_LEN = 0; /* MSS is same as frame size at MPA layer */
static const uint16_t MARKER_SZ = sizeof(marker_t);
static const uint16_t CRC_SZ = sizeof(crc_t);
static const uint16_t WORD_SZ = sizeof(uint32_t);
static const uint16_t MARKER_PERIOD = 512;
static const uint16_t PAYLD_CHNK = 512 - sizeof(marker_t);
static const uint32_t MAX_IPSEG = 1 << 16;
static const uint32_t POLL_TIMEOUT = 0;

static uint32_t MAX_CHUNKS = 0;
static uint32_t MAX_BLKS = 0;
static uint32_t MAX_MRKRS = 0;
static uint32_t DDP_MAX_HDR_SZ = 0;

static int mpa_encourage_one(void *val, void *arg);
static int mpa_encourage_one_block(struct user_context *uc,
                                   struct iwarp_sock *iwsk);

/* static int mpa_surface_err(iwsk_t *iwsk, mpa_err_t ecode); */

int mpa_open(void)
{
	MSS = MAX_IPSEG - 60 - 60; /* rfc-879 conserv.; see mpa-rfc Sec 3.2 */
	MSS -= 8; /* leave space for EOF etc. Spec independent. */
	FPDU_LEN = MSS;
	MAX_CHUNKS = (FPDU_LEN + MARKER_PERIOD-1) / MARKER_PERIOD;

	/* MAX_BLKS = 2*MAX_CHUNKS + 1 + pad + crc + two for header + one for
	 * safety. one blk for marker and another for payload.  */
	MAX_BLKS = (2*MAX_CHUNKS + 1) + (1 + 1) + (1 + 1) + 1;
	MAX_MRKRS = MAX_CHUNKS + 1 + 1;
	DDP_MAX_HDR_SZ = ddp_get_max_hdr_sz(); /* ddphdrblk */

	return 0;
}

void mpa_close(void)
{
	return;
}

int mpa_register_sock(iwsk_t *iwsk)
{
	memset(&(iwsk->mpask), 0, sizeof(iwsk->mpask));
	return 0;
}

void mpa_deregister_sock(iwsk_t *iwsk)
{
	return;
}

static int mpa_send_rrf(iwsk_t *iwsk, char *rrf, const char __user *pd_in,
			pd_len_t len_in, const char *key)
{
	int ret = 0, off = 0;
	uint32_t cntl = 0;
	struct kvec vec;
	struct msghdr msg;

	memcpy(rrf + off, key, MPA_RRF_KEY_SZ);
	off += MPA_RRF_KEY_SZ;
	if (iwsk->mpask.use_crc)
		mpa_set_C(cntl);
	if (iwsk->mpask.use_mrkr)
		mpa_set_M(cntl);
	mpa_unset_R(cntl);
	mpa_set_Res(cntl);
	mpa_set_Rev(cntl, 0); /* FIXME: ammasso needs it */
	mpa_set_PD_Length(cntl, len_in);
	memcpy(rrf + off, &cntl, sizeof(cntl));
	off += sizeof(cntl);
	if (copy_from_user(rrf + off, pd_in, len_in)) {
		ret = -EFAULT;
		goto out;
	}
	off += len_in;
	vec.iov_base = rrf;
	vec.iov_len = off;
	memset(&msg, 0, sizeof(msg));
	msg.msg_flags = MSG_NOSIGNAL;
	ret = kernel_sendmsg_full(iwsk->sock, &msg, &vec, 1, vec.iov_len);
out:
	return ret;
}


static int mpa_recv_rrf(iwsk_t *iwsk, char *rrf, uint32_t *cntl,
			char __user *pd_out, pd_len_t len_out,
			const char *key)
{
	int ret = 0, off = 0;
	struct kvec vec;
	struct msghdr msg;

	/* recv mpa req. read until cntl */
	memset(&msg, 0, sizeof(msg));
	vec.iov_base = rrf;
	vec.iov_len = MPA_RRF_KEY_SZ + sizeof(*cntl);
	ret = kernel_recvmsg_full(iwsk->sock, &msg, &vec, 1, vec.iov_len,
				  MSG_NOSIGNAL);
	if (ret) {
		goto out;
	}
	memcpy(cntl, rrf + MPA_RRF_KEY_SZ, sizeof(*cntl));
	if (mpa_get_PD_Length(*cntl) > len_out ||
	    memcmp(rrf, key, MPA_RRF_KEY_SZ)) {
		ret = -EINVAL; /* TODO: surface MPA_EINVRRF */
		goto out;
	}

	/* read pd in rest of reply */
	off = MPA_RRF_KEY_SZ + sizeof(*cntl);
	memset(&msg, 0, sizeof(msg));
	vec.iov_base = rrf + off;
	vec.iov_len = mpa_get_PD_Length(*cntl);
	ret = kernel_recvmsg_full(iwsk->sock, &msg, &vec, 1, vec.iov_len,
				  MSG_NOSIGNAL);
	if (ret)
		goto out;
	/* cannot use vec.* here as kernel_recvmsg modifies args in place */
	if (copy_to_user(pd_out, rrf + off, mpa_get_PD_Length(*cntl))) {
		ret = -EFAULT;
		goto out;
	}
out:
	return ret;
}

int mpa_init_startup(iwsk_t *iwsk, int is_initiator, const char __user *pd_in,
		     pd_len_t len_in, char __user *pd_out, pd_len_t len_out)
{
	int ret = 0;
	char *rrf = NULL;
	uint32_t cntl = 0;

	if (!pd_in || !pd_out) {
		ret = -EINVAL;
		goto out;
	}
	rrf = kmalloc(MPA_RRF_KEY_SZ + sizeof(cntl) + len_in, GFP_KERNEL);
	if (!rrf) {
		ret = -ENOMEM;
		goto out;
	}
	if (is_initiator) {
		ret = mpa_send_rrf(iwsk, rrf, pd_in, len_in, MPA_REQ_KEY);
		if (ret)
			goto free_rrf;
		ret = mpa_recv_rrf(iwsk, rrf, &cntl, pd_out, len_out,
				   MPA_REP_KEY);
		if (ret)
			goto free_rrf;
		/* agree with responder */
		iwsk->mpask.use_mrkr = mpa_get_M(cntl);
		iwsk->mpask.use_crc = mpa_get_C(cntl);
	} else {
		ret = mpa_recv_rrf(iwsk, rrf, &cntl, pd_out, len_out,
				   MPA_REQ_KEY);
		if (ret)
			goto free_rrf;
		if (iwsk->mpask.use_mrkr == 0) {
			iwsk->mpask.use_mrkr = mpa_get_M(cntl);
		} else {
			if (!mpa_get_M(cntl)) {
				ret = -EBADMSG; /* TODO: surface MPA_EINVRRF */
				goto free_rrf;
			}
		}
		if (iwsk->mpask.use_crc == 0) {
			iwsk->mpask.use_crc = mpa_get_C(cntl);
		} else {
			if (!mpa_get_C(cntl)) {
				ret = -EBADMSG; /* TODO: surface MPA_EINVRRF */
				goto free_rrf;
			}
		}
		ret = mpa_send_rrf(iwsk, rrf, pd_in, len_in, MPA_REP_KEY);
		if (ret)
			goto free_rrf;
	}
free_rrf:
	kfree(rrf);
out:
	return ret;
}


/* TODO: change ret to reflect the number of fds successfully processed */
int mpa_poll(struct user_context *uc, iwsk_t *iwsk)
{
	if (iwsk == NULL) {
	    /* Look at TCP receive queues to see if anything finished. */
	    return ht_iterate_callback(uc->fdhash, mpa_encourage_one, uc);
	} else {
	    /* just block on this particular one */
	    return mpa_encourage_one_block(uc, iwsk);
	}
}


static inline void mpa_fill_blk(struct kvec *blks, uint32_t *bidx,
				const void *p, uint32_t len, uint32_t *cp)
{
	/* possibly const pointer, but must wedge into non-const struct iovec */
	blks[*bidx].iov_base = (void *)(unsigned long) p;
	blks[*bidx].iov_len = len;
	(*bidx)++;
	*cp += len;
}

/*
 * Generalization of above for an array of ptr/len.
 */
static inline void mpa_fill_blkv(struct kvec *blks, uint32_t *bidx,
				 const struct kvec *pvec, int num_pvec,
				 uint32_t *cp)
{
	while (num_pvec--) {
	    blks[*bidx].iov_base = pvec->iov_base;
	    blks[*bidx].iov_len = pvec->iov_len;
	    *cp += pvec->iov_len;
	    ++pvec;
	    (*bidx)++;
	}
}


static int mpa_send_plain_fpdu(iwsk_t *iwsk,
                               void *ddp_hdr, uint32_t ddp_hdr_len,
			       const struct kvec *ddp_payldv,
			       int num_ddp_payldv, uint32_t ddp_payld_len)
{
	int ret = 0;
	uint32_t cp = 0, bi = 0, fpdu_len = 0;
	uint32_t pad_blk = 0;
	uint32_t crc_blk = 0;
	struct kvec *blks = NULL;
	struct msghdr msg;
	uint8_t pad = 0;
	ulpdu_len_t len;

	len = ddp_payld_len + ddp_hdr_len;
	pad = WORD_SZ*((len+WORD_SZ-1)/WORD_SZ) - len; /* 4-len%4 */
	fpdu_len = len + pad;
	/* ddp header, payload, optional crc */
	blks = kmalloc((1 + num_ddp_payldv + 1) * sizeof(*blks), GFP_KERNEL);
	if (!blks) {
		ret = -ENOMEM;
		goto out;
	}

	*((ulpdu_len_t *)ddp_hdr) = htons(len - 2); /* 2 bytes = mpa_hdr */
	mpa_fill_blk(blks, &bi, ddp_hdr, ddp_hdr_len, &cp);
	mpa_fill_blkv(blks, &bi, ddp_payldv, num_ddp_payldv, &cp);
	if (pad)
		mpa_fill_blk(blks, &bi, &pad_blk, pad, &cp);
	if (iwsk->mpask.use_crc) {
		fpdu_len += CRC_SZ;
		crc_blk = htonl(crc32c_vec(blks, bi));
		iwarp_debug("%s: crc %x b %u", __func__, crc_blk, bi);
		mpa_fill_blk(blks, &bi, &crc_blk, CRC_SZ, &cp);
	}
	msg.msg_flags = MSG_NOSIGNAL;
	/* NOTE: kernel_sendmsg called by kernel_sendmsg_full copies data */
	ret = kernel_sendmsg_full(iwsk->sock, &msg, blks, bi, fpdu_len);
	if (ret) /* update send sp only on success */
		iwsk_add_mpask_send_sp(iwsk, cp);
	kfree(blks);
out:
	return ret;
}

int mpa_send(iwsk_t *iwsk, void *ddp_hdr, const uint32_t ddp_hdr_len,
		    const struct kvec *ddp_payldv, int num_ddp_payldv,
		    uint32_t ddp_payld_len)
{
	if (iwsk->mpask.use_mrkr)
		return -ENOSYS;
	else
		return mpa_send_plain_fpdu(iwsk, ddp_hdr, ddp_hdr_len,
					   ddp_payldv, num_ddp_payldv,
					   ddp_payld_len);
}

static int mpa_recv_plain_fpdu(iwsk_t *iwsk, struct user_context *uc)
{
	int ret = 0;
	uint32_t hdrsz, cp, hp, bidx;
	uint8_t pad;
	uint8_t *ddphdr_blk;
	uint32_t crc_blk = 0;
	uint32_t pad_blk = 0;
	uint32_t len;
	struct kvec *blks, *blks_cp;
	struct kvec hdr;
	struct msghdr msg;
	int last_kmap_bidx;

	ddphdr_blk = kmalloc(DDP_MAX_HDR_SZ, GFP_KERNEL);
	if (!ddphdr_blk) {
		ret = -ENOMEM;
		goto out;
	}
	/* debugging */
	/* memset(ddphdr_blk, 0xee, DDP_MAX_HDR_SZ); */

	blks = kmalloc(MAX_BLKS*sizeof(*blks), GFP_KERNEL);
	if (!blks) {
		ret = -ENOMEM;
		goto free_ddphdr;
	}

	blks_cp = kmalloc(MAX_BLKS*sizeof(*blks_cp), GFP_KERNEL);
	if (!blks_cp) {
		ret = -ENOMEM;
		goto free_blks;
	}

	/*
	 * Read start of DDP header to determine what type of DDP
	 * header to read completely.
	 */
	hdr.iov_base = ddphdr_blk;
	hdr.iov_len = sizeof(ddp_hdr_start_t);
	ret = kernel_recvmsg_full(iwsk->sock, &msg, &hdr, 1, hdr.iov_len,
			          MSG_NOSIGNAL);
	if (ret < 0)
		goto free_blks_cp;

	/*
	 * Now read the rest of the header.
	 */
	hdrsz = ddp_get_hdr_sz(ddphdr_blk);
	hdr.iov_base = ddphdr_blk + sizeof(ddp_hdr_start_t);
	hdr.iov_len = hdrsz - sizeof(ddp_hdr_start_t);
	ret = kernel_recvmsg_full(iwsk->sock, &msg, &hdr, 1, hdr.iov_len,
			          MSG_NOSIGNAL);
	if (ret < 0)
		goto free_blks_cp;

	/*
	 * Put header into blks[]---keep all around for future possible CRC.
	 */
	bidx = 0;
	cp = 0;
	mpa_fill_blk(blks, &bidx, ddphdr_blk, hdrsz, &cp);
	hp = cp;  /* hp: position of last byte of header in fpdu */

	/*
	 * Figure out where this packet goes and set the blks[] addresses
	 * using kmap().
	 */
	ret = ddp_get_sink(uc, iwsk, ddphdr_blk, blks, MAX_BLKS, &bidx, &cp);
	if (ret < 0) {
		iwarp_info("%s: ddp_get_sink returns %d", __func__, ret);
		goto free_blks_cp;
	}

	last_kmap_bidx = bidx;

	len = blks[bidx-1].iov_len;
	pad = WORD_SZ*((len+WORD_SZ-1)/WORD_SZ) - len; /* 4 - len%4 */
	iwarp_debug("%s: len %u pad %u", __func__, len, pad);

	if (pad)
		mpa_fill_blk(blks, &bidx, &pad_blk, pad, &cp);
	if (iwsk->mpask.use_crc)
		mpa_fill_blk(blks, &bidx, &crc_blk, CRC_SZ, &cp);

	/* NOTE: kernel_recvmsg alters the kvec in-place therefore we need to
	 * make a copy of vector */
	memcpy(blks_cp, blks, sizeof(*blks)*bidx);
	{ /* debug */
	int i;
	iwarp_debug("%s: dump bidx %d cp-hp %d", __func__, bidx, cp-hp);
	for (i=1; i<bidx; i++)
		iwarp_debug("%s: bidx %d addr %p len %zu", __func__, i,
	                    blks_cp[i].iov_base, blks_cp[i].iov_len);
	}
	/* skip already-read header part */
	ret = kernel_recvmsg_full(iwsk->sock, &msg, &blks_cp[1],
				  bidx - 1, cp - hp, MSG_NOSIGNAL);
	if (ret < 0)
		goto free_blks_cp;

	/* we are updating the counter as soon as we recv something from the
	 * pipe. This is to keep track of how many bytes have been seen at
	 * this end of pipe, irrespective of integrity of message */
	iwsk_add_mpask_recv_sp(iwsk, cp);

	if (iwsk->mpask.use_crc) {
		uint32_t crc;
		crc = crc32c_vec(blks, bidx - 1);
		crc_blk = ntohl(crc_blk);
		iwarp_debug("%s: crc %x crc_blk %x bidx %u", __func__, crc,
		            crc_blk, bidx - 1);
		if (crc != crc_blk) {
			iwarp_info("crc check failed. exp %x got %x",
				   crc, crc_blk);
			ret = -EBADMSG; /* MPA_EINVCRC */
			goto free_blks_cp;
		}
	}
	ret = ddp_process_ulpdu(uc, iwsk, ddphdr_blk);

free_blks_cp:
	kfree(blks_cp);
free_blks:
	kfree(blks);
free_ddphdr:
	kfree(ddphdr_blk);
out:
	return ret;
}

static int mpa_encourage_one(void *val, void *arg)
{
	struct iwarp_sock *iwsk = val;
	struct user_context *uc = arg;
	unsigned int mask;
	int ret = 0;

	/* calls tcp_poll, e.g. */
	mask = iwsk->sock->ops->poll(iwsk->filp, iwsk->sock, NULL);

	if (mask & POLLIN) {
		if (iwsk->mpask.use_mrkr)
			ret = -ENOSYS;
		else
			ret = mpa_recv_plain_fpdu(iwsk, uc);
	} else if (mask & POLLNVAL) {
		ret = -EINVAL;
	} else if (mask & POLLERR) {
		ret = -ENOSYS;
	} else if (mask & POLLHUP) { /* socket is hung up */
		ret = -ECONNRESET; /* MPA_ECONNRESET */
	}
	return ret;
}

static int mpa_encourage_one_block(struct user_context *uc,
                                   struct iwarp_sock *iwsk)
{
	struct poll_wqueues table;
	unsigned int mask;
	int ret = 0;

	iwarp_debug("%s", __func__);

	poll_initwait(&table);
	set_current_state(TASK_INTERRUPTIBLE);

	/* calls tcp_poll, e.g. */
	mask = iwsk->sock->ops->poll(iwsk->filp, iwsk->sock, &table.pt);

	/*
	if (signal_pending(current))
		break;
	*/
	set_current_state(TASK_RUNNING);

	iwarp_debug("%s: mask %x", __func__, mask);

	if (mask & POLLIN) {
		if (iwsk->mpask.use_mrkr)
			ret = -ENOSYS;
		else
			ret = mpa_recv_plain_fpdu(iwsk, uc);
	} else if (mask & POLLNVAL) {
		ret = -EINVAL;
	} else if (mask & POLLERR) {
		ret = -ENOSYS;
	} else if (mask & POLLHUP) { /* socket is hung up */
		ret = -ECONNRESET; /* MPA_ECONNRESET */
	}

	poll_freewait(&table);
	return ret;
}

#if 0  /* unused */
static int mpa_surface_err(iwsk_t *iwsk, mpa_err_t ecode)
{
	return ddp_surface_llp_err(iwsk, IWSK_MPA, DDP_ELLP, ecode);
}
#endif

