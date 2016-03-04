#
# kernel mod makefile
#
# $Id: Makefile 671 2007-09-28 15:04:21Z dennis $
#
# Copyright (C) 2005 OSC iWarp Team
# Distributed under the GNU Public License Version 2 or later (See
# LICENSE)
#


CC = gcc


KIWARP_KMOD_SRC := init.c crc32c.c util.c rdmap.c ht.c cq.c ddp.c mpa.c \
                   mem.c iwsk.c

ifneq ($(KERNELRELEASE),)


#EXTRA_CFLAGS := -Winline -DIWARP_DEBUG
EXTRA_CFLAGS := -Winline

kiwarp-y := $(KIWARP_KMOD_SRC:.c=.o)

obj-m += kiwarp.o

else

# verbs library
VERB_SRC := $(addprefix ../verbs/,rnic.c pd.c qp.c cq.c swr.c rwr.c protected.c stubs.c\
			       errno.c openfab.c)
VERB_OBJ := $(VERB_SRC:.c=.ko)
VERB_LIB := ../verbs/libkverbs.a
VERB_INC := $(addprefix ../verbs/,verbs.h types.h limits.h perfmon.h errno.h stubs.h)

UTILO := ../iwarp/util.o

VERB_TEST_SRC_NAMES = verbsTest.c untaggedRTT.c tagged_w_RTT.c uni-spray-bw.c uni-spray-bw-openib-sw.c
VERB_TEST_SRC = $(addprefix ../verbs/Benchmarks/,$(VERB_TEST_SRC_NAMES))
VERB_TEST_OBJ = $(VERB_TEST_SRC:.c=.ko)
VERB_TEST_EXE = $(addprefix ../verbs/Benchmarks/k,$(VERB_TEST_SRC_NAMES:.c=))

AMS_INC := -I/usr/opt/ammasso/src/host/common/include/clustercore \
           -I/usr/opt/ammasso/src/common/include
AMS_LIB_DIR := /usr/opt/ammasso/lib
AMS_EXISTS  := $(shell test -f $(AMS_LIB_DIR)/libccil.so && echo yes)
VERB_TEST_SRC_AMS = $(addprefix ../verbs/Benchmarks/,uni-spray-bw-ams.c)
VERB_TEST_OBJ_AMS = $(VERB_TEST_SRC_AMS:.c=.o)
VERB_TEST_EXE_AMS = $(VERB_TEST_SRC_AMS:.c=)


# test programs, one .c per test
TEST_SRC := $(addprefix test/,test-dev.c test-rdmap.c test-mem.c)
TEST_OBJ := $(TEST_SRC:.c=.o)
TEST_EXE := $(TEST_SRC:.c=)

ALLSRC := $(TEST_SRC) $(KIWARP_KMOD_SRC)
HDRS   := crc32c.h priv.h rdmap.h user.h util.h ht.h cq.h ddp.h mpa.h iwsk.h \
          mem.h

user := $(shell whoami)



CPP_M = -MM
LD = $(CC)
OPT = -O3
LDFLAGS =
CWARN = -Wall -W -Wpointer-arith -Wwrite-strings -Wcast-align -Wcast-qual \
		-Wbad-function-cast -Wundef -Wmissing-prototypes \
		-Wmissing-declarations -Wnested-externs -Winline
CFLAGS := $(OPT) $(CWARN) -I.. -I../iwarp -DKERNEL_IWARP
VERSION := $(shell date +%Y%m%d)

.SUFFIXES:
.SUFFIXES: .c .o .i



#prob a better way to do this
ifeq ($(user),dennis)
#~ KDIR := /home/dennis/uml/linux-2.6.11
KDIR := /home/dennis/Projects/softwareIwarp/Kernel/2.6.11.12
#Need to do ln -s . arch inside asm directory
#~ archarg := ARCH=um
archarg := ARCH=x86_64
else
    ifeq ($(user),pw)
	KDIR := /lib/modules/$(shell uname -r)/build
    else
	#ananth
	KDIR := /usr/src/linux-uml
	archarg := ARCH=um
    endif
endif

.PHONY: all
all: kiwarp.ko $(TEST_EXE) $(VERB_LIB) $(VERB_TEST_EXE)

.PHONY: FORCE
kiwarp.ko: FORCE
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd) $(archarg) modules

clean::
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd) $(archarg) $@

.PHONY: dev
dev:
	d=$$(sed -n '/ kiwarp$$/{; s/ .*//; p; }' /proc/devices) ;\
	[[ -n $$d ]] || exit 1 ;\
	rm -f /dev/kiwarp ;\
	mknod -m 0666 /dev/kiwarp c $$d 0

.PHONY: remod
remod:
	-rmmod kiwarp
	+insmod kiwarp.ko && $(MAKE) --no-print-directory dev

$(VERB_LIB): $(VERB_OBJ)
	ar cr $@ $^

$(TEST_EXE): %: %.o $(UTILO)
	$(LD) $(LDFLAGS) -o $@ $@.o $(UTILO)

kless = $(subst /k,/,$(1))

$(VERB_TEST_EXE): %: $(VERB_TEST_OBJ) $(VERB_LIB) $(UTILO)
	$(LD) $(LDFLAGS) -o $@ $(call kless,$@).ko $(VERB_LIB) $(UTILO) -lm

$(KIWARP_KMOD_SRC:.c=.o): %.o: %.c FORCE
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd) $(archarg) $(shell pwd)/$@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.ko: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
	$(CC) $(CFLAGS) -E $< > $@

clean::
	rm -f $(TEST_OBJ) $(TEST_EXE)
	rm -f $(VERB_OBJ) $(VERB_LIB) $(VERB_TEST_OBJ) $(VERB_TEST_EXE)

ctags := $(shell ctags=exuberant-ctags ;\
		 hash $$ctags 2>/dev/null || ctags=ctags ;\
		 echo $$ctags)
tags: $(ALLSRC) $(HDRS)
	$(ctags) $(ALLSRC) $(HDRS) ../verbs/*.[ch]

endif
