/*
 * Memory handling.  Registration, stags, etc.
 *
 * $Id: mem.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#include <string.h>
#include <errno.h>
#include "mem.h"
#include "util.h"
#include "avl.h"

/* forward declare these typedefs */
typedef struct S_mem_region mem_region_t;
typedef struct S_stag_desc stag_desc_t;

/*
 * STAG descriptor.
 */
struct S_stag_desc {
    stag_desc_t *next;  /* singly linked list hanging off mr */
    mem_region_t *mr;
    size_t start, end;
    stag_t stag;  /* unique index */
    socket_t sk;
    stag_acc_t rw;
    int protection_domain;
};

/* not bothering with unique bitmask, just hoping no one ever has 4e9 in play */
static stag_t stag_next_counter = 1;

/*
 * Memory region descriptor.
 */
struct S_mem_region {
    void *addr;
    size_t len;
    int valid;
    stag_desc_t *stag_list;
};
static mem_region_t *mem_region = 0;
static int num_mem_region = 0;

/*
 * AVL tree for quick stag manipulation.
 */
struct avl_table *stag_avl = 0;

static void *mem_avl_malloc(struct libavl_allocator *x ATTR_UNUSED, size_t len)
{
    return Malloc(len);
}

static void mem_avl_free(struct libavl_allocator *x ATTR_UNUSED, void *buf)
{
    free(buf);
}

static struct libavl_allocator mem_avl_allocator = {
    .libavl_malloc = mem_avl_malloc,
    .libavl_free = mem_avl_free,
};

static int mem_avl_stag_comp(const void *a, const void *b, void *x ATTR_UNUSED)
{
    const stag_desc_t *sa = a;
    const stag_desc_t *sb = b;

    return sa->stag - sb->stag;
}

/*
 * Subsystem startup and shutdown functions.
 */
void mem_init(void)
{
    stag_avl = avl_create(mem_avl_stag_comp, 0, &mem_avl_allocator);
}

void mem_fini(void)
{
    int i;

    /* walk the mrs, destroying stags as we go */
    for (i=0; i<num_mem_region; i++) {
	stag_desc_t *sd, *sdnext;
	mem_region_t *mr = &mem_region[i];
	if (!mr->valid)
	    continue;
	sd = mr->stag_list;
	while (sd) {
	    sdnext = sd->next;
	    mem_stag_destroy(sd->stag);
	    sd = sdnext;
	}
	mr->valid = 0;
    }

    /* detsroy avl tree and mr array */
    avl_destroy(stag_avl, 0);
    free(mem_region);
    mem_region = 0;
    num_mem_region = 0;
}

mem_desc_t mem_register(void *addr, size_t len)
{
    int i;

    /* find a free slot */
    for (i=0; i<num_mem_region; i++)
	if (!mem_region[i].valid)
	    break;

    /* enlarge */
    if (i == num_mem_region) {
	int j;
	void *x = mem_region;
	num_mem_region += 10;
	mem_region = Malloc(num_mem_region * sizeof(*mem_region));
	if (i > 0) {
	    memcpy(mem_region, x, i * sizeof(*mem_region));
	    free(x);
	}
	for (j=i; j<num_mem_region; j++)
	    mem_region[j].valid = 0;
    }

    /* build new entry */
    mem_region[i].addr = addr;
    mem_region[i].len  = len;
    mem_region[i].valid = 1;
    mem_region[i].stag_list = NULL;
    return (mem_desc_t) &mem_region[i];
}

static mem_region_t *mr_from_md(mem_desc_t md)
{
    mem_region_t *mr = (mem_region_t *) md;

    if (mr < &mem_region[0] || mr >= &mem_region[num_mem_region])
	return 0;
    return mr;
}

int mem_deregister(mem_desc_t md)
{
    mem_region_t *mr = mr_from_md(md);

    if (!mr)
	return -EINVAL;

    /* cannot deregister until all stags have been invalidated */
    if (mr->stag_list)
	return -EBUSY;

    mr->valid = 0;
    return 0;
}

/*
 * Start and End are locations WITHIN the memory region that we
 * have already registered
 */
stag_t
mem_stag_create(int sk, mem_desc_t md, size_t start, size_t end, stag_acc_t rw,
		int prot_domain)
{
    stag_desc_t *sd;
    mem_region_t *mr = mr_from_md(md);
    size_t buffer_start;

    if (!mr)
	return -EINVAL;

    if (start >= end)
	return -EINVAL;

    if (end > mr->len)
	return -EINVAL;

    sd = Malloc(sizeof(*sd));
    sd->next = mr->stag_list;
    mr->stag_list = sd;
    sd->mr = mr;

    buffer_start = (size_t)mr->addr;

    sd->start = buffer_start + start;
    sd->end = buffer_start + end;

    sd->stag = stag_next_counter++;
    sd->sk = sk;
    sd->rw = rw;
    sd->protection_domain = prot_domain;
    /* if collision, just get next tag */
    while (avl_insert(stag_avl, sd))
	sd->stag = stag_next_counter++;
    return sd->stag;
}

int mem_stag_destroy(stag_t stag)
{
    stag_desc_t *sd, *si, **siprev;
    stag_desc_t sdtest = { .stag = stag };

    sd = avl_delete(stag_avl, &sdtest);
    if (!sd)
	return -EINVAL;
    /* remove from mr list */
    si = sd->mr->stag_list;
    siprev = &sd->mr->stag_list;
    while (si) {
	if (si == sd) {
	    *siprev = si->next;
	    break;
	}
	siprev = &si->next;
	si = si->next;
    }
    free(sd);
    return 0;
}

/*
 * Since other side can invalidate stags, may need to know if it still
 * exists or not.
 */
int mem_stag_is_enabled(stag_t stag)
{
	stag_desc_t sdtest = { .stag = stag };

	return !!avl_find(stag_avl, &sdtest);
}

/*
 * Called by DDP to determine if placement is valid for a given tagged message.
 * Returns null if invalid.
 */
void *
mem_stag_location(iwsk_t *sk, stag_t stag, size_t off, size_t len,
		  stag_acc_t rw)
{
	stag_desc_t *sd, sdtest = { .stag = stag };

	sd = avl_find(stag_avl, &sdtest);
	if (!sd)
		return NULL;
	if (!sk)
		return NULL;
	if ((rw & STAG_R) && !(sd->rw & STAG_R))
		return NULL;
	if ((rw & STAG_W) && !(sd->rw & STAG_W))
		return NULL;
	/* cannot write byte at end, ranges are start..(end-1) inclusive */
	if (off < sd->start || off >= sd->end)
		return NULL;

	if (off+len < sd->start || off+len > sd->end)
		return NULL;
	return (char*) off;
}
