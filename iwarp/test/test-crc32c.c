/*
 * Test crc32c.c.
 *
 * $Id: test-crc32c.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/uio.h>

#include "util.h"
#include "crc32c.h"

static void test_crc_vec(void);

static void
test_crc_vec(void)
{
	uint32_t crc;
	int i = 0, j = 0, SZ = 3, NUM = 32;
	struct iovec vec[3];
	char buf[3*32];
	for (i=0; i<SZ; i++) {
		vec[i].iov_base = Malloc(NUM);
		vec[i].iov_len = NUM;
		memset(vec[i].iov_base, 0, NUM);
	}
	memset(buf, 0, NUM);
	for (i=0; i<SZ; i++) {
		for (j=0; j<NUM; j++) {
			if (j & 0x1)
				buf[i*NUM + j] = *((char *)vec[i].iov_base + j) = 'A';
			else
				buf[i*NUM + j] = *((char *)vec[i].iov_base + j) = '5';
			debug(10, "buf[%u] = %c vec[%u].iov_base[%u] = %c\n", i*NUM + j,
				  buf[i*NUM + j], i, j, *((char *)vec[i].iov_base + j));
		}
	}
	crc = crc32c(buf, SZ*NUM);
	printf ("crc buf %u %x\n", crc, crc);
	crc = crc32c_vec(vec, SZ);
	printf ("crc vec %u %x\n", crc, crc);
}

int main(int argc, char *argv[])
{
    uint32_t crc, want;
    char buf[2048];
    int i;

    set_progname(argc, argv);
    memset(buf, 0, 2048);
    crc = crc32c(buf, 32);
    want = 0xaa36918a;
    if (crc != want)
	error("crc of zeroes is wrong: %x want %x", crc, want);

    memset(buf, 0xff, 32);
    crc = crc32c(buf, 32);
    want = 0x43aba862;
    if (crc != want)
	error("crc of ones is wrong: %x want %x", crc, want);

    for (i=0; i<32; i++)
	buf[i] = i;
    crc = crc32c(buf, 32);
    want = 0x4e79dd46;
    if (crc != want)
	error("crc of ascending is wrong: %x want %x", crc, want);

    for (i=31; i>=0; i--)
	buf[31-i] = i;
    crc = crc32c(buf, 32);
    want = 0x5cdb3f11;
    if (crc != want)
	error("crc of descending is wrong: %x want %x", crc, want);

	test_crc_vec();

    return 0;
}

