/*
 * MPA impl.
 *
 * $Id: mpa.c 669 2007-08-03 19:00:42Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/socket.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <netinet/tcp.h>

/*
 * IP_MTU is defined in linux/in.h, but linux/in.h conflicts with
 * netinit/in.h so we do a brute force redefinition of IP_MTU.
 */
#ifdef linux
#	ifndef IP_MTU
#		define IP_MTU 14
#	endif
#endif

#include "mpa.h"
#include "ddp.h"
#include "common.h"
#include "util.h"
#include "ht.h"
#include "crc32c.h"

/* rename struct pollfd */
typedef struct pollfd pollfd_t;

/* struct maintaining poll sockets */
typedef struct poll_sk {
	pollfd_t *sks;
	size_t numsks;
} poll_sk_t;

typedef struct marker {
	uint16_t reserved;
	uint16_t fpduptr;
} marker_t;

typedef uint32_t crc_t;
typedef uint32_t word_t;

static uint32_t mpa_recv_cntr = 0;
static uint32_t mpa_send_cntr = 0;

static const char MPA_REQ_KEY[] = "MPA ID Req Frame";
static const char MPA_REP_KEY[] = "MPA ID Rep Frame";
static uint32_t MSS = 0;
static uint32_t FPDU_LEN = 0; /* MSS is same as frame size at MPA layer */
static const uint16_t MARKER_SZ = sizeof(marker_t);
static const uint16_t CRC_SZ = sizeof(crc_t);
static const uint16_t WORD_SZ = sizeof(uint32_t);
static const uint16_t MARKER_PERIOD = 512;
static const uint16_t PAYLD_CHNK = 512 - sizeof(marker_t);
static const uint32_t MAX_IPSEG = 1 << 16;
static const uint32_t POLL_TIMEOUT = 0;

static struct iovec *blks = NULL;
static marker_t *mrkr_blk = NULL;
static void *ddphdr_blk = NULL;
static crc_t crc_blk;
static word_t pad_blk;
static poll_sk_t pollsks;
static uint32_t MAX_CHUNKS = 0;
static uint32_t MAX_BLKS = 0;
static uint32_t MAX_MRKRS = 0;
static uint32_t DDP_MAX_HDR_SZ = 0;

static inline int mpa_get_mtu(socket_t sock, void *mtu);
static int mpa_wrt_mrkr_fpdu(mpa_sk_t *mpask, void *ddp_hdr,
                             uint32_t ddp_hdr_len, const void *ddp_payld,
                             ulpdu_len_t ddp_payld_len);
static int mpa_wrt_plain_fpdu(mpa_sk_t *mpask, void *ddp_hdr,
                              uint32_t ddp_hdr_len, const void *ddp_payld,
			      ulpdu_len_t ddp_payld_len);
static inline void mpa_read_mrkr(iwsk_t *s, uint32_t *bidx, uint32_t *midx,
                                 uint32_t *cp, uint32_t *mp);
static inline void mpa_fill_blk(struct iovec *blks, uint32_t *bidx,
                                const void *p, uint32_t len, uint32_t *cp);
static int mpa_rd_mrkr_fpdu(iwsk_t *iwsk, uint32_t *bidx, uint32_t *midx);
static int mpa_rd_plain_fpdu(iwsk_t *iwsk, uint32_t *bidx);

/*
 * rfc-879: relationship between MTU, MSS, IPv4 & TCP headers
 */
void
mpa_init(void)
{
	MSS = MAX_IPSEG - 60 - 60; /* rfc-879 conserv.; also mpa-rfc Sec 3.2 */
	MSS -= 8; /* leave space for EOF etc. Spec independent. */
	FPDU_LEN = MSS;
	MAX_CHUNKS = (FPDU_LEN + MARKER_PERIOD-1) / MARKER_PERIOD;

	/*
	 * MAX_BLKS = 2*MAX_CHUNKS + 1 + pad + crc + two for header + one for
	 * safety. one blk for marker and another for payload.
	 */
	MAX_BLKS = (2*MAX_CHUNKS + 1) + (1 + 1) + (1 + 1) + 1;
	MAX_MRKRS = MAX_CHUNKS + 1 + 1;

	blks = Malloc(MAX_BLKS * sizeof(*blks));
	memset(blks, 0, MAX_BLKS * sizeof(*blks));

	/* buffer to store markers;  worst case size */
	mrkr_blk = Malloc(MARKER_SZ*MAX_MRKRS);
	memset(mrkr_blk, 0, MARKER_SZ*MAX_MRKRS);

	/* struct to store all poll sockets */
	pollsks.numsks = 0;
	pollsks.sks = Malloc(MAX_SOCKETS * sizeof(*pollsks.sks));
	memset(pollsks.sks, 0, MAX_SOCKETS * sizeof(*pollsks.sks));

	/* ddphdrblk */
	DDP_MAX_HDR_SZ = ddp_get_max_hdr_sz();
	ddphdr_blk = Malloc(DDP_MAX_HDR_SZ);
	memset(ddphdr_blk, 0, DDP_MAX_HDR_SZ);
}

inline void
mpa_fin(void)
{
	free(ddphdr_blk);
	free(pollsks.sks);
	free(blks);
	free(mrkr_blk);
}

inline int
mpa_register_sock(iwsk_t *s)
{
	int ret = -1;
	int one;

	s->mpask.use_crc = FALSE;
	s->mpask.use_mrkr = FALSE;
	s->mpask.recv_mp = 0; /* mpa-rfc Sec. 5.1, also Sec. 6.1 pg. 30 pnt. 7 */
	s->mpask.send_mp = 0; /* mpa-rfc Sec. 5.1 */
	s->mpask.send_sp = 0; /* mpa-rfc Sec. 5.1, also Sec. 6.1 pg 30 pnt. 7 */
	s->mpask.recv_sp = 0; /* mpa-rfc Sec. 5.1 */
	ret = mpa_get_mtu(s->sk, &(s->mpask.mss));
	if (ret < 0)
		return ret;
	s->mpask.mss = s->mpask.mss - 60 - 60 - 8; /* see mpa_init */

	/* add to pollsks */
	s->mpask.skidx = pollsks.numsks;
	pollsks.sks[pollsks.numsks].fd = s->sk;
	pollsks.sks[pollsks.numsks].events = POLLIN;
	pollsks.numsks++;

	/*
	 * Disable Nagle algorithm.
	 */
	one = 1;
	ret = setsockopt(s->sk, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
	if (ret < 0)
	    error_errno("%s: setsockopt nagle off", __func__);

	return 0;
}

inline void
mpa_deregister_sock(iwsk_t *s)
{
	if (s->mpask.skidx < (pollsks.numsks - 1)) /* not last, overwrite tail */
		memmove(&pollsks.sks[s->mpask.skidx],
			&pollsks.sks[s->mpask.skidx + 1],
			sizeof(struct pollfd)
			* (pollsks.numsks - 1 - s->mpask.skidx));
	else /* last socket, erase it */
		memset(&pollsks.sks[s->mpask.skidx], 0, sizeof(pollfd_t));
	pollsks.numsks--;
}

/*
 * when data is sent over the wire, tcp takes care of reading the bits in
 * network order. dont need to htonl rrf->cntl before writing and after
 * reading from the network.
 *
 * FIXME: We are currently deciding whether to use marker or CRC based on
 * initiator's criteria or responder's reply, mainly to work with ammasso
 * card. This is not according to rfc and has to be changed. One way to do it
 * would be to have a config file, which takes into account each card type's
 * requirements, and pass it on to program as one argument.
 */
int
mpa_init_startup(iwsk_t *iwsk, bool_t is_initiator, const char *pd_in,
                 char *pd_out, pd_len_t rpd_len)
{
	int ret;
	int len;
	pd_len_t pd_in_len;
	struct {
		char key[16];
		uint32_t cntl;
		char init_string[0];
	} *rrf;

	if(!strlen(pd_in))
		pd_in_len = 0;
	else
		pd_in_len = strlen(pd_in) + 1;

	pd_len_t pd_out_len = 0;

	if (!pd_out || !pd_in)
		return -EINVAL;

	len = pd_in_len;
	len += sizeof(*rrf);
	rrf = Malloc(len);

	if (is_initiator) {
		memset(rrf, 0, len);
		memcpy(rrf->key, &MPA_REQ_KEY, sizeof(rrf->key));
		if (iwsk->mpask.use_mrkr)
		    mpa_set_M(rrf->cntl); /* we prefer markers */
		if (iwsk->mpask.use_crc)
		    mpa_set_C(rrf->cntl); /* we prefer CRC */
		mpa_unset_R(rrf->cntl);
		mpa_set_Res(rrf->cntl);
		mpa_set_Rev(rrf->cntl, 0); /* ammasso specific revision */
		mpa_set_PD_Length(rrf->cntl, pd_in_len);
		strcpy(rrf->init_string, pd_in);

		ret = write_full(iwsk->sk, rrf, len);
		if (ret < 0)
			return ret;

		read_full(iwsk->sk, rrf, sizeof(*rrf));
		pd_out_len = mpa_get_PD_Length(rrf->cntl);

		/* XXX: We dont test if pd_out_len > pd_in_len and reallocate rrf
		 * since we read into pd_out instead of copying from
		 * rrf->init_string
		 */
		if (pd_out_len > rpd_len)
			return -EMSGSIZE;
		read_full(iwsk->sk, pd_out, pd_out_len);

		/* FIXME: change this to confirm to spec */
		if (mpa_get_M(rrf->cntl)) {
			iwsk->mpask.use_mrkr = TRUE;
		} else {
			iwsk->mpask.use_mrkr = FALSE;
		}

		if (mpa_get_C(rrf->cntl)) {
			iwsk->mpask.use_crc = TRUE;
		} else {
			iwsk->mpask.use_crc = FALSE;
		}

		/* TODO: Surface these errors */
		if (memcmp(&(rrf->key), &(MPA_REP_KEY), sizeof(rrf->key)))
			return -EBADMSG;
	} else {

		read_full(iwsk->sk, rrf, sizeof(*rrf));

		/* FIXME: change this to confirm to spec */
		if (iwsk->mpask.use_mrkr == 0) {
		    if (mpa_get_M(rrf->cntl)) {
			    iwsk->mpask.use_mrkr = TRUE;
		    } else {
			    iwsk->mpask.use_mrkr = FALSE;
		    }
		} else {
		    if (!mpa_get_M(rrf->cntl))
				return -EBADMSG;
		}

		if (iwsk->mpask.use_crc == 0) {
		    if (mpa_get_C(rrf->cntl)) {
			    iwsk->mpask.use_crc = TRUE;
		    } else {
			    iwsk->mpask.use_crc = FALSE;
		    }
		} else {
		    if (!mpa_get_C(rrf->cntl))
				return -EBADMSG;
		}

		pd_out_len = mpa_get_PD_Length(rrf->cntl);

		/* XXX: We dont test if pd_out_len > pd_in_len and reallocate rrf
		 * since we read into pd_out instead of copying from
		 * rrf->init_string
		 */
		read_full(iwsk->sk, pd_out, pd_out_len);

		/* TODO: Surface these errors */
		if (memcmp(&(rrf->key), &(MPA_REQ_KEY), sizeof(rrf->key)))
			return -EBADMSG;

		memset(rrf, 0, len);
		memcpy(&(rrf->key), &MPA_REP_KEY, sizeof(rrf->key));
		if (iwsk->mpask.use_mrkr) {
			mpa_set_M(rrf->cntl);
		} else {
			mpa_unset_M(rrf->cntl);
		}
		if (iwsk->mpask.use_crc) {
			mpa_set_C(rrf->cntl);
		} else {
			mpa_unset_C(rrf->cntl);
		}
		mpa_unset_R(rrf->cntl);
		mpa_set_Res(rrf->cntl);
		mpa_set_Rev(rrf->cntl, 1); /* neteffect specific revision */
		mpa_set_PD_Length(rrf->cntl, pd_in_len);
		if(pd_in_len != 0)
			strcpy(rrf->init_string, pd_in);
		ret = write_full(iwsk->sk, rrf, len);
		if (ret < 0)
			return ret;
	}
	free(rrf);



	return 0;
}

/*
 * When computing MULPDU llp_hdr size also accounted.
 * 6 = llp_hdr size + crc32c size
 * 4*ceil((double)FPDU_LEN/(double)MARKER_PERIOD)) = worst case markers
 * MSS % 4 = align to word boundary.
 */
int
mpa_get_mulpdu(iwsk_t *iwsk)
{
	int max_pdu;
	int val;
	socklen_t size = sizeof(int);

	/*get MSS*/
	getsockopt(iwsk->sk, SOL_TCP, TCP_MAXSEG, &val, &size);

	/*
	for neteffect max_pdu = MSS - 4
	NetEffect HW does not advertise an MSS so
	TCP in linux assumes default of 536 for MSS
	*/
	max_pdu = val - 4;

	/*
	for ammasso max_pdu = MSS -8 --- WHY?
	*/

	return max_pdu;
}

static inline int
mpa_get_mtu(socket_t sock, void *mtu)
{
	socklen_t optlen = sizeof(uint32_t);
	int ret = -1;

	ret = getsockopt(sock, SOL_IP, IP_MTU, mtu, &optlen);
	if (ret < 0)
		return ret;
	else
		return 0;
/*	return 1500;*/
}

int
mpa_set_sock_attrs(iwsk_t *iwsk)
{
	iwsk = NULL;
	return 0;
}

int
mpa_send(iwsk_t *iwsk, void *ddp_hdr, const uint32_t ddp_hdr_len,
	 const void *ddp_payld, const ulpdu_len_t ddp_payld_len)
{
	mpa_sk_t mpask;
	mpask.sk = iwsk->sk;
	mpask.ent = &(iwsk->mpask);
	if (mpask.ent->use_mrkr)
		return mpa_wrt_mrkr_fpdu(&mpask, ddp_hdr, ddp_hdr_len, ddp_payld,
								 ddp_payld_len);
	else
		return mpa_wrt_plain_fpdu(&mpask, ddp_hdr, ddp_hdr_len, ddp_payld,
								  ddp_payld_len);
}

/* marker arithmetic is modulo 2^32 */
static int
mpa_wrt_mrkr_fpdu(mpa_sk_t *mpask, void *ddp_hdr, uint32_t ddp_hdr_len,
                  const void *ddp_payld, ulpdu_len_t ddp_payld_len)
{
	mpa_send_cntr++;
	ulpdu_len_t len = ddp_payld_len + ddp_hdr_len;
	/* first 2 bytes makeup mpa hdr */
	*((ulpdu_len_t *)ddp_hdr) = htons(len - 2);

	stream_pos_t sp = mpask->ent->send_sp; /* position in stream */
	marker_pos_t mp = mpask->ent->send_mp - sp; /* marker position in fpdu */
	uint32_t cp = 0;
	const char *pp = ddp_payld; /* payload position */
	uint32_t i = 0, l = 0, h = 0, f = 0, cm = 0, b = 0;
	const uint8_t pad = WORD_SZ*((len+WORD_SZ-1)/WORD_SZ) - len; /* 4-len%4 */
	marker_t mrkr;
	mrkr.reserved = 0;
	uint32_t num_mrkrs = 0;
	uint32_t fpdu_len = 0;
	int ret;

	l = len + pad;
	if (l > mp) {
		num_mrkrs = 1; /* num_mrkrs is atleast one */
		num_mrkrs += (l - mp)/PAYLD_CHNK;
	} else {
		num_mrkrs = 0;
	}
	fpdu_len = l + num_mrkrs*MARKER_SZ + CRC_SZ;
	mpask->ent->send_mp += num_mrkrs*MARKER_PERIOD;

	debug(2, "mp=%d sp=%d len=%d fpdu_len=%d", mp, sp,
		  len - 2, fpdu_len);

	/* fill ddp_header */
	b = 0;
	i = 0;
	if (mp < ddp_hdr_len) {
		mrkr_blk[i].fpduptr = mp;
		if (mp == 0){
			mpa_fill_blk(blks, &b, &mrkr_blk[i], MARKER_SZ, &cp);
			i++;
			mpa_fill_blk(blks, &b, ddp_hdr, ddp_hdr_len, &cp);
		} else {
			mpa_fill_blk(blks, &b, ddp_hdr, mp, &cp);
			mpa_fill_blk(blks, &b, &mrkr_blk[i], MARKER_SZ, &cp);
			i++;
			mpa_fill_blk(blks, &b, (uint8_t *)ddp_hdr + mp,
						 ddp_hdr_len - mp, &cp);
		}
		mp += MARKER_PERIOD;
		cm++; /* check */
	} else {
		mpa_fill_blk(blks, &b, ddp_hdr, ddp_hdr_len, &cp);
	}

	/* fill head of payload until the next marker */
	h = mp - cp;
	if (h > ddp_payld_len)
		h = ddp_payld_len;
	/* discard const to wedge into iovec */
	if (h) /* if some filler exists, fill with filler */
		mpa_fill_blk(blks, &b, pp, h, &cp);
	pp += h;

	/* fill body of payload */
	l = ddp_payld_len - h;
	while (l > 0) {
		mrkr_blk[i].fpduptr = mp;
		mpa_fill_blk(blks, &b, &mrkr_blk[i], MARKER_SZ, &cp);
		i++;
		mp += MARKER_PERIOD;

		f = mp - cp;
		if (f > l)
			f = l;
		mpa_fill_blk(blks, &b, pp, f, &cp);
		pp += f;

		l -= f;
		cm++; /* check */
	}

	/* pad to word boundary */
	if (pad) {
		pad_blk = 0;
		mpa_fill_blk(blks, &b, &pad_blk, pad, &cp);
	}

	if (cp == mp) {
		mrkr_blk[i].fpduptr = mp;
		mpa_fill_blk(blks, &b, &mrkr_blk[i], MARKER_SZ, &cp);
		i++;
		cm++;
	}

	mpask->ent->send_sp += cp;

	if (mpask->ent->use_crc) {
		crc_blk = htonl(crc32c_vec(blks, b));
		debug(4, "crc = %x b = %u", crc_blk, b);
		mpa_fill_blk(blks, &b, &crc_blk, CRC_SZ, &cp);
	}

	debug(2, "fpdu_len=%d, cp=%d, len=%d", fpdu_len, cp, len);

	iw_assert(cp == fpdu_len, "cp(%u) != fpdu_len(%u)", cp, fpdu_len);
	iw_assert(cm == num_mrkrs, "cm(%u) != num_mrkrs(%u)", cm, num_mrkrs);
	iw_assert(i < MAX_MRKRS, "i(%u) >= MAX_MRKRS(%u)", i, MAX_MRKRS);
	iw_assert(b < MAX_BLKS, "b(%u) >= MAX_BLKS(%u)", b, MAX_BLKS);

	ret = writev_full(mpask->sk, blks, b, fpdu_len);
	return ret;
}

static int
mpa_wrt_plain_fpdu(mpa_sk_t *mpask, void *ddp_hdr, uint32_t ddp_hdr_len,
                   const void *ddp_payld, ulpdu_len_t ddp_payld_len)
{
	ulpdu_len_t len = ddp_payld_len + ddp_hdr_len;
	/* first 2 bytes makeup mpa hdr */
	*((ulpdu_len_t *)ddp_hdr) = htons(len - 2);

	uint32_t cp = 0, bi = 0, fpdu_len = 0;
	const uint8_t pad = WORD_SZ*((len+WORD_SZ-1)/WORD_SZ) - len; /* 4-len%4 */
	int ret;

	fpdu_len = len + pad;

	debug(2, "fpdu_len = %d len = %d and pad = %d\n", fpdu_len, len, pad);

	mpa_fill_blk(blks, &bi, ddp_hdr, ddp_hdr_len, &cp);
	mpa_fill_blk(blks, &bi, ddp_payld, ddp_payld_len, &cp);

	if (pad) {
		pad_blk = 0;
		mpa_fill_blk(blks, &bi, &pad_blk, pad, &cp);
	}

	if (mpask->ent->use_crc) {
		fpdu_len += CRC_SZ;
		crc_blk = htonl(crc32c_vec(blks, bi));
		debug(4, "crc = %x b = %u", ntohl(crc_blk), bi);
		mpa_fill_blk(blks, &bi, &crc_blk, CRC_SZ, &cp);
	}

	mpask->ent->send_sp += cp;

	ret = writev_full(mpask->sk, blks, bi, fpdu_len);
	return ret;
}


/* FIXME: handle broken connection */
int
mpa_poll_generic(int timeout)
{
	int pollret;
	uint32_t i;

	/* poll till an event */
	pollret = poll(pollsks.sks, pollsks.numsks, timeout);
	if (pollret < 0) {
		if (errno == EINTR)
			pollret = 0;
		else
			return pollret;
	}

	for (i=0; pollret && i < pollsks.numsks; i++) {
		if (pollsks.sks[i].revents & POLLIN) {
			int ret = 0;
			iwsk_t *iwsk = iwsk_lookup(pollsks.sks[i].fd);
			iw_assert(iwsk != NULL, "%s:%d iwsk == NULL", __FILE__, __LINE__);
			ret = mpa_recv(iwsk);
			if (ret < 0)
				return ret;
			pollret--;
		}
	}
	iw_assert(pollret == 0, "%s:%d pollret(%d)", __FILE__, __LINE__, pollret);
	return 0;
}

int
mpa_recv(iwsk_t *iwsk)
{
	uint32_t bidx = 0, midx = 0;
	int ret;

	if (iwsk->mpask.use_mrkr)
		ret = mpa_rd_mrkr_fpdu(iwsk, &bidx, &midx);
	else
		ret = mpa_rd_plain_fpdu(iwsk, &bidx);
	if (ret < 0)
		return ret;

	ddp_process_ulpdu(iwsk, ddphdr_blk);
	return 0;
}

static inline void
mpa_read_mrkr(iwsk_t *s, uint32_t *bidx, uint32_t *midx, uint32_t *cp,
              uint32_t *mp)
{
/*	int ret = read(s->sk, &mrkr_blk[*midx], MARKER_SZ);
	if (ret < 0)
		error_errno("%s:%d", __FILE__, __LINE__);*/
	read_full(s->sk, &mrkr_blk[*midx], MARKER_SZ);
	blks[*bidx].iov_base = &mrkr_blk[*midx];
	blks[*bidx].iov_len = MARKER_SZ;
	*mp += MARKER_PERIOD;
	*cp += MARKER_SZ;
	(*bidx)++, (*midx)++;
}

static inline void
mpa_fill_blk(struct iovec *blks, uint32_t *bidx, const void *p, uint32_t len,
	     uint32_t *cp)
{
	/* possibly const pointer, but must wedge into non-const struct iovec */
	blks[*bidx].iov_base = (void *)(unsigned long) p;
	blks[*bidx].iov_len = len;
	(*bidx)++;
	*cp += len;
}

/*
 * read ddp_hdr_start in ddphdr_blk.
 * pass ddp_hdr_start to ddp layer to determine header size
 * read rest of ddphdr_blk
 * get buffer from ddp layer
 * generate recv vector
 * read into recv vector
 * do crc check.
 */
static int
mpa_rd_mrkr_fpdu(iwsk_t *iwsk, uint32_t *bidx, uint32_t *midx)
{
	mpa_recv_cntr++;
	stream_pos_t sp = iwsk->mpask.recv_sp;
	marker_pos_t mp = iwsk->mpask.recv_mp - sp;
	marker_pos_t mk = mp;
	uint32_t hdrsz = 0, cp = 0, pl = 0, ln = 0, hp = 0, lp = 0, crc = 0;
	uint32_t st_bidx = *bidx; /* starting block index */
	uint8_t *cur = NULL;
	uint8_t pad = 0;
	buf_t b;
	int ret;

	memset(ddphdr_blk, 0, DDP_MAX_HDR_SZ);

	if (mp == 0) {
		mpa_read_mrkr(iwsk, bidx, midx, &cp, &mp);
	}

	read_full(iwsk->sk, ddphdr_blk, sizeof(ddp_hdr_start_t));
	hdrsz = ddp_get_hdr_sz(ddphdr_blk);
	if (mp >= hdrsz) {
		read_full(iwsk->sk, (uint8_t *)ddphdr_blk + sizeof(ddp_hdr_start_t),
				  hdrsz - sizeof(ddp_hdr_start_t));
		/* we need hdr for crc calculation */
		mpa_fill_blk(blks, bidx, ddphdr_blk, hdrsz, &cp);
	} else {
		read_full(iwsk->sk, (uint8_t *)ddphdr_blk + sizeof(ddp_hdr_start_t),
				  mp - sizeof(ddp_hdr_start_t));
		/* we need hdr for crc calculation */
		mpa_fill_blk(blks, bidx, ddphdr_blk, mp, &cp);

		mpa_read_mrkr(iwsk, bidx, midx, &cp, &mp);

		read_full(iwsk->sk, (uint8_t *)ddphdr_blk + (mp - MARKER_PERIOD),
				  hdrsz - (mp - MARKER_PERIOD));
		/* we need hdr for crc calculation */
		mpa_fill_blk(blks, bidx,
					 (uint8_t *)ddphdr_blk + (mp - MARKER_PERIOD),
					 hdrsz - (mp - MARKER_PERIOD), &cp);
	}

	ret = ddp_get_sink(iwsk, ddphdr_blk, &b);
	if (ret < 0)
		return ret;

	cur = b.buf;
	pl = b.len;
	st_bidx = *bidx; /* readv from st_bidx, ignore already read parts */
	hp = cp; /* hp: position of last byte of header in fpdu */
	pad = WORD_SZ*((b.len+WORD_SZ-1)/WORD_SZ) - b.len; /* 4 - b.len%4 */
	debug(2, "pl %d", pl);
	while (pl > 0) {
		ln = mp - cp;
		if (ln > pl)
			ln = pl;
		mpa_fill_blk(blks, bidx, cur, ln, &cp);
		cur += ln;
		pl -= ln;

		if (pl == 0 && pad) {
			pad_blk = 0;
			mpa_fill_blk(blks, bidx, &pad_blk, pad, &cp);
		}

		if (cp == mp) {
			mpa_fill_blk(blks, bidx, &mrkr_blk[*midx], MARKER_SZ, &cp);
			mp += MARKER_PERIOD;
			(*midx)++;
		}
	}
	iw_assert(cur == ((uint8_t *)b.buf + b.len),
			  "cur(%p) != (b.buf + b.len)(%p)",
			  cur, ((uint8_t *)b.buf + b.len));

	iwsk->mpask.recv_sp += cp;
	iwsk->mpask.recv_mp += (*midx)*MARKER_PERIOD;

	if (iwsk->mpask.use_crc)
		mpa_fill_blk(blks, bidx, &crc_blk, CRC_SZ, &cp);

	readv_full(iwsk->sk, &blks[st_bidx], *bidx - st_bidx, cp - hp);

	for (lp = 0; lp < *midx; lp++) {
		if(mrkr_blk[lp].fpduptr != mk + lp*MARKER_PERIOD) {
			printerr("fpduptr (%d), expected (%d)",
					 mrkr_blk[lp].fpduptr, mk + lp*MARKER_PERIOD);
			return -EBADMSG;
		}
	}
	if(iwsk->mpask.recv_mp - sp != mk + (*midx * MARKER_PERIOD)) {
		printerr("new mp(%d) != expected new mp(%d)",
				 iwsk->mpask.recv_mp - sp, mk + (*midx * MARKER_PERIOD));
		return -EBADMSG;
	}

	if (iwsk->mpask.use_crc) {
		crc = crc32c_vec(blks, *bidx - 1);
		crc_blk = ntohl(crc_blk);
		debug(4, "%s: crc %x crc_blk %x bidx %u", __func__, crc,
		      crc_blk, *bidx - 1);
		if (crc != crc_blk)
			printerr("crc check failed. exp %x got %x",
					 crc, crc_blk); /* TODO: Surface this error */
			return -EBADMSG;
	}

	return 0;
}


static int
mpa_rd_plain_fpdu(iwsk_t *iwsk, uint32_t *bidx)
{
	uint32_t hdrsz = 0, cp = 0, hp = 0, crc = 0;
	uint32_t st_bidx = *bidx; /* starting block index */
	uint8_t pad = 0;
	buf_t b;

	memset(ddphdr_blk, 0, sizeof(DDP_MAX_HDR_SZ));

	read_full(iwsk->sk, ddphdr_blk, sizeof(ddp_hdr_start_t));
	hdrsz = ddp_get_hdr_sz(ddphdr_blk);
	read_full(iwsk->sk, (uint8_t *)ddphdr_blk + sizeof(ddp_hdr_start_t),
			  hdrsz - sizeof(ddp_hdr_start_t));

	mpa_fill_blk(blks, bidx, ddphdr_blk, hdrsz, &cp);

	ddp_get_sink(iwsk, ddphdr_blk, &b);
	pad = WORD_SZ*((b.len+WORD_SZ-1)/WORD_SZ) - b.len; /* 4 - b.len%4 */
	st_bidx = *bidx; /* readv from st_bidx, ignore already read parts */
	hp = cp; /* hp: position of last byte of header in fpdu */
	mpa_fill_blk(blks, bidx, b.buf, b.len, &cp);

	if (pad) {
		pad_blk = 0;
		mpa_fill_blk(blks, bidx, &pad_blk, pad, &cp);
	}

	if (iwsk->mpask.use_crc)
		mpa_fill_blk(blks, bidx, &crc_blk, CRC_SZ, &cp);

	readv_full(iwsk->sk, &blks[st_bidx], *bidx - st_bidx, cp - hp);

	iwsk->mpask.recv_sp += cp;

	if (iwsk->mpask.use_crc) {
		crc = crc32c_vec(blks, *bidx - 1);
		crc_blk = ntohl(crc_blk);
		debug(4, "crc %x %x bidx %u", crc, crc_blk, *bidx - 1);
		if (crc != crc_blk) {
			printerr("crc check failed. exp %x got %x",
					 crc, crc_blk); /* TODO: Surface this error */
			return -EBADMSG;
		}
	}

	return 0;
}
