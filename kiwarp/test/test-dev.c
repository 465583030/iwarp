/*
 * Test basic chardev open/close/operations.
 *
 * Do:  p=7000 ; ( echo -e '\0\033AC\377\377\377\377\0\0\0\0\0\0\0\001\0\0\0\0recvbuf\n\0\0\0\0' | /usr/bin/nc -l $p ) & sleep 1 ; test/test-dev $p
 *
 * $Id: test-dev.c 644 2005-11-21 15:42:20Z pw $
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
#include <sys/socket.h>
#include <netinet/in.h>
#include "util.h"
#include "kiwarp/user.h"

#define uint64_from_ptr(p) ((uint64_t)(uintptr_t)(p))

static const char *kiwarp_dev = "/dev/kiwarp";

int main(int argc, char *argv[])
{
    int kiwarp_fd, fd, ret;
    int port;
    uint64_t cq;  /* handle to in-kernel CQ structure */
    struct sockaddr_in sin;
    struct user_cq_create ucc;
    struct user_cq_destroy ucd;
    struct user_register_sock ureg;
    struct user_deregister_sock uds;
    struct user_post_recv upr;
    struct user_send us;
    struct user_poll upoll;
    struct user_mem_reg umr;
    struct user_stag_create usc;
    struct work_completion wc;
    struct {
	char sbuf[60];
	char rbuf[60];
    } bufs;
    unsigned long bufs_md;
    int32_t bufs_stag;

    set_progname(argc, argv);

    if (argc == 1)
	port = 7000;
    if (argc == 2)
	port = atoi(argv[1]);
    else {
	fprintf(stderr, "Usage: %s [port]\n", progname);
	return 1;
    }

    kiwarp_fd = open(kiwarp_dev, O_RDWR);
    if (kiwarp_fd < 0)
	error_errno("open %s", kiwarp_dev);

    /* build and connect a socket */
    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0)
	error_errno("socket");
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ret = connect(fd, (struct sockaddr *) &sin, sizeof(sin));
    if (ret < 0)
	error_errno("connect");

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
	error("register sock short write: %d of %d", ret, (int) sizeof(ureg));

    /* register memory for the bufs */
    umr.cmd = IWARP_MEM_REG;
    umr.address = &bufs;
    umr.len = sizeof(bufs);
    umr.mem_desc = &bufs_md;
    ret = write(kiwarp_fd, &umr, sizeof(umr));
    if (ret < 0)
	error_errno("stag create");
    if (ret != sizeof(umr))
	error("stag create short write: %d of %d", ret, (int) sizeof(umr));

    /* get an stag for both bufs */
    usc.cmd = IWARP_STAG_CREATE;
    usc.md = bufs_md;
    usc.start = &bufs;
    usc.len = sizeof(bufs);
    usc.rw = 0x3;
    usc.prot_domain = 0;
    usc.stag = &bufs_stag;
    ret = write(kiwarp_fd, &usc, sizeof(usc));
    if (ret < 0)
	error_errno("stag create");
    if (ret != sizeof(usc))
	error("stag create short write: %d of %d", ret, (int) sizeof(usc));

    /* post a send operation */
    strcpy(bufs.sbuf, "<send buffer>\n");
    us.cmd = IWARP_SEND;
    us.fd = fd;
    us.id = uint64_from_ptr(&us);
    us.buf = bufs.sbuf;
    us.len = strlen(bufs.sbuf)+1;
    us.local_stag = bufs_stag;
    ret = write(kiwarp_fd, &us, sizeof(us));
    if (ret < 0)
	error_errno("post send");
    if (ret != sizeof(us))
	error("post send short write: %d of %d", ret, (int) sizeof(us));

    /* post a recv operation */
    strcpy(bufs.rbuf, "**X**\n");
    upr.cmd = IWARP_POST_RECV;
    upr.fd = fd;
    upr.id = uint64_from_ptr(&upr);
    upr.buf = bufs.rbuf;
    upr.len = sizeof(bufs.rbuf);
    ret = write(kiwarp_fd, &upr, sizeof(upr));
    if (ret < 0)
	error_errno("post recv");
    if (ret != sizeof(upr))
	error("post recv short write: %d of %d", ret, (int) sizeof(upr));

    /* poll for send completion */
    upoll.cmd = IWARP_POLL;
    upoll.cq_handle = cq;
    upoll.wc = &wc;
    ret = write(kiwarp_fd, &upoll, sizeof(upoll));
    if (ret < 0)
	error_errno("poll send");
    if (ret != sizeof(upoll))
	error("poll send short write: %d of %d", ret, (int) sizeof(upoll));
    if (wc.id != uint64_from_ptr(&us))
	error("poll send returned wc.id %Lu, expecting %p\n", (unsigned long long) wc.id, &us);
    printf("send okay\n");

    /* poll for recv completion, waits for packet arrival */
    for (;;) {
	upoll.cmd = IWARP_POLL;
	upoll.cq_handle = cq;
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
	    error("poll recv short write: %d of %d", ret, (int) sizeof(upoll));
	break;
    }
    if (wc.id != uint64_from_ptr(&upr))
	error("poll recv returned wc.id %Lu, expecting %p\n", (unsigned long long) wc.id, &upr);
    if (strncmp(bufs.rbuf, "recvbuf", 7) != 0)
	error("poll recv okay but rbuf \"%s\" should be \"recvbuf\"", bufs.rbuf);
    printf("recv okay\n");

    /* deregister play socket */
    uds.cmd = IWARP_DEREGISTER_SOCK;
    uds.fd = fd;
    ret = write(kiwarp_fd, &uds, sizeof(uds));
    if (ret < 0)
	error_errno("deregister sock");
    if (ret != sizeof(uds))
	error("deregister sock short write: %d of %d", ret, (int) sizeof(uds));

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
	error("cq destroy short write: %d of %d", ret, (int) sizeof(ucd));

    ret = close(kiwarp_fd);
    if (ret < 0)
	error_errno("close kiwarp_fd");
    return 0;
}
