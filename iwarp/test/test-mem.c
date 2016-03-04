/*
 * Test memory handling.
 *
 * Try:
 *   valgrind --tool=memcheck --leak-check=yes --show-reachable=yes ./test-mem
 *
 * $Id: test-mem.c 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#include <errno.h>
#include "mem.h"
#include "util.h"

int main(int argc, char *argv[])
{
    void *x;
    size_t len;
    mem_desc_t md;
    stag_t stag, stag1, stag2;
    int ret;

    set_progname(argc, argv);
    mem_init();

    len = 27;
    x = Malloc(len);
    md = mem_register(x, len);

    stag = mem_stag_create(0, md, 0, len, STAG_R,0);
    if (stag < 0)
	error_ret(stag, "%s: full stag create failed", __func__);
    if (!mem_stag_is_enabled(stag))
	error("%s: mem_stag_create: stag %d not enabled", __func__, stag);
    stag1 = stag;

    stag = mem_stag_create(0, md, 3, 5, STAG_R,0);
    if (stag < 0)
	error_ret(stag, "%s: partial stag create failed", __func__);
    if (!mem_stag_is_enabled(stag))
	error("%s: mem_stag_create: stag %d not enabled", __func__, stag);
    stag2 = stag;

    stag = mem_stag_create(0, md, 0, len+1, STAG_R,0);
    if (stag >= 0)
	error("%s: mem_stag_create: too-big worked: %d", __func__, stag);
    if (stag != -EINVAL)
	error_ret(stag, "%s: mem_stag_create: too-big unexpected error",
	  __func__);

    ret = mem_deregister(md);
    if (ret == 0)
	error("%s: mem_deregister worked", __func__);
    if (ret != -EBUSY)
	error_ret(ret, "%s: mem_deregister unexpected error", __func__);

#if 1
    ret = mem_stag_destroy(stag1);
    if (ret < 0)
	error_ret(ret, "%s: mem_stag_destroy %d", __func__, stag1);
    ret = mem_stag_destroy(stag2);
    if (ret < 0)
	error_ret(ret, "%s: mem_stag_destroy %d", __func__, stag2);

    ret = mem_deregister(md);
    if (ret < 0)
	error_ret(ret, "%s: mem_deregister", __func__);
    free(x);
#endif

    mem_fini();
    return 0;
}
