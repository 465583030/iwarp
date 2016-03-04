/*
 * Pretend to be iWarpRTT to talk to an Ammasso card.
 *
 * Run either as:
 *
 * iWarpRTT -r 10.0.0.4
 * iWarpRTTfake -c 10.0.0.4
 *
 * or
 *
 * iWarpRTTfake -r 10.0.0.5
 * iWarpRTT -c 10.0.0.5
 *
 * $Id: iWarpRTTfake.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "crc32c.h"
#include "util.h"

static int iters = 10;
static int bufsize = 1024;

static uint32_t remote_stag;
static uint64_t remote_to;
static uint32_t remote_len;
static uint32_t local_stag;
static uint64_t local_to;
static uint32_t local_len;

static void read_stag_to_len(int s);
static void write_stag_to_len(int s);
static void read_ping(int s, int c);
static void send_ping(int s, int c);

static void usage(void)
{
    fprintf(stderr, "Usage: %s {-r|-c} <peerIP>\n", __func__);
    exit(1);
}

int main(int argc, char *argv[])
{
    int s, ret, i, off;
    struct in_addr inaddr;
    struct sockaddr_in sin;
    char buf[2048];
    char want[2048];
    int server = 0;

    set_progname(argc, argv);
    if (argc != 3) usage();
    if (!strcmp(argv[1], "-r"))
	server = 1;
    else if (!strcmp(argv[1], "-c"))
	server = 0;
    else
	usage();
    ret = inet_aton(argv[2], &inaddr);
    if (ret == 0)
	error_errno("inet_aton failed");

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
	error_errno("socket");
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(7998);

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

	/* read client hello string */
	sprintf(want, "MPA ID Req Frame");
	off = strlen(want);
	*(uint16_t *)(want+off) = htons(0x4000); off += 2;
	*(uint16_t *)(want+off) = htons(7); off += 2;
	sprintf(want+off, "client"); off += 7;  /* incl null */
	ret = read(s, buf, off);
	if (ret < 0)
	    error_errno("read");
	if (ret != off)
	    error("short read: %d of %d", ret, off);
	if (memcmp(buf, want, off) != 0)
	    error("read client hello message, not match");

	/* write server hello string */
	sprintf(buf, "MPA ID Rep Frame");
	off = strlen(buf);
	*(uint16_t *)(buf+off) = htons(0x4000); off += 2;
	*(uint16_t *)(buf+off) = htons(7); off += 2;
	sprintf(buf+off, "server"); off += 7;  /* incl null */
	ret = write(s, buf, off);
	if (ret < 0)
	    error_errno("write");
	if (ret != off)
	    error("short write: %d of %d", ret, off);

	write_stag_to_len(s);
	read_stag_to_len(s);

	for (i=0; i<iters; i++) {
	    send_ping(s, 'R');
	    read_ping(s, 'C');
	}

    } else {
	/* client */
	sin.sin_addr.s_addr = inaddr.s_addr;
	ret = connect(s, (struct sockaddr *) &sin, sizeof(sin));
	if (ret < 0)
	    error_errno("connect");
	sleep(1);

	/* write client hello string */
	sprintf(buf, "MPA ID Req Frame");
	off = strlen(buf);
	*(uint16_t *)(buf+off) = htons(0x4000); off += 2;
	*(uint16_t *)(buf+off) = htons(7); off += 2;
	sprintf(buf+off, "client"); off += 7;  /* incl null */
	ret = write(s, buf, off);
	if (ret < 0)
	    error_errno("write");
	if (ret != off)
	    error("short write: %d of %d", ret, off);

	/* read server hello string */
	sprintf(want, "MPA ID Rep Frame");
	off = strlen(want);
	*(uint16_t *)(want+off) = htons(0x4000); off += 2;
	*(uint16_t *)(want+off) = htons(7); off += 2;
	sprintf(want+off, "server"); off += 7;  /* incl null */
	ret = read(s, buf, off);
	if (ret < 0)
	    error_errno("read");
	if (ret != off)
	    error("short read: %d of %d", ret, off);
	if (memcmp(buf, want, off) != 0)
	    error("read server hello message, not match");

	read_stag_to_len(s);
	write_stag_to_len(s);

	for (i=0; i<iters; i++) {
	    read_ping(s, 'R');
	    send_ping(s, 'C');
	}
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
    int len, ddp_len, mpa_len, off, ret;
    uint32_t crc, val;

    len = 4+8+4;
    ddp_len = 18 + len;
    mpa_len = 6 + ddp_len;
    ret = read(s, buf, mpa_len);
    if (ret < 0)
	error_errno("read stag/to/len");
    if (ret != mpa_len)
	error("short read stag/to/len: %d of %d", ret, mpa_len);
    off = 0;
    val = ntohs(*(uint16_t *)(buf+off)); off += 2;
    if ((int) val != ddp_len)
	error("ddp_len is %d, expected %d", val, ddp_len);
    val = ntohs(*(uint16_t *)(buf+off)); off += 2;
    if (val != 0x4143)
	error("ddp control is 0x%04x, expected 0x%04x", val, 0x4143);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (val != 0)
	error("ddp empty stag is 0x%08x, expected 0x%08x", val, 0);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (val != 0)
	error("ddp queue is 0x%08x, expected 0x%08x", val, 0);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (val != 1)
	error("ddp msn is 0x%08x, expected 0x%08x", val, 1);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (val != 0)
	error("ddp mo is 0x%08x, expected 0x%08x", val, 0);
    remote_stag = *(uint32_t *)(buf+off); off += 4;
    remote_to = *(uint64_t *)(buf+off); off += 8;
    remote_len = *(uint32_t *)(buf+off); off += 4;
    if ((off%4) != 0)
	error("received off = %d, need padding", off);
    crc = crc32c(buf, off);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (crc != val)
	error("mpa crc is 0x%08x, expected 0x%08x", val, crc);
    if (off != mpa_len)
	error("looked at only %d of %d bytes read", off, mpa_len);
    printf("got stag %x to %Lx len %d\n", remote_stag, Lu(remote_to),
            remote_len);
}

static void
write_stag_to_len(int s)
{
    char buf[2048];
    int len, ddp_len, off, ret;
    uint32_t crc;

    len = 4 + 8 + 4;
    ddp_len = 18 + len;
    local_stag = 0x01798a00;
    local_to = 0x000000000804e008;
    local_len = 1024;
    off = 0;
    *(uint16_t *)(buf+off) = htons(ddp_len); off += 2;  /* mpa ulpdu_len */
    *(uint16_t *)(buf+off) = htons(0x4143); off += 2;  /* ddp send w/last */
    *(uint32_t *)(buf+off) = 0; off += 4;  /* ddp stag field, no stag */
    *(uint32_t *)(buf+off) = 0; off += 4;  /* ddp queue */
    *(uint32_t *)(buf+off) = htonl(1); off += 4;  /* ddp msn */
    *(uint32_t *)(buf+off) = 0; off += 4;  /* ddp mo */
    *(uint32_t *)(buf+off) = local_stag; off += 4;
    *(uint64_t *)(buf+off) = local_to; off += 8;
    *(uint32_t *)(buf+off) = local_len; off += 4;
    if ((off%4) != 0)
	error("send off = %d, need padding", off);
    crc = crc32c(buf, off);
    *(uint32_t *)(buf+off) = htonl(crc); off += 4;  /* mpa crc */
    ret = write(s, buf, off);
    if (ret < 0)
	error_errno("write stag/to/len");
    if (ret != off)
	error("short write stag/to/len: %d of %d", ret, off);
    printf("sent stag %x to %Lx len %d\n", local_stag, Lu(local_to), local_len);
}

static void
read_ping(int s, int c)
{
    char buf[2048];
    int len, ddp_len, off, ret, i;
    uint32_t val, crc;
    uint64_t qval;

    ddp_len = bufsize + 14;
    len = ddp_len + 6;
    ret = read(s, buf, len);
    if (ret < 0)
	error_errno("%s: read", __func__);
    if (ret != len)
	error("%s: short read: %d of %d", __func__, ret, len);
    off = 0;
    val = ntohs(*(uint16_t *)(buf+off)); off += 2;
    if ((int) val != ddp_len)
	error("len is %d, expected %d", val, ddp_len);
    val = ntohs(*(uint16_t *)(buf+off)); off += 2;
    if (val != 0xc140)
	error("ddp control is 0x%04x, expected 0x%04x", val, 0xc140);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (val != local_stag)
	error("ddp rdma stag is 0x%08x, expected 0x%08x", val, local_stag);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    qval = val; qval <<= 32;
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    qval |= val;
    if (qval != local_to)
	error("ddp rdma to is 0x%016Lx, expected 0x%016Lx", Lu(qval),
	      Lu(local_to));
    for (i=0; i<bufsize; i++)
	if (buf[off+i] != c)
	    error("payload expected 0x%02x at payload pos %d, got 0x%02x",
	      c, i, buf[off+i]);
    off += bufsize;
    crc = crc32c(buf, off);
    val = ntohl(*(uint32_t *)(buf+off)); off += 4;
    if (crc != val)
	error("mpa crc is 0x%08x, expected 0x%08x", val, crc);
    if (off != len)
	error("looked at only %d of %d bytes read", off, len);
    /* printf("%s: okay\n", __func__); */
}

static void
send_ping(int s, int c)
{
    char buf[2048];
    int len, ddp_len, off, ret, i;
    uint32_t crc;

    ddp_len = bufsize + 14;
    len = ddp_len + 6;
    off = 0;
    *(uint16_t *)(buf+off) = htons(ddp_len); off += 2;
    *(uint16_t *)(buf+off) = htons(0xc140); off += 2;
    *(uint32_t *)(buf+off) = htonl(remote_stag); off += 4;
    *(uint32_t *)(buf+off) = 0; off += 4;  /* hi bits remote_to */
    *(uint32_t *)(buf+off) = htonl((uint32_t)(remote_to & 0xffffffff)); off +=4;
    for (i=0; i<bufsize; i++)
	buf[off+i] = c;
    off += bufsize;
    crc = crc32c(buf, off);
    *(uint32_t *)(buf+off) = htonl(crc); off += 4;
    if (off != len)
	error("%s: generated only %d of %d bytes", __func__, off, len);
    ret = write(s, buf, len);
    if (ret < 0)
	error_errno("%s: write", __func__);
    if (ret != len)
	error("%s: short write: %d of %d", __func__, ret, len);
    /* printf("%s: okay\n", __func__); */
}

