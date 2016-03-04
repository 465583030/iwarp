/*
 * rdmap layer test routines
 *
 * $Id: test_conn_reset.c 666 2007-08-03 15:12:59Z dennis $
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

static void test_conn_reset(socket_t sk);

static void
test_conn_reset(socket_t sk)
{
	buf_t b;
	uint32_t NUM = 1024;
	b.len = NUM*sizeof(uint32_t);
	b.buf = Malloc(b.len);
	memset(b.buf, 0, b.len);
	cqe_t cqe;
	cq_t *scq, *rcq;

	scq = cq_create(16);
	rcq = cq_create(16);
	rdmap_init();
	rdmap_register_sock(sk, scq, rcq);

	iwsk_t *iwsk = iwsk_lookup(sk);
	iwsk->mpask.use_mrkr = TRUE;
	iwsk->mpask.use_crc = TRUE;

	if (is_server) {
		rdmap_post_recv(sk, b.buf, b.len, 0);
		sleep(3);
		while (cq_consume(iwsk->rcq, &cqe) == -ENOENT)
			rdmap_poll();
	} else {
		uint32_t i = 0;
		for (i=0; i<NUM; i++)
			*(((uint32_t *)b.buf) + i) = i;
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
	is_server = get_isserver();
	socket_t sk = init_connection(is_server);
	test_conn_reset(sk);
	close(sk);
	return 0;
}

