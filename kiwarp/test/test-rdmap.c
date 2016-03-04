/*
 * Test basic chardev open/close/operations.
 *
 * Do:  p=7000 ; ( echo recvbuf | nc -l $p ) & sleep 1 ; test/test-dev $p
 *
 * $Id: test-rdmap.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "util.h"
#include "kiwarp/user.h"

#define uint64_from_ptr(p) ((uint64_t)(uintptr_t)(p))

static const char *kiwarp_dev = "/dev/kiwarp";
static int server = 0;

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
		int one = 1;
		ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one,
		                 sizeof(one));
		if (ret < 0)
			error_errno("setsockopt SO_REUSEADDR");
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

static void test_rdmap_send(int kiwarp_fd, int fd, uint64_t scq, uint64_t rcq,
			    int msgsz)
{
	int ret = 0;
	struct user_post_recv upr;
	struct user_send us;
	struct user_poll upoll;
	struct user_mem_reg umr;
	struct user_stag_create usc;
	struct work_completion wc;
	unsigned long send_md, recv_md;
	int32_t send_stag, recv_stag;
	char *sbuf;
	char *rbuf;

	debug(2, "%s: fd %d scq %Lx rcq %Lx msgsz %d", __func__, fd, scq, rcq,
	      msgsz);
	sbuf = Malloc(msgsz);
	rbuf = Malloc(msgsz);

	/* register send area */
	umr.cmd = IWARP_MEM_REG;
	umr.address = sbuf;
	umr.len = msgsz;
	umr.mem_desc = &send_md;
	ret = write(kiwarp_fd, &umr, sizeof(umr));
	if (ret < 0)
		error_errno("mem_register");
	if (ret != sizeof(umr))
		error("wrote only %d\n", ret);
	debug(2, "registered send mem_region %lx", send_md);

	/* register recv area */
	umr.cmd = IWARP_MEM_REG;
	umr.address = rbuf;
	umr.len = msgsz;
	umr.mem_desc = &recv_md;
	ret = write(kiwarp_fd, &umr, sizeof(umr));
	if (ret < 0)
		error_errno("mem_register");
	if (ret != sizeof(umr))
		error("wrote only %d\n", ret);
	debug(2, "registered recv mem_region %lx", recv_md);

	/* create send stag */
	usc.cmd = IWARP_STAG_CREATE;
	usc.md = send_md;
	usc.start = sbuf;
	usc.len = msgsz;
	usc.rw = 0x1;  /* read */
	usc.prot_domain = 1;
	usc.stag = &send_stag;
	ret = write(kiwarp_fd, &usc, sizeof(usc));
	if (ret < 0)
		error_errno("mem_create_stag");
	if (ret != sizeof(usc))
		error("wrote only %d\n", ret);
	debug(2, "created send stag %lx", send_stag);

	/* create recv stag */
	usc.cmd = IWARP_STAG_CREATE;
	usc.md = recv_md;
	usc.start = rbuf;
	usc.len = msgsz;
	usc.rw = 0x2;  /* write */
	usc.prot_domain = 1;
	usc.stag = &recv_stag;
	ret = write(kiwarp_fd, &usc, sizeof(usc));
	if (ret < 0)
		error_errno("mem_create_stag");
	if (ret != sizeof(usc))
		error("wrote only %d\n", ret);
	debug(2, "created recv stag %lx", recv_stag);

	/* post a recv operation */
	memset(rbuf, 'r', msgsz);
	upr.cmd = IWARP_POST_RECV;
	upr.fd = fd;
	upr.id = uint64_from_ptr(&upr);
	upr.buf = rbuf;
	upr.len = msgsz;
	upr.local_stag = recv_stag;
	ret = write(kiwarp_fd, &upr, sizeof(upr));
	if (ret < 0)
		error_errno("post recv");
	if (ret != sizeof(upr))
		error("post recv short write: %d of %d", ret,
		      (int) sizeof(upr));

	/* post a send operation */
	memset(sbuf, 's', msgsz);
	us.cmd = IWARP_SEND;
	us.fd = fd;
	us.id = uint64_from_ptr(&us);
	us.buf = sbuf;
	us.len = msgsz;
	us.local_stag = send_stag;
	ret = write(kiwarp_fd, &us, sizeof(us));
	if (ret < 0)
		error_errno("post send");
	if (ret != sizeof(us))
		error("post send short write: %d of %d", ret, (int) sizeof(us));

	/* poll for send completion */
	upoll.cmd = IWARP_POLL;
	upoll.cq_handle = scq;
	upoll.wc = &wc;
	ret = write(kiwarp_fd, &upoll, sizeof(upoll));
	if (ret < 0)
		error_errno("poll send");
	if (ret != sizeof(upoll))
		error("poll send short write: %d of %d", ret, (int) sizeof(upoll));
	if (wc.id != uint64_from_ptr(&us))
		error("poll send returned wc.id %Lu, expecting %p\n", (unsigned long long) wc.id,
		      &us);
	printf("send okay\n");

	/* poll for recv completion, waits for packet arrival */
	for (;;) {
		upoll.cmd = IWARP_POLL;
		upoll.cq_handle = rcq;
		upoll.wc = &wc;
		ret = write(kiwarp_fd, &upoll, (int) sizeof(upoll));
		printf("poll ret = %d\n", ret);
		if (ret < 0) {
			if (errno == EAGAIN) {
				printf("no recv, trying again\n");
				sleep(1);
				continue;
			}
			error_errno("poll recv");
		}
		if (ret != sizeof(upoll))
			error("poll recv short write: %d of %d", ret,
			      (int) sizeof(upoll));
		break;
	}
	if (wc.id != uint64_from_ptr(&upr))
		error("poll recv returned wc.id %Lu, expecting %p\n",
		      (unsigned long long) wc.id, &upr);
	rbuf[msgsz] = '\0';
	printf("rbuf has \"%s\"\n", rbuf);
}

static void test_rdmap_rdma_write(int kiwarp_fd, int fd, uint64_t scq,
				  uint64_t rcq, int msgsz)
{
	int ret = 0;
	struct user_mem_reg umr;
	struct user_stag_create usc;
	struct user_rdma_write urw;
	struct user_post_recv upr;
	struct user_send us;
	struct user_poll upoll;
	struct user_encourage ue;
	struct work_completion wc;
	unsigned long bufs_md;
	int32_t bufs_stag;
	int32_t stag;
	uint64_t to;
	char *sbuf;
	char *rbuf;
	int need;

	/* room for overhead stuff needed */
	if (msgsz < (int) (sizeof(stag) + sizeof(to)))
	    msgsz = sizeof(stag) + sizeof(to);

	sbuf = Malloc(2 * msgsz);
	rbuf = sbuf + msgsz;

	/* register memory for the bufs */
	umr.cmd = IWARP_MEM_REG;
	umr.address = sbuf;
	umr.len = 2 * msgsz;
	umr.mem_desc = &bufs_md;
	ret = write(kiwarp_fd, &umr, sizeof(umr));
	if (ret < 0)
	    error_errno("mem register");
	if (ret != sizeof(umr))
	    error("mem register short write: %d of %d", ret, (int) sizeof(umr));

	/* get an stag for both bufs */
	usc.cmd = IWARP_STAG_CREATE;
	usc.md = bufs_md;
	usc.start = sbuf;
	usc.len = 2 * msgsz;
	usc.rw = 0x3;
	usc.prot_domain = 0;
	usc.stag = &bufs_stag;
	ret = write(kiwarp_fd, &usc, sizeof(usc));
	if (ret < 0)
	    error_errno("stag create");
	if (ret != sizeof(usc))
	    error("stag create short write: %d of %d", ret, (int) sizeof(usc));

	/* post a recv operation */
	upr.cmd = IWARP_POST_RECV;
	upr.fd = fd;
	upr.id = uint64_from_ptr(&upr);
	upr.buf = rbuf;
	upr.len = sizeof(stag) + sizeof(to);
	upr.local_stag = bufs_stag;
	ret = write(kiwarp_fd, &upr, sizeof(upr));
	if (ret < 0)
		error_errno("post recv");
	if (ret != sizeof(upr))
		error("post recv short write: %d of %d", ret, (int)sizeof(upr));

	/* clear for debugging */
	memset(sbuf, 0x5a, 2*msgsz);

	/* post a send operation with stag/to */
	to = (uint64_t) (unsigned long) rbuf;
	memcpy(sbuf, &bufs_stag, sizeof(stag));
	memcpy(sbuf + sizeof(stag), &to, sizeof(to));
	debug(2, "%s: sending stag %x to %lx", __func__, bufs_stag, to);
	us.cmd = IWARP_SEND;
	us.fd = fd;
	us.id = uint64_from_ptr(&us);
	us.buf = sbuf;
	us.len = sizeof(stag) + sizeof(to);
	us.local_stag = bufs_stag;
	ret = write(kiwarp_fd, &us, sizeof(us));
	if (ret < 0)
		error_errno("post send");
	if (ret != sizeof(us))
		error("post send short write: %d of %d", ret, (int) sizeof(us));

	/* poll for send and recv completions, waits for packet arrival */
	for (need=2; need>0; ) {
		upoll.cmd = IWARP_POLL;
		upoll.cq_handle = rcq;
		upoll.wc = &wc;
		ret = write(kiwarp_fd, &upoll, sizeof(upoll));
		if (ret < 0) {
			if (errno == EAGAIN) {
				printf("no recv, trying again\n");
				sleep(1);
				continue;
			}
			error_errno("poll recv");
		}
		if (ret != sizeof(upoll))
			error("poll recv short write: %d of %d", ret,
			      (int) sizeof(upoll));
		--need;
		debug(2, "%s: poll okay", __func__);
	}

	/* remember his stag/to */
	memcpy(&stag, rbuf, sizeof(stag));
	memcpy(&to, rbuf + sizeof(stag), sizeof(to));
	debug(2, "%s: got his stag %x to %lx", __func__, stag, to);

	if (server) {
	    /* execute rdma write */
	    memset(sbuf, 0x5A, msgsz);
	    urw.cmd = IWARP_RDMA_WRITE;
	    urw.fd = fd;
	    urw.id = uint64_from_ptr(&urw);
	    urw.buf = sbuf;
	    urw.len = msgsz;
	    urw.local_stag = bufs_stag;
	    urw.sink_stag = stag;
	    urw.sink_to = to;
	    ret = write(kiwarp_fd, &urw, sizeof(urw));
	    if (ret < 0)
		    error_errno("rdma write");
	    if (ret != sizeof(urw))
		    error("rdma write short: %d of %d", ret, (int) sizeof(urw));

	    /* grab cqe */
	    upoll.cmd = IWARP_POLL;
	    upoll.cq_handle = scq;
	    upoll.wc = &wc;
	    ret = write(kiwarp_fd, &upoll, sizeof(upoll));
	    if (ret < 0)
		    error_errno("poll rdma write");
	    if (ret != sizeof(upoll))
		    error("poll send short write: %d of %d", ret, (int) sizeof(upoll));
	    if (wc.id != uint64_from_ptr(&urw))
		    error("poll send returned wc.id %Lu, expecting %p\n",
			  (unsigned long long) wc.id, &urw);
	    debug(1, "%s: rdma write okay", __func__);
	    sleep(2);  /* let rdma write finish on other side */
	} else {
	    /* poll on rbuf */
	    for (;;) {
		debug(2, "%s: polling rbuf", __func__);
		ue.cmd = IWARP_ENCOURAGE;
		ret = write(kiwarp_fd, &ue, sizeof(ue));
		if (ret < 0)
			error_errno("encourage");
		if (ret != sizeof(ue))
			error("encourage short: %d of %d", ret,
			      (int) sizeof(ue));
		if (*rbuf == 0x5a) {
		    debug(1, "%s: rdma write worked okay", __func__);
		    break;
		}
		sleep(1);
	    }
	}
}

static void test_rdmap_rdma_read(int kiwarp_fd, int fd, uint64_t scq,
				 uint64_t rcq, int msgsz)
{
	int ret, need, i;
	struct user_mem_reg umr;
	struct user_mem_dereg umd;
	struct user_stag_create usc;
	struct user_stag_destroy usd;
	struct user_rdma_read urr;
	struct user_post_recv upr;
	struct user_send us;
	struct user_poll upoll;
	struct user_encourage ue;
	struct work_completion wc;
	int32_t stag, bufs_stag;
	uint64_t to;
	unsigned long bufs_md;
	unsigned char *sbuf;
	unsigned char *rbuf;

	debug(2, "%s", __func__);

	/* room for overhead stuff needed */
	if (msgsz < (int) (sizeof(stag) + sizeof(to)))
	    msgsz = sizeof(stag) + sizeof(to);

	sbuf = Malloc(2 * msgsz);
	rbuf = sbuf + msgsz;

	/* register memory */
	umr.cmd = IWARP_MEM_REG;
	umr.address = sbuf;
	umr.len = 2 * msgsz;
	umr.mem_desc = &bufs_md;
	ret = write(kiwarp_fd, &umr, sizeof(umr));
	if (ret < 0)
		error_errno("mem register");
	if (ret != sizeof(umr))
	    error("mem register short write: %d of %d", ret, (int) sizeof(umr));
	debug(2, "registered mem_region %x", bufs_md);

	/* get an stag for both bufs */
	usc.cmd = IWARP_STAG_CREATE;
	usc.md = bufs_md;
	usc.start = sbuf;
	usc.len = 2 * msgsz;
	usc.rw = 0x3;
	usc.prot_domain = 0;
	usc.stag = &bufs_stag;
	ret = write(kiwarp_fd, &usc, sizeof(usc));
	if (ret < 0)
	    error_errno("stag create");
	if (ret != sizeof(usc))
	    error("stag create short write: %d of %d", ret, (int) sizeof(usc));
	debug(2, "created stag %x", bufs_stag);


	/* post a recv operation */
	upr.cmd = IWARP_POST_RECV;
	upr.fd = fd;
	upr.id = uint64_from_ptr(&upr);
	upr.buf = rbuf;
	upr.len = sizeof(stag) + sizeof(to);
	upr.local_stag = bufs_stag;
	ret = write(kiwarp_fd, &upr, sizeof(upr));
	if (ret < 0)
		error_errno("post recv");
	if (ret != sizeof(upr))
		error("post recv short write: %d of %d", ret, (int)sizeof(upr));
	debug(2, "posted recv %p", rbuf);

	/* clear for debugging */
	memset(sbuf, 0x5a, 2*msgsz);

	/* post a send operation with stag/to */
	to = (uint64_t) (unsigned long) rbuf;
	memcpy(sbuf, &bufs_stag, sizeof(stag));
	memcpy(sbuf + sizeof(stag), &to, sizeof(to));
	debug(2, "%s: sending stag %x to %lx", __func__, bufs_stag, to);
	us.cmd = IWARP_SEND;
	us.fd = fd;
	us.id = uint64_from_ptr(&us);
	us.buf = sbuf;
	us.len = sizeof(stag) + sizeof(to);
	us.local_stag = bufs_stag;
	ret = write(kiwarp_fd, &us, sizeof(us));
	if (ret < 0)
		error_errno("post send");
	if (ret != sizeof(us))
		error("post send short write: %d of %d", ret, (int) sizeof(us));

	/* poll for send and recv completions, waits for packet arrival */
	for (need=2; need>0; ) {
		upoll.cmd = IWARP_POLL;
		upoll.cq_handle = rcq;
		upoll.wc = &wc;
		ret = write(kiwarp_fd, &upoll, sizeof(upoll));
		if (ret < 0) {
			if (errno == EAGAIN) {
				debug(1, "%s: no recv, trying again\n");
				sleep(1);
				continue;
			}
			error_errno("poll recv");
		}
		if (ret != sizeof(upoll))
			error("poll recv short write: %d of %d", ret,
			      (int) sizeof(upoll));
		--need;
		debug(2, "%s: poll okay", __func__);
	}

	/* remember his stag/to */
	memcpy(&stag, rbuf, sizeof(stag));
	memcpy(&to, rbuf + sizeof(stag), sizeof(to));
	debug(2, "%s: got his stag %x to %lx", __func__, stag, to);

	if (server) {
	    /* wait for him to prep data space */
	    sleep(1);
	    /* execute rdma read */
	    urr.cmd = IWARP_RDMA_READ;
	    urr.fd = fd;
	    urr.id = uint64_from_ptr(&urr);
	    urr.src_stag = stag;
	    urr.src_to = to;
	    urr.len = msgsz;
	    urr.sink_stag = bufs_stag;
	    urr.sink_to = uint64_from_ptr(rbuf);
	    ret = write(kiwarp_fd, &urr, sizeof(urr));
	    if (ret < 0)
		    error_errno("rdma read");
	    if (ret != sizeof(urr))
		    error("rdma read short: %d of %d", ret, (int) sizeof(urr));

	    /* grab cqe */
	    while(1) {
		    upoll.cmd = IWARP_POLL;
		    upoll.cq_handle = scq;
		    upoll.wc = &wc;
		    ret = write(kiwarp_fd, &upoll, sizeof(upoll));
		    if (ret < 0) {
			    if (errno == EAGAIN) {
				    printf("no recv, trying again\n");
				    sleep(1);
				    continue;
			    }
			    error_errno("poll recv");
		    }
		    if (ret != sizeof(upoll))
			    error("poll rr short : %d of %d", ret,
			          (int) sizeof(upoll));
		    if (wc.id != uint64_from_ptr(&urr))
			    error("poll rr wc.id %Lu, expecting %p\n",
				  Lu(wc.id), &urr);
		    debug(1, "%s: rdma read okay", __func__);
		    break;
	    }

	    /* verify correct result */
	    if (rbuf[0] != 'r')
		error("%s: rbuf has %02x hoping for %02x", __func__,
		      rbuf[0], 'r');
	} else {
	    /* give him some data */
	    memset(rbuf, 'r', msgsz);
	    /* wait for server rdma read to complete */
	    for (i=0; i<5; i++) {
		debug(2, "%s: polling rbuf", __func__);
		ue.cmd = IWARP_ENCOURAGE;
		ret = write(kiwarp_fd, &ue, sizeof(ue));
		if (ret < 0)
			error_errno("encourage");
		if (ret != sizeof(ue))
			error("encourage short: %d of %d", ret,
			      (int) sizeof(ue));
		usleep(500000);
	    }
	}

	/* destroy stag */
	usd.cmd = IWARP_STAG_DESTROY;
	usd.stag = bufs_stag;
	ret = write(kiwarp_fd, &usd, sizeof(usd));
	if (ret < 0)
		error_errno("mem_destroy_stag");
	if (ret != sizeof(usd))
		error("wrote only %d\n", ret);
	debug(2, "destroyed stag %d", bufs_stag);

	/* deregister memory */
	umd.cmd = IWARP_MEM_DEREG;
	umd.md = bufs_md;
	ret = write(kiwarp_fd, &umd, sizeof(umd));
	if (ret < 0)
		error_errno("mem deregister");
	if (ret != sizeof(umd))
		error("%s: mem deregister short write %d", __func__, ret);
	debug(2, "deregistered mem_region %x", bufs_md);

	free(sbuf);
}

static void test_rdmap_init_startup(int kiwarp_fd, int fd, int msgsz)
{
	int ret = 0;
	struct user_init_startup uis;
	char clntpd[] = "client";
	char srvrpd[] = "server";
	char *rbuf;

	rbuf = Malloc(msgsz+10);
	uis.cmd = IWARP_INIT_STARTUP;
	uis.fd = fd;
	if (!server) {
		uis.is_initiator = !server;
		uis.pd_in = clntpd;
		uis.len_in = sizeof(clntpd);
		uis.pd_out = rbuf;
		uis.len_out = sizeof(srvrpd);
		ret = write(kiwarp_fd, &uis, sizeof(uis));
		if (ret < 0)
			error_errno("init_startup");
		if (ret != sizeof(uis))
			error("wrote only %d of uis (%d)", ret, (int) sizeof(uis));
		debug(2, "remote pd %s", rbuf);
	} else {
	}
	free(rbuf);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s {-r|-c} <peerIP> [-n msgsz [-t test]]\n",
		progname);
	exit(1);
}

int main(int argc, char *argv[])
{
	int kiwarp_fd, fd, ret;
	uint64_t cq;  /* handle to in-kernel CQ structure */
	struct user_cq_create ucc;
	struct user_cq_destroy ucd;
	struct user_register_sock ureg;
	struct user_sock_attrs attrs;
	struct user_deregister_sock uds;
	struct in_addr inaddr;
	struct hostent *hp;
	int msgsz = 1500;
	int test = 1;

	set_progname(argc, argv);
	if (argc < 3) usage();
	if (!strcmp(argv[1], "-r"))
		server = 1;
	else if (!strcmp(argv[1], "-c"))
		server = 0;
	else
		usage();
	hp = gethostbyname(argv[2]);
	if (!hp)
	    error_errno("gethostbyname");
	inaddr = *(struct in_addr *) hp->h_addr_list[0];
	if (argc >= 5 && !strcmp(argv[3], "-n"))
		msgsz = atoi(argv[4]);
	if (argc >= 7 && !strcmp(argv[5], "-t"))
		test = atoi(argv[6]);

	kiwarp_fd = open(kiwarp_dev, O_RDWR);
	if (kiwarp_fd < 0)
		error_errno("open %s", kiwarp_dev);

	/* build and connect a socket */
	fd = build_connection(server, &inaddr);

	/* create a cq */
	ucc.cmd = IWARP_CQ_CREATE;
	ucc.depth = 20;
	ucc.cq_handle = &cq;
	ret = write(kiwarp_fd, &ucc, sizeof(ucc));
	if (ret < 0)
		error_errno("cq create");
	if (ret != sizeof(ucc))
		error("cq create short write: %d of %d", ret, (int) sizeof(ucc));

	/* register it */
	ureg.cmd = IWARP_REGISTER_SOCK;
	ureg.fd = fd;
	ureg.scq_handle = cq;
	ureg.rcq_handle = cq;
	ret = write(kiwarp_fd, &ureg, sizeof(ureg));
	if (ret < 0)
		error_errno("register sock");
	if (ret != sizeof(ureg))
		error("register sock short write: %d of %d", ret,
		      (int) sizeof(ureg));

	/* set sock attrs */
	attrs.cmd = IWARP_SET_SOCK_ATTRS;
	attrs.fd = fd;
	attrs.use_crc = 1;
	attrs.use_mrkr = 0;
	ret = write(kiwarp_fd, &attrs, sizeof(attrs));
	if (ret < 0)
		error_errno("set sock attrs");

	/* run the tests */
	switch (test) {
		case 1:
			test_rdmap_send(kiwarp_fd, fd, cq, cq, msgsz);
			break;
		case 2:
			test_rdmap_rdma_write(kiwarp_fd, fd, cq, cq, msgsz);
			break;
		case 3:
			test_rdmap_rdma_read(kiwarp_fd, fd, cq, cq, msgsz);
			break;
		case 4:
			test_rdmap_init_startup(kiwarp_fd, fd, msgsz);
		default:
			break;
	}

	/* deregister play socket */
	uds.cmd = IWARP_DEREGISTER_SOCK;
	uds.fd = fd;
	ret = write(kiwarp_fd, &uds, sizeof(uds));
	if (ret < 0)
		error_errno("deregister sock");
	if (ret != sizeof(uds))
		error("deregister sock short write: %d of %d", ret,
		      (int) sizeof(ureg));

	/* close play socket */
	ret = close(fd);
	if (ret < 0)
		error_errno("close fd");

	/* destroy cq */
	ucd.cmd = IWARP_CQ_DESTROY;
	ucd.cq_handle = cq;
	ret = write(kiwarp_fd, &ucd, sizeof(ucd));
	if (ret < 0)
		error_errno("cq destroy");
	if (ret != sizeof(ucd))
		error("cq destroy short write: %d of %d", ret,
		      (int) sizeof(ucd));

	ret = close(kiwarp_fd);
	if (ret < 0)
		error_errno("close kiwarp_fd");
	return 0;
}
