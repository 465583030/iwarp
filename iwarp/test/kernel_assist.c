/*
 * assist to debug kiwarp kernel.
 *
 * $Id: kernel_assist.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "util.h"
#include "cq.h"
#include "iwsk.h"
#include "rdmap.h"

static volatile sig_atomic_t interrupt = 0;
static int server = 0;

static void gotinterrupt(int signo)
{
	interrupt = 1;
	signo = 0;
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s {-r|-c} <peerIP> -n msgsz -t <testNUM>\n", __func__);
	exit(1);
}

static int build_connection(int server, struct in_addr *inaddr)
{
	int ret = 0;
	int s = 0;
	struct sockaddr_in sin;

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0)
		error_errno("socket");
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(7998);

	if (server) {
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
	} else {
		sin.sin_addr.s_addr = inaddr->s_addr;
		ret = connect(s, (struct sockaddr *) &sin, sizeof(sin));
		if (ret < 0)
			error_errno("connect");
	}
	return s;
}

static void dump_plain_fpdu(int fd)
{
	int ret = 0;
	struct sigaction act;
	uint8_t *buf = Malloc(1500);

	act.sa_handler = gotinterrupt;
	act.sa_flags = 0;
	if ((sigemptyset(&act.sa_mask) == -1)
	    || (sigaction(SIGINT, &act, NULL) == -1)) {
		error_errno("Failed to set sigint handler");
	}

	while (!interrupt) {
		ret = read(fd, buf, 1500);
		if (ret < 0) {
			free(buf);
			error_errno("%s:%d, read failed ret (%d)", __FILE__, __LINE__,
				    ret);
		}
		int i = 0;
		for (i = 0; i < ret; i++) {
			printf("%c:%x ", buf[i], buf[i]);
			if (i % 20 == 0)
				printf("\n");
		}
	}
	free(buf);
}

static void test_rdmap(int fd, int msgsz)
{
	uint8_t *buf = Malloc(msgsz);
	cqe_t cqe;
	cq_t *cq = cq_create(16);

	rdmap_init();
	rdmap_register_sock(fd, cq, cq);

	iwsk_t *iwsk = iwsk_lookup(fd);
	iwsk->mpask.use_mrkr = 0;
	iwsk->mpask.use_crc = 1;

	rdmap_post_recv(fd, buf, msgsz, 3);
	while(cq_consume(iwsk->rcq, &cqe) == -ENOENT)
		rdmap_poll();
	memset(buf, 0x55, msgsz);
	rdmap_send(fd, buf, msgsz, 4);
	while(cq_consume(iwsk->scq, &cqe) == -ENOENT)
		rdmap_poll();

	cq_destroy(cq);
	rdmap_deregister_sock(fd);
	free(buf);
	rdmap_fin();
}

static void test_rdma_write(int fd, int msgsz)
{
	uint8_t *buf = Malloc(msgsz + 16);
	cqe_t cqe;
	cq_t *cq = cq_create(16);

	rdmap_init();
	rdmap_register_sock(fd, cq, cq);
	iwsk_t *iwsk = iwsk_lookup(fd);
	iwsk->mpask.use_crc = 1;
	iwsk->mpask.use_mrkr = 0;

	if (server) {
		mem_init();
		mem_desc_t md = mem_register(buf, msgsz);
		stag_t stag = mem_stag_create(fd, md, 0, msgsz, STAG_W, 0);
		tag_offset_t to = (tag_offset_t)((uintptr_t)buf);
		memset(buf, msgsz, 0);
		memcpy(buf, &stag, sizeof(stag));
		memcpy(buf + sizeof(stag), &to, sizeof(to));
		debug(2, "sending stag %x to %lx", stag, to);
		rdmap_send(fd, buf, sizeof(stag) + sizeof(to), 0);
		while (cq_consume(iwsk->scq, &cqe) == -ENOENT);
		while(!buf[msgsz - 1])
			rdmap_poll();
		debug(2, "buf[%d] = %x", msgsz - 1, buf[msgsz - 1]);

		mem_stag_destroy(stag);
		mem_deregister(md);
		mem_fini();
	} else {
		return;
	}
	free(buf);
	rdmap_fin();

}

static void test_rdma_read(int fd, int msgsz)
{
	uint8_t *sbuf = Malloc(msgsz + 16);
	uint8_t *rbuf = Malloc(msgsz + 16);
	cqe_t cqe;
	cq_t *cq = cq_create(16);

	debug(2, "%d", __func__);
	rdmap_init();
	rdmap_register_sock(fd, cq, cq);
	iwsk_t *iwsk = iwsk_lookup(fd);
	iwsk->mpask.use_crc = 1;
	iwsk->mpask.use_mrkr = 0;

	if (server) {
		mem_init();
		mem_desc_t md = mem_register(rbuf, msgsz);
		stag_t stag = mem_stag_create(fd, md, 0, msgsz, STAG_R, 0);
		debug(2, "created stag %x md %x", stag, md);
		tag_offset_t to = (tag_offset_t)((uintptr_t)rbuf);
		memset(sbuf, msgsz, 0);
		memcpy(sbuf, &stag, sizeof(stag));
		memcpy(sbuf + sizeof(stag), &to, sizeof(to));
		debug(2, "sending stag %x to %lx", stag, to);
		rdmap_send(fd, sbuf, sizeof(stag) + sizeof(to), 0);
		while (cq_consume(iwsk->scq, &cqe) == -ENOENT);
		rdmap_post_recv(fd, sbuf, 10, 2);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
		debug(2, "sbuf[0] = %x", sbuf[0]);
		mem_stag_destroy(stag);
		mem_deregister(md);
		mem_fini();
	} else {
	}
	free(sbuf);
	free(rbuf);
	rdmap_fin();
}

static void test_init_startup(int fd, int msgsz)
{
	int ret = 0;
	char srvrpd[] = "server";
	char clntpd[] = "client";
	char *buf = Malloc(msgsz + 16);
	cq_t *cq = cq_create(16);

	debug(2, "%d", __func__);
	rdmap_init();
	rdmap_register_sock(fd, cq, cq);
	iwsk_t *iwsk = iwsk_lookup(fd);
	iwsk->mpask.use_crc = 1;
	iwsk->mpask.use_mrkr = 0;

	if (server) {
		memset(buf, 0, sizeof(clntpd));
		ret = rdmap_init_startup(fd, !server, srvrpd, buf, sizeof(clntpd));
		debug(2, "expected %s recvd %s", clntpd, buf);
	} else {
	}
	free(buf);
	rdmap_fin();
}


int main(int argc, char *argv[])
{
	int fd, ret;
	int test = 1;
	struct in_addr inaddr;
	int msgsz = 1500;
	set_progname(argc, argv);

	if (argc < 4) usage();
	if (!strcmp(argv[1], "-r"))
		server = 1;
	else if (!strcmp(argv[1], "-c"))
		server = 0;
	else
		usage();
	ret = inet_aton(argv[2], &inaddr);
	if (ret == 0)
		error_errno("inet_aton failed");
	if (!strcmp(argv[3], "-n"))
		msgsz = atoi(argv[4]);
	if (!strcmp(argv[5], "-t"))
		test = atoi(argv[6]);

	/* build and connect a socket */
	fd = build_connection(server, &inaddr);

	switch (test) {
		case 1:
			dump_plain_fpdu(fd);
			break;
		case 2:
			test_rdmap(fd, msgsz);
			break;
		case 3:
			test_rdma_write(fd, msgsz);
			break;
		case 4:
			test_rdma_read(fd, msgsz);
			break;
		case 5:
			test_init_startup(fd, msgsz);
		default:
			break;
	}
	ret = close(fd);
	if (ret)
		printerr("err %d closin fd %d\n", ret, fd);
	return ret;
}
