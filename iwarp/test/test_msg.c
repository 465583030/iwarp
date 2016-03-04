/*
 * test different message lengths, message orders, message throughput
 *
 * $Id: test_msg.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "ddp.h"
#include "mpa.h"
#include "common.h"
#include "util.h"
#include "rdmap.h"
#include "test_stub.h"
#include "iwsk.h"
#include "mem.h"
#include "cq.h"


static bool_t is_server = FALSE;
static int32_t length = -1;
static int32_t numiters = -1;

static void test_multi_msg(socket_t sk);
static void test_spray(socket_t sk, bool_t use_mrkr, bool_t use_crc);

static void ATTR_NORETURN
local_usage(const char *funcname)
{
	fprintf(stderr, "%s: Usage: %s [-s 1] [-l <msg_len>] [-n <numiters>] "
			"<server>\n", funcname, progname);
	exit(1);
}

static void
parse_local_options(int argc, char *argv[])
{
	while(++argv, --argc > 0){
		const char *cp;
		int i = 0;
		if (**argv == '-'){
			switch((*argv)[1]){
				case 'l':
					cp = &((*argv)[2]);
					for (i=1; *cp && *cp == "length"[i]; cp++, i++);
					if(*cp)
						local_usage(__func__);
					if(++argv, --argc <= 0) local_usage("length");
					length = atoi(*argv);
					break;
				case 'n':
					cp = &((*argv)[2]);
					for (i=1; *cp && *cp == "numiters"[i]; cp++, i++);
					if(*cp)
						local_usage(__func__);
					if(++argv, --argc <= 0) local_usage("numiters");
					numiters = atoi(*argv);
					break;
				case 's':
					++argv, --argc;
					break;
				default:
					local_usage(__func__);
			}
		} else {
			if (length == -1 || numiters == -1)
				local_usage(__func__);
		}
	}
	if (length == -1 || numiters == -1)
		local_usage(__func__);
}

static void
test_multi_msg(socket_t sk)
{
	uint32_t NUM = length/4;
	buf_t b;
	b.len = NUM*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);

	rdmap_init();
	rdmap_register_sock(sk, NULL, NULL);

	rdmap_deregister_sock(sk);
	free(b.buf);
	rdmap_fin();
}

static void
test_spray(socket_t sk, bool_t use_mrkr, bool_t use_crc)
{
	int32_t i;
	int32_t window = 64;
	uint32_t off=0;
	char *buf;
	cqe_t cqe;
	cq_t *cq;

	if (length < 8)
		buf = Malloc(8);
	else
		buf = Malloc(length);
	memset(buf, 0, length);

	cq = cq_create(16);

	mem_init();
	rdmap_init();
	rdmap_register_sock(sk, cq, cq);
	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = use_mrkr;
	iwsk->mpask.use_crc = use_crc;
	debug(2, "iwsk %p %d", iwsk, iwsk->sk);

	if (is_server) {
		off = sizeof(stag_t) + sizeof(tag_offset_t) + sizeof(int32_t);
		char *local_buf = Malloc(20);
		rdmap_post_recv(sk, local_buf, off, 3);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();

		off = 0;
		stag_t rem_stag = *(stag_t *)(local_buf + off);
		off += sizeof(rem_stag);
		tag_offset_t rem_to = *(tag_offset_t *)(local_buf + off);
		off += sizeof(rem_to);
		int32_t rem_len = *(int32_t *)(local_buf + off);
		debug(2, "recieved stag:%d to:%Lx len:%d", rem_stag, rem_to, rem_len);

		memset(buf, 0, rem_len);
		for (i=0; i<numiters; i++) {
			rdmap_post_recv(sk, local_buf, 0, 4);
			int j = 0;
			for (j=0; j < window-1; j++) {
				rdmap_rdma_write(sk, rem_stag, rem_to, buf, rem_len, 3);
				while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
			}
			buf[rem_len-1] = 'A';
			rdmap_rdma_write(sk, rem_stag, rem_to, buf, rem_len, 3);
			while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
			while(cq_consume(iwsk->rcq, &cqe) == -ENOENT)
				rdmap_poll();
			buf[rem_len-1] = '\0';
		}
		free(local_buf);
	} else {
		mem_desc_t md = mem_register(buf, length);
		stag_t stag = mem_stag_create(sk, md, 0, length, STAG_RW, 0);
		tag_offset_t to = (tag_offset_t)((uintptr_t)buf);
		off = sizeof(stag_t) + sizeof(tag_offset_t) + sizeof(int32_t);
		char *local_buf = Malloc(off);

		off=0;
		*(stag_t *)(local_buf + off) = stag; off += sizeof(stag);
		*(tag_offset_t *)(local_buf + off) = to; off += sizeof(to);
		*(int32_t *)(local_buf + off) = length; off += sizeof(length);
		rdmap_send(sk, local_buf, off, 1);
		while(cq_consume(iwsk->scq, &cqe) == -ENOENT);

		debug(2, "sent stag:%d to:%Lx len:%d", stag, to, length);

		memset(buf, 0, length);
		for (i=0; i<numiters; i++) {
			for ( ; ; ) {
				if (*(volatile char *)(buf + length-1) != 0)
					break;
				rdmap_poll();
			}
			buf[length-1] = '\0';
			rdmap_send(sk, local_buf, 0, 2);
			while(cq_consume(iwsk->scq, &cqe) == -ENOENT);

		}

		free(local_buf);
		mem_deregister(md);
	}

	cq_destroy(cq);
	rdmap_deregister_sock(sk);
	free(buf);
	rdmap_fin();
	mem_fini();
}

int
main(int argc, char *argv[])
{
	parse_options(argc, argv);
	parse_local_options(argc, argv);
	is_server = get_isserver();
	socket_t sk = init_connection(is_server);
	test_multi_msg(sk);
	test_spray(sk, FALSE, FALSE);
	close(sk);
	return 0;
}
