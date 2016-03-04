/*
 * Memory handling header.
 *
 * $Id: mem.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __MEM_H
#define __MEM_H

#include <sys/types.h>
#include <stdint.h>
#include "iwsk.h"

/*
 * Need these to build and destroy internal data structures.
 */
void mem_init(void);
void mem_fini(void);

/*
 * The ULP must register memory before associating with it an STAG.  The STAG
 * is the opaque cookie passed to the sender that enables it to do RDMA Write
 * (or respond to RDMA read).  We do not require that outbound buffers used
 * in sends be registered, however, only memory to be accessed by STAGs in
 * incoming packets.  This may change.
 *
 * Memory registrations may be partially or completely overlapping (not in
 * the spec, just our decision).  Thus we cannot uniquely represent them by
 * just an address and use an opaque handle instead.  Internally this is an
 * entry in a table of existing registrations.
 *
 * We also allow registration independent of STAG creation, as registration
 * is expected to be slow and involve OS and RNIC interaction while STAGs
 * are faster.
 */
typedef uintptr_t mem_desc_t;

mem_desc_t mem_register(void *addr, size_t len);
int mem_deregister(mem_desc_t md);

/*
 * STAGs are tied to a particular stream.  An alternate model would be to use
 * protection domains (PDs) to permit any of a number of streams to use a given
 * STAG.  Both could coexist, but we use only the stream model.  See DDP spec
 * 8.2 and RDMAP spec 5.2.
 */
typedef int32_t stag_t;  /* wire is 32 bits, reserving 1 bit for error */
typedef enum {
    STAG_R =  1,
    STAG_W =  2,
    STAG_RW = STAG_R | STAG_W,
} stag_acc_t;

/*
 * Note that the RDMAP spec implies that the ULP creates the stag.  We're
 * not going to do that.  If the remote side invalidates the STAG, the only
 * way it can find out when it is reenabled is through some app-specific
 * communication.  Might as well send the new STAG then too.
 *
 * start and end are offsets into an existing registered region.  The stag
 * will permit manipulation of the bytes from addr+start to addr+end-1
 * inclusive.
 */
stag_t mem_stag_create(socket_t sk, mem_desc_t md, size_t start, size_t end,
                       stag_acc_t rw, int prot_domain);
int mem_stag_destroy(stag_t stag);
int mem_stag_is_enabled(stag_t stag);

/*
 * Called by DDP to determine if placement is valid for a given tagged
 * message.
 * Returns null if invalid.
 */
void *mem_stag_location(iwsk_t *sk, stag_t stag, size_t off, size_t len,
                        stag_acc_t rw);

/*
 * buffer type. Used specifically in untagged buffer model.
 */
typedef struct {
	void *buf;
	size_t len;
} buf_t;

#endif  /* __MEM_H */
