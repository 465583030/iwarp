#
# Makefile for software iwarp
#
# $Id: Makefile 658 2006-10-19 19:58:05Z dennis $
#
# Copyright (C) 2005 OSC iWarp Team
# Distributed under the GNU Public License Version 2 or later (See LICENSE)
#

# iwarp library
SRC := mem.c util.c avl.c ddp.c mpa.c rdmap.c cq.c ht.c iwsk.c crc32c.c
OBJ := $(SRC:.c=.o)
INC := mem.h util.h avl.h ddp.h common.h rdmap.h mpa.h cq.h ht.h iwsk.h \
       crc32c.h list.h
LIB := libiwarp.a 

# verbs library
VERB_SRC := $(addprefix ../verbs/,rnic.c pd.c qp.c cq.c swr.c rwr.c protected.c stubs.c openfab.c\
			       errno.c)
VERB_OBJ := $(VERB_SRC:.c=.o)
VERB_LIB := ../verbs/libverbs.a
VERB_INC := $(addprefix ../verbs/,verbs.h types.h limits.h perfmon.h errno.h stubs.h)

VERB_TEST_SRC = $(addprefix ../verbs/Benchmarks/,verbsTest.c untaggedRTT.c tagged_w_RTT.c uni-spray-bw.c uni-spray-bw-openib-sw.c)
VERB_TEST_OBJ = $(VERB_TEST_SRC:.c=.o)
VERB_TEST_EXE = $(VERB_TEST_SRC:.c=)

AMS_INC := -I/usr/local/ammasso/src/host/common/include/clustercore \
           -I/usr/local/ammasso/src/common/include
AMS_LIB_DIR := /usr/local/ammasso/lib64
AMS_EXISTS  := $(shell test -f $(AMS_LIB_DIR)/libccil.so && echo yes)
VERB_TEST_SRC_AMS = $(addprefix ../verbs/Benchmarks/,uni-spray-bw-ams.c)
VERB_TEST_OBJ_AMS = $(VERB_TEST_SRC_AMS:.c=.o)
VERB_TEST_EXE_AMS = $(VERB_TEST_SRC_AMS:.c=)

OPENIB_INC := -I/usr/local/openib-iwarp/include
OPENIB_LIB_DIR := /usr/local/openib-iwarp/lib
OPENIB_EXISTS  := $(shell test -f $(OPENIB_LIB_DIR)/libibverbs.so && echo yes)
VERB_TEST_SRC_OPENIB = $(addprefix ../verbs/Benchmarks/,uni-spray-bw-openib.c)
VERB_TEST_OBJ_OPENIB = $(VERB_TEST_SRC_OPENIB:.c=.o)
VERB_TEST_EXE_OPENIB = $(VERB_TEST_SRC_OPENIB:.c=)

# test programs, one .c per test
TEST_SRC := $(addprefix test/,test-mem.c test_ddp.c test_hash.c iWarpRTTfake.c \
						test-crc32c.c test_hdrs.c test_rdmap.c \
						test_mpa.c test_talk_ams.c test_conn_reset.c \
						test_msg.c kernel_assist.c)
TEST_OBJ := $(TEST_SRC:.c=.o)
TEST_EXE := $(TEST_SRC:.c=)
TEST_STUB_SRC := test/test_stub.c
TEST_STUB_OBJ := $(TEST_STUB_SRC:.c=.o)

ALLSRC := $(SRC) $(TEST_SRC) $(TEST_STUB_SRC) $(VERB_SRC) $(VERB_TEST_SRC)
ifneq (,$(AMS_EXISTS))
ALLSRC += $(VERB_TEST_SRC_AMS)
endif
ifneq (,$(OPENIB_EXISTS))
ALLSRC += $(VERB_TEST_SRC_OPENIB)
endif
ALLINC := $(INC) $(VERB_INC) 

OTHER  = Makefile
DIST_FILES := $(sort $(ALLSRC) $(ALLINC) $(OTHER))

CC = gcc
CPP_M = -MM
LD = $(CC)
OPT = -O3
LDFLAGS =
CWARN = -Wall -W -Wpointer-arith -Wwrite-strings -Wcast-align -Wcast-qual \
		-Wbad-function-cast -Wundef -Wmissing-prototypes \
		-Wmissing-declarations -Wnested-externs
CFLAGS := $(OPT) $(CWARN) -I. -I..
VERSION := $(shell date +%Y%m%d)

.SUFFIXES:
.SUFFIXES: .c .o .i

# default target
all:: $(LIB) $(VERB_LIB) $(TEST_EXE) $(VERB_TEST_EXE)

ifneq (,$(AMS_EXISTS))
all:: $(VERB_TEST_EXE_AMS)
endif

ifneq (,$(OPENIB_EXISTS))
all:: $(VERB_TEST_EXE_OPENIB)
endif

$(LIB): $(OBJ)
	ar cr $@ $^

$(VERB_LIB): $(VERB_OBJ)
	ar cr $@ $^ 

$(TEST_EXE): %: %.o $(TEST_STUB_OBJ) $(LIB)
	$(LD) $(LDFLAGS) -o $@ $@.o $(TEST_STUB_OBJ) $(LIB)

$(VERB_TEST_EXE): %: %.o $(VERB_LIB) $(LIB)
	$(LD) $(LDFLAGS) -o $@ $@.o $(VERB_LIB) $(LIB) -lm

$(VERB_TEST_EXE_AMS): %: %.o util.o
	$(LD) $(LDFLAGS) -o $@ $@.o util.o -L$(AMS_LIB_DIR) -lccil -lm

$(VERB_TEST_EXE_OPENIB): %: %.o util.o
	$(LD) $(LDFLAGS) -o $@ $@.o util.o -L$(OPENIB_LIB_DIR) -libverbs -lrdmacm -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(VERB_TEST_OBJ_AMS): %.o: %.c
	$(CC) $(CFLAGS) $(AMS_INC) -c $< -o $@

$(VERB_TEST_OBJ_OPENIB): %.o: %.c
	$(CC) $(CFLAGS) $(OPENIB_INC) -c $< -o $@

%.i: %.c
	$(CC) $(CFLAGS) -E $< > $@

.PHONY: clean distclean
clean::
	rm -f $(LIB) $(OBJ) $(TEST_OBJ) $(TEST_EXE) $(TEST_STUB_OBJ)
	rm -f $(VERB_OBJ) $(VERB_LIB) $(VERB_TEST_OBJ) $(VERB_TEST_EXE)
	rm -f $(VERB_TEST_EXE_AMS) $(VERB_TEST_OBJ_AMS)
	rm -f $(VERB_TEST_EXE_OPENIB) $(VERB_TEST_OBJ_OPENIB)
distclean:: clean
	rm -f tags

tags: $(ALLSRC) $(ALLINC)
	@if [ -f /usr/bin/exuberant-ctags ]; then \
		exuberant-ctags $(ALLSRC) $(ALLINC);\
	elif [ -f /usr/bin/ctags ]; then \
		ctags $(ALLSRC) $(ALLINC);\
	else\
		echo "Specify path of ctags/exuberant-ctags";\
	fi

# depend
ifeq (,$(filter clean distclean dist,$(MAKECMDGOALS)))
-include .depend
endif
all:: .depend
.depend: $(ALLSRC) $(ALLINC) Makefile
	@echo making dependencies
	@$(CC) $(CPP_M) $(CFLAGS) $(SRC) > .depend
	@$(CC) $(CPP_M) $(CFLAGS) $(TEST_SRC) $(TEST_STUB_SRC) |\
	    sed 's/\(.*:\)/test\/\1/' >> .depend
	@$(CC) $(CPP_M) $(CFLAGS) $(VERB_SRC) |\
	    sed 's/\(.*:\)/verbs\/\1/' >> .depend
	@$(CC) $(CPP_M) $(CFLAGS) $(VERB_TEST_SRC) |\
	    sed 's/\(.*:\)/verbs\/Benchmarks\/\1/' >> .depend
ifneq (,$(AMS_EXISTS))
	@$(CC) $(CPP_M) $(CFLAGS) $(AMS_INC) $(VERB_TEST_SRC_AMS) |\
	    sed 's/\(.*:\)/verbs\/Benchmarks\/\1/' >> .depend
endif
ifneq (,$(OPENIB_EXISTS))
	@$(CC) $(CPP_M) $(CFLAGS) $(OPENIB_INC) $(VERB_TEST_SRC_OPENIB) |\
	    sed 's/\(.*:\)/verbs\/Benchmarks\/\1/' >> .depend
endif

distclean::
	rm -f .depend

