/*
 * Run either as:
 *
 * iWarpRTT -r 10.0.0.4 [ -s <bufsize> ]
 * test_talk_ams -c 10.0.0.4 [ -s <bufsize> ]
 *
 * or
 *
 * test_talk_ams -r 10.0.0.5 [ -s <bufsize> ]
 * iWarpRTT -c 10.0.0.5 [ -s <bufsize> ]
 *
 * $Id: test_talk_ams.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later. (See
 * LICENSE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "crc32c.h"
#include "util.h"
#include "rdmap.h"
#include "mpa.h"
#include "mem.h"
#include "cq.h"

static int iters = 10;
static int bufsize = 1024;

static stag_t remote_stag;
static uint64_t remote_to;
static uint32_t remote_len;
static stag_t local_stag;
static uint64_t local_to;
static uint32_t local_len;

static void local_usage(const char *err_pos);
static void parse_local_options(int argc, char *argv[]);

static void read_stag_to_len(int s);
static void write_stag_to_len(int s);
static void read_ping(int s, int c, char *b);
static void send_ping(int s, int c, char *b);

static void local_usage(const char *err_pos)
{
	fprintf(stderr, "Usage: %s:%s {-r|-c} <peerIP> -s <size> \n",
			err_pos, progname);
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
				case 's':
					cp = &((*argv)[2]);
					for (i=1; *cp && *cp == "size"[i]; cp++, i++);
					if(*cp)
						local_usage(__func__);
					if(++argv, --argc <= 0) local_usage("size");
					bufsize = atoi(*argv);
					break;
				case 'r':
					++argv, --argc;
					break;
				case 'c':
					++argv, --argc;
					break;
				default:
					local_usage(__func__);
			}
		}
	}
}


int main(int argc, char *argv[])
{
	int s, ret;
	struct in_addr inaddr;
	struct sockaddr_in sin;
	int server = 0;
	char *remote_priv_data;
/*	FILE *fp = NULL;*/

	set_progname(argc, argv);
	if (argc < 3)
		local_usage("argc < 3");
	if (!strcmp(argv[1], "-r"))
		server = 1;
	else if (!strcmp(argv[1], "-c"))
		server = 0;
	else
		local_usage("bad args");
	ret = inet_aton(argv[2], &inaddr);
	if (ret == 0)
		error("%s: invalid IP address \"%s\"", __func__, argv[2]);
	parse_local_options(argc, argv);

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0)
		error_errno("socket");
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(7998);

	remote_priv_data = malloc(20);

	if (server) {
		/* server */
		ret = bind(s, (struct sockaddr *) &sin, sizeof(sin));
		if (ret < 0)
			error_errno("bind");
		ret = listen(s, 5);
		if (ret < 0)
			error_errno("listen");
		ret = accept(s, 0, 0);
		if (ret < 0)
			error_errno("accept");
		close(s);
		s = ret;

		cq_t *scq = cq_create(16);
		cq_t *rcq = cq_create(16);
		mem_init();
		rdmap_init();
		rdmap_register_sock(s, scq, rcq);

		iwsk_t *iwsk = iwsk_lookup(s);
		mpa_init_startup(iwsk, FALSE, "client", remote_priv_data, 20);

		char *b = Malloc(bufsize);
		local_len = bufsize;
		local_to = (tag_offset_t)((uintptr_t)b);
/*		local_to = 0;*/
		mem_desc_t md = mem_register(b, bufsize);
		local_stag = mem_stag_create(s, md, 0, bufsize, STAG_W|STAG_R, 0);
		if (local_stag < 0)
		    error("%s: mem_stag_create failed", __func__);

		write_stag_to_len(s);
		read_stag_to_len(s);

		int i = 0;
		for(i=0; i<iters; i++) {
			memset(b, 0, local_len);
			send_ping(s, 'R', b);
			memset(b, 0, local_len);
			read_ping(s, 'C', b);
		}

		mem_deregister(md);
		free(b);
/*		close(s);*/

		rdmap_deregister_sock(s);
		cq_destroy(scq);
		cq_destroy(rcq);
		rdmap_fin();
		mem_fini();
	} else {
		/* client */
		sin.sin_addr.s_addr = inaddr.s_addr;
		ret = connect(s, (struct sockaddr *) &sin, sizeof(sin));
		if (ret < 0)
			error_errno("connect");
		sleep(1);

		cq_t *scq = cq_create(16);
		cq_t *rcq = cq_create(16);
		mem_init();
		rdmap_init();
		rdmap_register_sock(s, scq, rcq);

		iwsk_t *iwsk = iwsk_lookup(s);
		mpa_init_startup(iwsk, TRUE, "client", remote_priv_data, 20);

		char *b = Malloc(bufsize);
		local_len = bufsize;
		local_to = (tag_offset_t)((uintptr_t)b);
/*		local_to = 0;*/
		mem_desc_t md = mem_register(b, bufsize);
		local_stag = mem_stag_create(s, md, 0, bufsize, STAG_W|STAG_R, 0);
		if (local_stag < 0)
		    error("%s: mem_stag_create failed", __func__);

		read_stag_to_len(s);
		write_stag_to_len(s);

/*		fp = fdopen(s, "rw");*/

		int i = 0;
		for(i=0; i<iters; i++) {
			memset(b, 0, local_len);
			read_ping(s, 'R', b);
			memset(b, 0, local_len);
			send_ping(s, 'C', b);
/*			fflush(fp);*/
		}

		mem_deregister(md);
		free(b);

/*		close(s);*/

		rdmap_deregister_sock(s);
		cq_destroy(scq);
		cq_destroy(rcq);
		rdmap_fin();
		mem_fini();

	}

	printf("done\n");
	sleep(1);
	close(s);
	return 0;
}

static void
read_stag_to_len(int s)
{
	char buf[2048];
	int off;
	buf_t b;

	b.buf = buf;
	b.len = 2048;

	rdmap_post_recv(s, b.buf, b.len, 0);
	iwsk_t *iwsk = iwsk_lookup(s);
	cqe_t cqe;
	while(cq_consume(iwsk->rcq, &cqe) == -ENOENT)
		rdmap_poll();

	off = 0;
	remote_stag = *(stag_t *)(buf+off); off += 4;
	remote_to = *(tag_offset_t *)(buf+off); off += 8;
	remote_len = *(uint32_t *)(buf+off); off += 4;
	printf("got stag %x to %llx len %d\n", remote_stag,
	       (unsigned long long) remote_to, remote_len);
}

static void
write_stag_to_len(int s)
{
	char buf[2048];
	int off;

	memset(buf, 0, 2048);
/*	local_stag = 0x01798a00;*/
/*	local_to = 0x000000000804e008;*/
/*	local_len = 1024;*/
	off = 0;
	*(stag_t *)(buf+off) = local_stag; off += 4;
	*(tag_offset_t *)(buf+off) = local_to; off += 8;
	*(uint32_t *)(buf+off) = local_len; off += 4;
	rdmap_send(s, buf, off, 0);

	iwsk_t *iwsk = iwsk_lookup(s);
	cqe_t cqe;
	while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
	printf("sent stag %x to %llx len %d\n", local_stag,
	       (unsigned long long) local_to, local_len);
}

static void
read_ping(int s ATTR_UNUSED, int c, char *b)
{
	const int off = 0;
	int i;

	while(b[bufsize - 1] == '\0')
		rdmap_poll();

	for (i=0; i<bufsize; i++)
		if (b[off+i] != c)
			error("payload expected 0x%02x at payload pos %d, got 0x%02x",
				  c, i, b[off+i]);
	/* printf("%s: okay\n", __func__); */
}

static void
send_ping(int s, int c, char *b)
{
	int i;
	iwsk_t *iwsk = iwsk_lookup(s);
	cqe_t cqe;

	for (i=0; i<bufsize; i++)
		b[i] = c;
	rdmap_rdma_write(s, remote_stag, remote_to, b, bufsize, 0);

	while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
	/* printf("%s: okay\n", __func__); */
}

