#
# Top level makefile
#
# Copyright (C) 2005 OSC iWarp Team
# Distributed under the GNU Public License Version 2 or later (See LICENSE)
#
.PHONY: all clean FORCE
all clean:
	make -C iwarp $@
	#comment out the following line for building on 32 bit machines
	make -C kiwarp $@

FORCE:;
tags: FORCE
	ctags $(shell find . -name '*.[ch]')

iwarp_test_files := \
    test-mem.c test_ddp.c test_hash.c iWarpRTTfake.c \
    test-crc32c.c test_hdrs.c test_rdmap.c \
    test_mpa.c test_talk_ams.c test_conn_reset.c \
    test_msg.c kernel_assist.c test_stub.[ch]

iwarp_files := \
    Makefile \
    mem.c util.c avl.c ddp.c mpa.c rdmap.c cq.c ht.c iwsk.c crc32c.c \
    mem.h util.h avl.h ddp.h common.h rdmap.h mpa.h cq.h ht.h iwsk.h \
    crc32c.h list.h \
    $(addprefix test/,$(iwarp_test_files))

kiwarp_test_files := \
    test-dev.c test-rdmap.c test-mem.c

kiwarp_files := \
    Makefile \
    init.c crc32c.c util.c rdmap.c ht.c cq.c ddp.c mpa.c mem.c iwsk.c \
    crc32c.h priv.h rdmap.h user.h util.h ht.h cq.h ddp.h mpa.h iwsk.h mem.h \
    $(addprefix test/,$(kiwarp_test_files))

verbs_benchmarks_files := \
    verbsTest.c untaggedRTT.c tagged_w_RTT.c uni-spray-bw.c uni-spray-bw-ams.c uni-spray-bw-openib-sw.c

verbs_files := \
    rnic.c pd.c qp.c cq.c swr.c rwr.c protected.c stubs.c stubs.h \
    errno.c errno.h verbs.h types.h limits.h perfmon.h openfab.c openfab.h\
    $(addprefix Benchmarks/,$(verbs_benchmarks_files))

ib_files := \
	sa.h verbs.h

rdma_files := \
	rdma_cma.h

files := \
    README \
    LICENSE \
    Makefile \
    $(addprefix iwarp/,$(iwarp_files)) \
    $(addprefix kiwarp/,$(kiwarp_files)) \
    $(addprefix verbs/,$(verbs_files)) \
    $(addprefix verbs/openfab/infiniband/,$(ib_files)) \
    $(addprefix verbs/openfab/rdma/,$(rdma_files))


package := iwarp
version := 1.2
dir := $(package)-$(version)
tarball := $(dir).tar.gz

dist: FORCE
	rm -rf $(dir) $(tarball)
	mkdir $(dir)
	cp -al --parents $(files) $(dir)
	tar cf - $(dir) | gzip -9c > $(tarball)
	rm -rf $(dir)

