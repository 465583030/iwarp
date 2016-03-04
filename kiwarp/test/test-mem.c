/*
 * mem test scaffold
 *
 * $Id: test-mem.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
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

static const char *kiwarp_dev = "/dev/kiwarp";

int main(int argc, char **argv)
{
	int kiwarp_fd, ret;
	struct user_mem_reg umr;
	struct user_mem_dereg umd;
	struct user_stag_create usc;
	struct user_stag_destroy usd;
	void *buf = Malloc(1500);
	unsigned long md;
	int32_t stag;

	set_progname(argc, argv);

	kiwarp_fd = open(kiwarp_dev, O_RDWR);
	if (kiwarp_fd < 0)
		error_errno("open %s", kiwarp_dev);

	/* register memory */
	umr.cmd = IWARP_MEM_REG;
	umr.address = buf;
	umr.len = 1500;
	umr.mem_desc = &md;
	ret = write(kiwarp_fd, &umr, sizeof(umr));
	if (ret < 0)
		error_errno("mem_register");
	if (ret != sizeof(umr))
		error("wrote only %d\n", ret);
	debug(2, "registered mem_region %d", md);

	/* create stag */
	usc.cmd = IWARP_STAG_CREATE;
	usc.md = md;
	usc.start = buf;
	usc.len = 200;
	usc.rw = 1;
	usc.prot_domain = 1;
	usc.stag = &stag;
	ret = write(kiwarp_fd, &usc, sizeof(usc));
	if (ret < 0)
		error_errno("mem_create_stag");
	if (ret != sizeof(usc))
		error("wrote only %d\n", ret);
	debug(2, "created stag %d", stag);

	/* destroy stag */
	usd.cmd = IWARP_STAG_DESTROY;
	usd.stag = stag;
	ret = write(kiwarp_fd, &usd, sizeof(usd));
	if (ret < 0)
		error_errno("mem_destroy_stag");
	if (ret != sizeof(usd))
		error("wrote only %d\n", ret);
	debug(2, "destroyed stag %d", stag);

	/* try to destroy stag again */
	usd.cmd = IWARP_STAG_DESTROY;
	usd.stag = stag;
	ret = write(kiwarp_fd, &usd, sizeof(usd));
	if (ret >= 0)
		error("duplicate destruction of stag succeeded");
	debug(2, "caught duplicate destruction of stag");

	/* deregister memory */
	umd.cmd = IWARP_MEM_DEREG;
	umd.md = md;
	ret = write(kiwarp_fd, &umd, sizeof(umd));
	if (ret < 0)
		error_errno("mem_deregister");
	if (ret != sizeof(umd))
		error("wrote only %d\n", ret);
	debug(2, "deregistered mem_region %d", md);

	/* deregister memory region again */
	umd.cmd = IWARP_MEM_DEREG;
	umd.md = md;
	ret = write(kiwarp_fd, &umd, sizeof(umd));
	if (ret >= 0)
		error("duplicate mem_deregister succeeded");
	debug(2, "caught duplicate mem_deregister");

	ret = close(kiwarp_fd);
	return ret;
}

