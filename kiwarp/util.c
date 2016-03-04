/*
 * general utils
 *
 * $Id: util.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/net.h>
#include <linux/socket.h>
#include "util.h"

#if 0
static void dump_iovec(struct kvec *vec, int numvec)
{
	int i;
	size_t totlen = 0;

	for (i=0; i<numvec; i++)
		totlen += vec[i].iov_len;
	iwarp_debug("%s: vec %p numvec %d totlen %zu", __func__, vec, numvec,
	            totlen);
	for (i=0; i<numvec; i++)
		iwarp_debug("%s: vec[%d] base %p len %zu", __func__, i,
			    vec[i].iov_base, vec[i].iov_len);
}
#endif

/*
 * returns:
 * 	= 0 : on success
 * 	< 0 : on failure
 * Note that recvmsg() modifies the iovec as it fills it.
 */
int kernel_recvmsg_full(struct socket *sock, struct msghdr *msg,
			struct kvec *vec, int vec_sz, ssize_t len, int flags)
{
	int cc, ret = 0;

	iwarp_debug("%s: entry", __func__);
	while (len > 0) {
		cc = kernel_recvmsg(sock, msg, vec, vec_sz, len, flags);
		if (cc <= 0) {  /*if we dont' check for 0 it gets stuck in an infinite loop when other side disconencts and
					the entire node grinds to a halt*/
			iwarp_info("%s: first kernel_recvmsg err %d, sock %p msg %p vec %p vec_sz %d len %zd flags %d",
			           __func__, cc, sock, msg, vec, vec_sz, len, flags);
			ret = cc;
			goto out;
		}
		len -= cc;
		//~ iwarp_info("cc is %d\n", cc);
		iwarp_debug("%s: got %d, now len %zu", __func__, cc, len);
	}
	ret = 0;
out:
	iwarp_debug("%s: done, ret %d", __func__, ret);
	return ret;
}

/*
 * returns:
 * 	= 0 : on success
 * 	< 0 : on failure
 * NOTE: this is under assumption that kernel_sendmsg also behaves similarly.
 */
int kernel_sendmsg_full(struct socket *sock, struct msghdr *msg,
			struct kvec *vec, int vec_sz, size_t len)
{
	int cc, vi = 0, ret = 0;
	struct kvec residual;

	while (len > 0) {
		cc = kernel_sendmsg(sock, msg, &vec[vi], vec_sz - vi, len);
		if (cc < 0) {
			ret = cc;
			goto out;
		}
		len -= cc;
		if (len > 0) {
			while (vi < vec_sz && (int)vec[vi].iov_len < cc) {
				cc -= vec[vi].iov_len;
				vi++;
			}
			iwarp_debug("vi (%d) >= vec_sz (%d)", vi, vec_sz);

			residual.iov_base = vec[vi].iov_base + cc;
			residual.iov_len = vec[vi].iov_len - cc;
			cc = kernel_sendmsg(sock, msg, &residual, 1,
					    residual.iov_len);
			if (cc < 0) {
				/* TODO: if non-blocking check EAGAIN & loop */
				ret = cc;
				goto out;
			}
			len -= residual.iov_len;
			vi++;
		}
	}
	/* TODO: what to return if len == 0 and vi != vec_sz */
out:
	return ret;
}

