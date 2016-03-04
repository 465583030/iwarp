/*
 * test for ddp layer functions.
 *
 * $Id: test_ddp.c 666 2007-08-03 15:12:59Z dennis $
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

static bool_t is_server = FALSE;

static void test_recv(socket_t sk);

static void
test_recv(socket_t sk)
{
	const uint32_t NUM = 1024;
	uint32_t *b = (uint32_t *)Malloc(NUM*sizeof(uint32_t));

	if (is_server) {
		uint32_t i;
		for(i=0; i<10; i++)
			b[i] = i;
		write(sk, b, 20);
	} else {
		int ret;
		ret = read(sk, b, NUM*sizeof(uint32_t));
		printf("%d\n", ret);
	}
}

int
main(int argc, char *argv[])
{
	parse_options(argc, argv);
	is_server = get_isserver();
	socket_t sk = init_connection(is_server);
	test_recv(sk);
	close(sk);
	return 0;
}
