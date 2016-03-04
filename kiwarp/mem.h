/*
 * Memory functions header.
 *
 * $Id: mem.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#ifndef __MEM_H
#define __MEM_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/uio.h>
#include "iwsk.h"
#include "ht.h"

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
 * Memory region descriptor.
 */
typedef struct mem_region {
    void *start;
    size_t len;
    int valid;
    struct list_head stag_list;
    struct page **page_list;
    void **caddr;  /* temp slots for kmapping of page_list */
    int npages;
} mem_region_t;


/*
 * STAG descriptor.
 */
typedef struct stag_desc {
	struct list_head list;  /* linked list hanging off mr */
	mem_region_t *mr;
	void *start;
	size_t len;
	stag_t stag;  /* unique index */
	stag_acc_t rw;
	int protection_domain;
} stag_desc_t;

/*
 * memory manager. Every "heavy-weight context" i.e. process has a user
 * context (priv.h) associated with it which light-weight threads share. Once
 * the device is open, the management of memory used by the process to
 * interact with kiwarp device is independent of other processes, so are the
 * stags that are associated with the process. This struct manages the
 * memory.
 *
 * There can be mutiple stags associated with one memory region. But no stag
 * can point to mulitple memory regions. So stag is the primary way to key in
 * to the memory of interest. This also means that following condition may be
 * true:
 * stag_a's start == stag_b's start && stag_a's len == stag_b's len.
 */

typedef struct mem_manager {
	size_t num_mem_region;
	mem_region_t *mem_region;
	stag_t stag_next_cntr;
	ht_t *stag_ht;
	spinlock_t lock;
} mem_manager_t;

/*
 * buffer type. Used specifically in untagged buffer model.
 */
typedef struct{
	void *buf;
	size_t len;
} buf_t;


/*
 * memory error types. RDMAP and DDP will interpret the error codes according
 * to their semantics
 */
typedef enum {
	MEM_ESTAG=0x0,		/* invalid stag */
	MEM_ESTAGASSOC=0x1,	/* stag stream association error :TODO */
	MEM_EBOUNDS=0x2,	/* base/bounds violation */
	MEM_ACCES=0x3,		/* access violation error */
	MEM_TOWRAP=0x4,		/* tag offset has wrapped */
	MEM_ESTAGDEST=0x5	/* stag cannot be destroyed */
} mem_err_t;

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
typedef unsigned long mem_desc_t;

/*
 * Need these to build and destroy internal data structures.
 */
int mem_initialize(mem_manager_t *mm);

void mem_stag_free(void *x);

int mem_release(mem_manager_t *mm);

mem_desc_t mem_register(void *addr, size_t len, mem_manager_t *mm);

int mem_deregister(mem_desc_t md, mem_manager_t *mm);

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
stag_t mem_stag_create(mem_desc_t md, void *start, size_t len, stag_acc_t rw,
                       int prot_domain, mem_manager_t *mm);

int mem_stag_destroy(stag_t stag, mem_manager_t *mm);

inline int mem_stag_is_enabled(stag_t stag, mem_manager_t *mm);

/*
 * Called by DDP to determine if placement is valid for a given tagged
 * message.
 * Returns null if invalid.
 */
stag_desc_t *mem_stag_desc(stag_t stag, void __user *startv, size_t len,
                           stag_acc_t rw, mem_manager_t *mm);

int mem_fill_iovec(const void __user *buf, int payload_len, int offset,
                   struct stag_desc *sd, struct kvec *iov, int num_iov_alloc,
		   int *numiov);
int mem_unmap_iovec(const void __user *buf, int payload_len, int offset,
                    struct stag_desc *sd);

#endif /* __MEM_H */
