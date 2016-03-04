/*
 * memory management
 *
 * $Id: mem.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */
#include <linux/mm.h>
#include <linux/highmem.h>
#include "mem.h"
#include "util.h"

static const uint32_t DELTA = 10;

static mem_region_t *mr_from_md(mem_desc_t md, mem_manager_t *mm);

static inline mem_region_t *mr_from_md(mem_desc_t md, mem_manager_t *mm)
{
	mem_region_t *mr = (mem_region_t *) md;

	if (mr < &(mm->mem_region[0])
	    || mr >= &(mm->mem_region[mm->num_mem_region]))
		return 0;
	return mr;
}

int mem_initialize(mem_manager_t *mm)
{
	int ret = 0;
	int i = 0;

	if (!mm) {
		ret = -EINVAL;
		goto out;
	}

	mm->num_mem_region = DELTA;
	mm->mem_region = kmalloc(sizeof(*(mm->mem_region)) * DELTA,
				 GFP_KERNEL);
	if(!mm->mem_region) {
		ret = -ENOMEM;
		goto out;
	}
	memset(mm->mem_region, 0, DELTA * sizeof(*(mm->mem_region)));
	for (i = 0; i < DELTA; i++)
		INIT_LIST_HEAD(&(mm->mem_region[i].stag_list));

	mm->stag_ht = kmalloc(sizeof(*(mm->stag_ht)), GFP_KERNEL);
	if (!mm->stag_ht) {
		ret = -ENOMEM;
		goto free_mem_region;
	}
	ret = ht_create(5, mm->stag_ht);
	if (ret)
		goto free_stag_ht; /* failure */

	mm->stag_next_cntr = 1;
	spin_lock_init(&mm->lock);
	goto out; /* return success */

free_stag_ht:
	kfree(mm->stag_ht);
free_mem_region:
	kfree(mm->mem_region);
out:
	return ret;
}

void mem_stag_free(void *x)
{
	kfree(x);
}

int mem_release(mem_manager_t *mm)
{
	int i;
	int ret = 0;
	struct hlist_node *cur, *next;
	ht_node_t *node;
	stag_desc_t *sd;

	if (!mm) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&mm->lock);
	/* loop over stag_descs in stag hash; remove them from
	 * mm->mem_region->stag_list. ht_delete_callback removes them from
	 * stag_ht and releases their memory.
	 */
	for (i = 0; i < mm->stag_ht->sz; i++) {
		hlist_for_each_safe(cur, next, &(mm->stag_ht->htab[i])) {
			node = hlist_entry(cur, ht_node_t, hnod);
			sd = node->val;
			list_del(&(sd->list));
			ht_delete_callback((void *)(unsigned long)sd->stag,
			                   mm->stag_ht, mem_stag_free);
		}
	}

	kfree(mm->stag_ht);
	kfree(mm->mem_region);
	spin_unlock(&mm->lock);

out:
	return ret;
}

/* alloc 4 pages for these things */
#define mem_desc_page_order 2

/*
 * Returns the new memory descriptor or NULL if failure.
 */
mem_desc_t mem_register(void *addr, size_t len, mem_manager_t *mm)
{
	int i, ret, off;
	mem_region_t *mr = NULL;
	struct page *page_list_page;
	unsigned long npages, cur_base;

	iwarp_debug("%s: addr %p len %zu", __func__, addr, len);
	spin_lock(&mm->lock);

	/*
	 * Find an empty slot or allocate a new one.
	 */
	for (i = 0; i < mm->num_mem_region; i++) {
		if (!mm->mem_region[i].valid)
			break;
	}

	if (i == mm->num_mem_region) {
		void *x = mm->mem_region;
		size_t orig_num = mm->num_mem_region;
		size_t new_num = orig_num + DELTA;
		mm->num_mem_region = new_num;
		mm->mem_region = kmalloc(sizeof(*(mm->mem_region)) * new_num,
					 GFP_KERNEL);
		if (!mm->mem_region)
			goto unlock;
		memcpy(mm->mem_region, x,
		       sizeof(*(mm->mem_region)) * orig_num);
		memset(&(mm->mem_region[orig_num]), 0,
		       DELTA * sizeof(*(mm->mem_region)));
		kfree(x);
	}

	mr = &mm->mem_region[i];

	cur_base = (unsigned long) addr & PAGE_MASK;
	npages = PAGE_ALIGN(len + ((unsigned long) addr & ~PAGE_MASK))
	         >> PAGE_SHIFT;

	/* if this happens, need to build a list of chunks of pages */
	if (npages > (1<<mem_desc_page_order) * PAGE_SIZE
	              / sizeof(struct page *)) {
		iwarp_info("%s: too many pages %lu > %lu", __func__, npages,
		           (1<<mem_desc_page_order) * PAGE_SIZE
			   / sizeof(struct page *));
		mr = NULL;
		goto unlock;
	}

	/*
	 * Grab the user pages.
	 */
	page_list_page = alloc_pages(GFP_KERNEL, mem_desc_page_order);
	if (!page_list_page) {
		mr = NULL;
		goto unlock;
	}
	mr->page_list = (struct page **) page_address(page_list_page);

	mr->caddr = kmalloc(npages * sizeof(*mr->caddr), GFP_KERNEL);
	if (!mr->caddr) {
		free_pages((unsigned long) mr->page_list, mem_desc_page_order);
		mr = NULL;
		goto unlock;
	}

	down_write(&current->mm->mmap_sem);

	off = 0;
	while (off < npages) {
		ret = get_user_pages(current, current->mm,
		                     cur_base + off * PAGE_SIZE, npages - off,
				     1, 0, mr->page_list + off, NULL);
		if (ret < 0) {
			for (i=0; i<off; i++)
				put_page(mr->page_list[i]);
			free_pages((unsigned long) mr->page_list,
			           mem_desc_page_order);
			kfree(mr->caddr);
			mr = NULL;
			goto out_sem;
		}
		off += ret;
	}

	/*
	 * Finally mark valid the new mem region and return it.
	 */
	mr->valid = 1;
	mr->start = addr;
	mr->len = len;
	mr->npages = npages;
	INIT_LIST_HEAD(&mr->stag_list);

out_sem:
	up_write(&current->mm->mmap_sem);
unlock:
	spin_unlock(&mm->lock);
	return (mem_desc_t) mr;
}

int mem_deregister(mem_desc_t md, mem_manager_t *mm)
{
	int i, ret = 0;
	mem_region_t *mr = NULL;

	spin_lock(&mm->lock);
	mr = mr_from_md(md, mm);
	if (!mr) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!mr->valid) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!list_empty(&(mr->stag_list))) {
		ret = -EBUSY;
		goto unlock;
	}

	/* release locked pages */
	down_write(&current->mm->mmap_sem);
	for (i=0; i<mr->npages; i++) {
		put_page(mr->page_list[i]);
	}
	up_write(&current->mm->mmap_sem);
	free_pages((unsigned long) mr->page_list, mem_desc_page_order);
	kfree(mr->caddr);

	/* TODO: delete entry by splicing like perl */
	mr->valid = 0;

unlock:
	spin_unlock(&mm->lock);
	return ret;
}

/* start is absolute address, len is the span */
stag_t mem_stag_create(mem_desc_t md, void *start, size_t len, stag_acc_t rw,
		       int prot_domain, mem_manager_t *mm)
{
	int ret = 0;
	stag_desc_t *sd = NULL;
	mem_region_t *mr = NULL;

	iwarp_debug("%s: start %p len %zu", __func__, start, len);
	spin_lock(&mm->lock);
	mr = mr_from_md(md, mm);
	if (!mr || start < mr->start || len > mr->len) {
		ret = -EINVAL;
		goto unlock;
	}

	/* sd is freed by either mem_release or mem_stag_destroy */
	sd = kmalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		ret = -ENOMEM;
		goto unlock;
	}
	memset(sd, 0, sizeof(*sd));

	sd->mr = mr;
	sd->start = start;
	sd->len = len;
	sd->rw = rw;
	sd->protection_domain = prot_domain;

	/* insertion also checks for collision */
	do {
		sd->stag = mm->stag_next_cntr++;
		ret = ht_insert((void *)(unsigned long)sd->stag, sd, mm->stag_ht);
	} while (ret);

	list_add(&(sd->list), &(mr->stag_list));
	ret = sd->stag;

unlock:
	spin_unlock(&mm->lock);
	return ret;
}

int mem_stag_destroy(stag_t stag, mem_manager_t *mm)
{
	int ret = 0;
	stag_desc_t *sd = NULL;

	spin_lock(&mm->lock);
	ret = ht_lookup((void *)(unsigned long)stag, (void **)&sd, mm->stag_ht);
	if (ret)
		goto unlock;

	list_del(&(sd->list));
	ret = ht_delete((void *)(unsigned long)sd->stag, mm->stag_ht);

	kfree(sd);

unlock:
	spin_unlock(&mm->lock);
	return ret;
}

inline int mem_stag_is_enabled(stag_t stag, mem_manager_t *mm)
{
	int ret = 0;
	stag_desc_t *sd = NULL;

	spin_lock(&mm->lock);
	ret = ht_lookup((void *)(unsigned long)stag, (void **)&sd, mm->stag_ht);
	spin_unlock(&mm->lock);

	return ret;
}

/*
 * Verify the given stag exists and is compatible with the requested
 * access mode.  Check the memory range too.  Return the full stag
 * descriptor.
 */
stag_desc_t *mem_stag_desc(stag_t stag, void __user *start, size_t len,
                           stag_acc_t rw, mem_manager_t *mm)
{
	int ret;
	stag_desc_t *sd = NULL;

	spin_lock(&mm->lock);
	ret = ht_lookup((void *)(unsigned long)stag, (void **)&sd, mm->stag_ht);
	spin_unlock(&mm->lock);

	iwarp_debug("%s: stag %d start %p len %zu rw %d returns %d and sd %p",
	            __func__, stag, start, len, rw, ret, sd);
	if (ret)
		return NULL;
	if ((rw & STAG_R) && !(sd->rw & STAG_R))
		return NULL;
	if ((rw & STAG_W) && !(sd->rw & STAG_W))
		return NULL;
	if (start < sd->start || start >= (sd->start + sd->len))
		return NULL;
	if ((start + len) < sd->start || (start + len) > (sd->start + sd->len))
		return NULL;

	return sd;
}

/*
 * Fill iov based on buffer page boundaries.
 *   *numiov_inout: Pass in the number of entries available in the iov array,
 *                  on return, replaced with number filled.
 *   returns 0 on success
 */
int mem_fill_iovec(const void __user *bufv, int payload_len, int offset,
                   struct stag_desc *sd, struct kvec *iov, int num_iov_alloc,
		   int *numiov)
{
	void *caddr;
	unsigned long buf = (unsigned long) bufv + offset;
	const unsigned long ms = (unsigned long) sd->mr->start >> PAGE_SHIFT;

	iwarp_debug("%s: buf %p len %d offset %d sd %p kvec %p", __func__,
	            bufv, payload_len, offset, sd, iov);
	while (payload_len > 0) {
		int page_index = (buf >> PAGE_SHIFT) - ms;
		int page_offset = (buf & ~PAGE_MASK);
		int page_avail = PAGE_SIZE - page_offset;
		int numbytes = payload_len;
		if (numbytes > page_avail)
			numbytes = page_avail;

		caddr = kmap(sd->mr->page_list[page_index]);

		if (*numiov == num_iov_alloc) {
			iwarp_info("%s: iov overflow: >%d needed", __func__,
			           *numiov);
			return -EOVERFLOW;
		}
		iov[*numiov].iov_base = caddr + page_offset;
		iov[*numiov].iov_len = numbytes;
		iwarp_debug("%s: iov %d base %p len %zu", __func__, *numiov,
		            iov[*numiov].iov_base, iov[*numiov].iov_len);
		++*numiov;
		payload_len -= numbytes;
		buf += numbytes;
	}
	return 0;
}

/*
 * Unmap kmap after iovec use.  Similar to fill call.
 */
int mem_unmap_iovec(const void __user *bufv, int payload_len, int offset,
                    struct stag_desc *sd)
{
	unsigned long buf = (unsigned long) bufv + offset;
	const unsigned long ms = (unsigned long) sd->mr->start >> PAGE_SHIFT;

	while (payload_len > 0) {
		int page_index = (buf >> PAGE_SHIFT) - ms;
		int page_offset = (buf & ~PAGE_MASK);
		int page_avail = PAGE_SIZE - page_offset;
		int numbytes = payload_len;
		if (numbytes > page_avail)
			numbytes = page_avail;

		kunmap(sd->mr->page_list[page_index]);

		payload_len -= numbytes;
		buf += numbytes;
	}
	return 0;
}

