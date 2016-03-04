/*
 * rdmap layer test routines
 *
 * $Id: test_rdmap.c 666 2007-08-03 15:12:59Z dennis $
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

typedef enum{
	TEST_WR_PENDING,
	TEST_WR_COMPLETE
}test_wr_status_t;

typedef struct test_untag_wrd{
	cq_wrid_t id;
	buf_t *buf;
}test_untag_wrd_t;

typedef struct test_tag_wrd{
	struct list_head list;
	cq_wrid_t id;
	stag_t stag;
	test_wr_status_t wr_status;
	msg_len_t len;
}test_tag_wrd_t;

static bool_t is_server = FALSE;
static int32_t length = -1;

static void ATTR_NORETURN local_usage(const char *funcname);
static void parse_local_options(int argc, char *argv[]);

static void test_send(socket_t sk, bool_t use_mrkr, bool_t use_crc);
static void test_rdma_write(socket_t sk, bool_t use_mrkr, bool_t use_crc);
static void test_reap_rwr(socket_t sk);
static void test_rdma_read(socket_t sk, bool_t use_mrkr, bool_t use_crc);
static void test_byte_order(socket_t sk, bool_t use_mrkr, bool_t use_crc);

static void ATTR_NORETURN
local_usage(const char *funcname)
{
	fprintf(stderr, "%s: Usage: %s [-s 1] [-l <msg_len>] "
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
				case 's':
					++argv, --argc;
					break;
				default:
					local_usage(__func__);
			}
		} else {
			if (length == -1)
				local_usage(__func__);
		}
	}
	if (length == -1)
		local_usage(__func__);
}

static void
test_send(socket_t sk, bool_t use_mrkr, bool_t use_crc)
{
	uint32_t NUM = length/4;
	buf_t b;
	b.len = NUM*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);

	rdmap_init();
	rdmap_register_sock(sk, NULL, NULL);

	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = use_mrkr;
	iwsk->mpask.use_crc = use_crc;

	if (is_server) {
		uint32_t i =0;
		rdmap_post_recv(sk, b.buf, b.len, 0);
		while (!*(((uint32_t *)b.buf) + (NUM-1))) {
			rdmap_poll();
		}
		for (i=0; i<NUM; i++)
			if (*(((uint32_t *)b.buf) + i) != i)
			    error("%s: wrong byte, got %d, wanted %d",
			      __func__, *(((uint32_t *)b.buf) + i), i);
	} else {
		uint32_t i = 0;
		for (i=0; i<NUM; i++)
			*(((uint32_t *)b.buf) + i) = i;
		rdmap_send(sk, b.buf, b.len, 0);
	}
	rdmap_deregister_sock(sk);
	free(b.buf);
	rdmap_fin();
}

/* tests rdma_write. also tests multiple messages as side effect */
static void
test_rdma_write(socket_t sk, bool_t use_mrkr, bool_t use_crc)
{
	uint32_t NUM = length/4;
	uint32_t i;
	buf_t b;
	b.len = NUM*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);

	rdmap_init();
	rdmap_register_sock(sk, NULL, NULL);

	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = use_mrkr;
	iwsk->mpask.use_crc = use_crc;

	if (is_server) {
		mem_init();
		mem_desc_t md = mem_register(b.buf, b.len);
		stag_t stag = mem_stag_create(sk, md, 0, b.len, STAG_W, 0);
		tag_offset_t to = (tag_offset_t)((uintptr_t)b.buf);
		debug(2, "stag gen (%u)", stag);
		rdmap_send(sk, &stag, sizeof(stag), 0);
		rdmap_send(sk, &to, sizeof(to), 0);
		rdmap_post_recv(sk, b.buf, b.len, 6);
		while (!*(((uint32_t *)b.buf) + (NUM-1)))
			rdmap_poll();
		for (i=0; i<NUM; i++)
			if (*(((uint32_t *)b.buf) + i) != i)
			    error("%s: wrong byte, got %d, wanted %d",
			      __func__, *(((uint32_t *)b.buf) + i), i);
		mem_deregister(md);
		mem_fini();
	} else {
		stag_t stag = 0;
		tag_offset_t to = 0;
		rdmap_post_recv(sk, &stag, sizeof(stag), 9);
		while (!stag)
			rdmap_poll();
		rdmap_post_recv(sk, &to, sizeof(to), 10);
		while (!to)
			rdmap_poll();
		debug(2, "stag recved (%u) to %lx", stag, to);
		for (i=0; i<NUM; i++)
			*(((uint32_t *)b.buf) + i) = i;
		rdmap_rdma_write(sk, stag, to, b.buf, b.len, 0);
	}
	rdmap_deregister_sock(sk);
	free(b.buf);
	rdmap_fin();
}

static void
test_reap_rwr(socket_t sk)
{
	iwsk_t *iwsk;
	rdmap_control_field_t cf;
	test_tag_wrd_t *d = NULL;

	rdmap_init();
	rdmap_register_sock(sk, NULL, NULL);

	if (is_server) {
		iwsk = iwsk_lookup(sk);
		cf = 0;
		rdmap_set_OPCODE(cf, RDMA_READ_RESP);

		/* test with empty q. tested it caught the error */
/*		rdmap_tag_recv(iwsk, cf, 3, 4);*/

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 3;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test with one element */
		rdmap_tag_recv(iwsk, cf, d->stag, d->len);

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 3;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 4;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test with multiple stags with match at start*/
		rdmap_tag_recv(iwsk, cf, 3, 2);

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 6;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test multiple stags no match. currently catches the error */
		/*rdmap_tag_recv(iwsk, cf, 1, 4); *//* currently 4 6; no 1 */

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 3;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test with multiple stags match at end or middle */
		rdmap_tag_recv(iwsk, cf, 6, 8); /* currently 4 6 3 */
		rdmap_tag_recv(iwsk, cf, 3, 8);

		/* test for reap action deq */
		rdmap_tag_recv(iwsk, cf, 4, 4);

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 1;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);
		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 2;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test for reap action blocked */
		rdmap_tag_recv(iwsk, cf, 1, 4);
		/* empty the queue */
		rdmap_tag_recv(iwsk, cf, 2, 4);

		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 3;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);
		d = Malloc(sizeof(test_tag_wrd_t));
		d->id = 2;
		d->stag = 3;
		d->wr_status = TEST_WR_PENDING;
		d->len = 0;
		list_add_tail(&d->list, &iwsk->rdmapsk.rwrq);

		/* test with multiple similar stags & blocked for mis-match */
		rdmap_tag_recv(iwsk, cf, 3, 4);

	}
	rdmap_deregister_sock(sk);
	rdmap_fin();
}

static void
test_rdma_read(socket_t sk, bool_t use_mrkr, bool_t use_crc)
{
	uint32_t NUM = length/4;
	uint32_t i;
	buf_t b;
	b.len = NUM*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);
	cqe_t cqe;
	cq_t *scq, *rcq;

	scq = cq_create(16);
	rcq = cq_create(16);

	mem_init();
	rdmap_init();
	rdmap_register_sock(sk, scq, rcq);
	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = use_mrkr;
	iwsk->mpask.use_crc = use_crc;
	debug(2, "iwsk %p %d", iwsk, iwsk->sk);

	if (is_server) {
		mem_desc_t md = mem_register(b.buf, b.len);
		stag_t stag = mem_stag_create(sk, md, 0, b.len, STAG_R, 0);
		tag_offset_t to = (tag_offset_t)((uintptr_t)b.buf);

		uint32_t ack = 0;
		buf_t buf_ack;
		buf_ack.buf = &ack;
		buf_ack.len = sizeof(uint32_t);

		for (i=0; i<NUM; i++)
			*(((uint32_t *)b.buf) + i) = i;

		rdmap_send(sk, &stag, sizeof(stag_t), 1);
		while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
		debug(2, "wrid %u", cqe.id);

		rdmap_send(sk, &to, sizeof(to), 2);
		while(cq_consume(iwsk->scq, &cqe) == -ENOENT);
		debug(2, "wrid %u", cqe.id);

		rdmap_post_recv(sk, buf_ack.buf, buf_ack.len, 3);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
		debug(2, "ack %u", ack);

		mem_deregister(md);
	} else {
		mem_desc_t md = mem_register(b.buf, b.len);
		stag_t my_stag = mem_stag_create(sk, md, 0, b.len, STAG_W, 0);
		stag_t rem_stag = 0;
		tag_offset_t rem_to = 0;
		tag_offset_t my_to = (tag_offset_t)((uintptr_t)b.buf);

		rdmap_post_recv(sk, &rem_stag, sizeof(rem_stag), 1);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
		debug(2, "stag rcvd (%u)", rem_stag);

		rdmap_post_recv(sk, &rem_to, sizeof(rem_to), 2);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
		debug(2, "stag rcvd (%u)", rem_to);

		rdmap_rdma_read(sk, my_stag, my_to, b.len, rem_stag, rem_to, 2);
		while (cq_consume(iwsk->scq, &cqe))
			rdmap_poll();
		debug(2, "cqe %u %u", cqe.id, cqe.msg_len);
		uint32_t ack = cqe.msg_len;

		rdmap_send(sk, &ack, sizeof(ack), 3);
		while(cq_consume(iwsk->scq, &cqe) == -ENOENT)
			rdmap_poll();

/*		for (i=0; i<NUM; i++)*/
/*			printf("%u\n", *(((uint32_t *)b.buf) + i));*/

		mem_deregister(md);
	}

	cq_destroy(scq);
	cq_destroy(rcq);
	rdmap_deregister_sock(sk);
	free(b.buf);
	rdmap_fin();
	mem_fini();
}

/* use ethereal in conjunction with this test. */
static void
test_byte_order(socket_t sk, bool_t use_mrkr, bool_t use_crc)
{
	buf_t b;
	b.len = 4*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);
	cqe_t cqe;
	cq_t *scq, *rcq;

	scq = cq_create(16);
	rcq = cq_create(16);
	rdmap_init();
	rdmap_register_sock(sk, scq, rcq);

	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = use_mrkr;
	iwsk->mpask.use_crc = use_crc;

	if (is_server) {
		rdmap_post_recv(sk, b.buf, b.len, 0);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
	} else {
		uint32_t off = 0;
		uint8_t *cp = b.buf;
		*(stag_t *)(cp + off) = 0x01798a00; off += 4;
		*(tag_offset_t *)(cp + off) = 0x000000000804e008; off += 8;
		*(msg_len_t *)(cp + off) = 1024; off += 4;
		rdmap_send(sk, b.buf, b.len, 0);
	}

	cq_destroy(scq);
	cq_destroy(rcq);
	rdmap_deregister_sock(sk);
	free(b.buf);
	rdmap_fin();
}

int
main(int argc, char *argv[])
{
	parse_options(argc, argv);
	parse_local_options(argc, argv);
	is_server = get_isserver();
	socket_t sk = init_connection(is_server);
	test_send(sk, TRUE, TRUE);
	test_send(sk, FALSE, TRUE);
	test_rdma_write(sk, TRUE, TRUE);
	test_rdma_write(sk, FALSE, TRUE);
	test_reap_rwr(sk);
	test_rdma_read(sk, TRUE, TRUE);
	test_rdma_read(sk, FALSE, TRUE);
	test_byte_order(sk, TRUE, TRUE);
	test_byte_order(sk, FALSE, TRUE);
	close(sk);
	return 0;
}
