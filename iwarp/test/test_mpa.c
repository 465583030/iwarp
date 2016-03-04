/*
 * test for mpa layer functions.
 *
 * $Id: test_mpa.c 666 2007-08-03 15:12:59Z dennis $
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
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "ddp.h"
#include "mpa.h"
#include "common.h"
#include "util.h"
#include "rdmap.h"
#include "test_stub.h"
#include "iwsk.h"
#include "cq.h"

static bool_t is_server = FALSE;

static void test_mpa_init_startup(socket_t sk);

static void print_no(uint32_t d);
static void test_mpa_hdr(void);

static void
test_mpa_init_startup(socket_t sk)
{
	char *remote;
	remote = malloc(20);
	rdmap_init();
	rdmap_register_sock(sk, NULL, NULL);

	iwsk_t *s = iwsk_lookup(sk);

	mpa_init_startup(s, is_server, "client", remote, 20);
	printf("remote private data is %s\n", remote);
	rdmap_deregister_sock(sk);
	rdmap_fin();
}

static void
print_no(uint32_t d)
{
	uint8_t *c = (uint8_t *)&d;
	printf("%x %x %x %x ", *c, *(c + 1), *(c + 2), *(c + 3));
}

static void
test_mpa_hdr(void)
{
	uint32_t a = 0;
	mpa_set_M(a);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_unset_M(a);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_set_M(a);
	mpa_set_C(a);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_unset_C(a);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_set_C(a);
	mpa_set_R(a);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

/*	mpa_unset_R(a);*/
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_set_Res(a);
	mpa_set_Rev(a, 0xda);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_set_PD_Length(a, 0xa5);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	mpa_set_PD_Length(a, 0xdead);
	print_no(a);
	printf("%x %x\n", a, htonl(a));

	a = htonl(a);

	a = ntohl(a);

	printf("M=%x C=%x R=%x Res=%x Rev=%x PDL=%x\n",
		   mpa_get_M(a),
		   mpa_get_C(a),
		   mpa_get_R(a),
		   mpa_get_Res(a),
		   mpa_get_Rev(a),
		   mpa_get_PD_Length(a));

}

int main(int argc, char *argv[])
{
	parse_options(argc, argv);
	is_server = get_isserver();
	socket_t sk = init_connection(is_server);
	test_mpa_init_startup(sk);
	test_mpa_hdr();
	close(sk);
	return 0;
}
