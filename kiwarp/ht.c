/*
 * hash table for sockets
 *
 * $Id: ht.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include "ht.h"
#include "util.h"

#define HT_LOCK(l) \
	do {\
		spin_lock(&(l));\
	} while(0)

#define HT_UNLOCK(l) \
	do {\
		spin_unlock(&(l));\
	} while(0)

/**
 * ht should be allocated before calling ht_create.
 * order: log2(size of hash table)
 */
int ht_create(uint8_t order, ht_t *ht)
{
	int i;

	if (!ht)
		return -EINVAL;

	ht->sz = (1 << order);
	ht->lg_sz = order;
	iwarp_debug("%s: sz: %d log(sz): %d", __func__, ht->sz, ht->lg_sz);
	ht->numnodes = 0;
	ht->htab = kmalloc(ht->sz * sizeof(*ht), GFP_KERNEL);
	if (!ht->htab)
		return -ENOMEM;

	for (i = 0; i < ht->sz; i++)
		INIT_HLIST_HEAD(&(ht->htab[i]));

	spin_lock_init(&ht->lock);
	return 0;
}

int ht_destroy_callback(ht_t *ht, void (*func)(void *x))
{
	int i, ret = 0;
	struct hlist_head *head;
	struct hlist_node *node, *next;
	ht_node_t *nod;

	HT_LOCK(ht->lock);

	for (i = 0; i < ht->sz; i++) {
		head = &(ht->htab[i]);
		hlist_for_each_safe(node, next, head) {
			nod = hlist_entry(node, ht_node_t, hnod);
			hlist_del(&(nod->hnod));
			if (func)
				(*func)(nod->val);
			kfree(nod);
			ht->numnodes--;
		}
	}
	if (ht->numnodes) {
		ret = -ENOTSYNC;
		goto unlock;
	}

	kfree(ht->htab);
	ht->htab = 0;
	ht->sz = 0;

unlock:
	HT_UNLOCK(ht->lock);
	return ret;
}

int ht_destroy(ht_t *ht)
{
	return ht_destroy_callback(ht, NULL);
}

int ht_insert(void *key, void *val, ht_t *ht)
{
	int ret = 0;
	uint32_t idx = hash_long((unsigned long)key, ht->lg_sz);
	struct hlist_head *head = &(ht->htab[idx]);
	struct hlist_node *node;
	ht_node_t *nod;

	HT_LOCK(ht->lock);

	hlist_for_each(node, head) {
		nod = hlist_entry(node, ht_node_t, hnod);
		if (nod->key == key)  {
			ret = -EEXIST;
			goto unlock;
		}
	}

	nod = kmalloc(sizeof(*nod), GFP_KERNEL);
	if (!nod) {
		ret = -ENOMEM;
		goto unlock;
	}
	nod->key = key;
	nod->val = val;
	INIT_HLIST_NODE(&(nod->hnod));
	hlist_add_head(&(nod->hnod), head);
	ht->numnodes++;

unlock:
	HT_UNLOCK(ht->lock);
	return ret;
}

/* val: the returned value
 * returns the status of operation
 */
int ht_lookup(void *key, void **val, ht_t *ht)
{
	int ret;
	uint32_t idx = hash_long((unsigned long)key, ht->lg_sz);
	struct hlist_head *head = &(ht->htab[idx]);
	struct hlist_node *node;
	ht_node_t *nod;
	*val = NULL;

	HT_LOCK(ht->lock);

	ret = -ENOENT;
	hlist_for_each(node, head) {
		nod = hlist_entry(node, ht_node_t, hnod);
		if (nod->key == key) {
			*val = nod->val;
			ret = 0;
			break;
		}
	}

	HT_UNLOCK(ht->lock);
	return ret;
}

/*
 * val is not tracked inside ht, it is responsibility of caller to release
 * the memory associated with val.
 */
int ht_delete_callback(void *key, ht_t *ht, void (*func)(void *x))
{
	int ret;
	uint32_t idx = hash_long((unsigned long)key, ht->lg_sz);
	struct hlist_head *head = &(ht->htab[idx]);
	struct hlist_node *node;
	ht_node_t *nod;

	HT_LOCK(ht->lock);

	ret = -EINVAL;
	hlist_for_each(node, head) {
		nod = hlist_entry(node, ht_node_t, hnod);
		if (nod->key == key) {
			hlist_del(&(nod->hnod));
			if (func)
				(*func)(nod->val);
			kfree(nod);
			ht->numnodes--;
			ret = 0;
			break;
		}
	}

	HT_UNLOCK(ht->lock);
	return ret;
}

int ht_delete(void *key, ht_t *ht)
{
	return ht_delete_callback(key, ht, NULL);
}

/*
 * It is not safe to change the table in any way inside the callback.
 */
int ht_iterate_callback(ht_t *ht, int (*func)(void *arg1, void *arg2),
                         void *arg)
{
	int i = 0;
	int ret = 0;

	HT_LOCK(ht->lock);
	for (i=0; i<ht->sz; i++) {
		struct hlist_node *node;
		hlist_for_each(node, &ht->htab[i]) {
			ht_node_t *nod = hlist_entry(node, ht_node_t, hnod);
			ret = (*func)(nod->val, arg);
			if (ret)
				goto unlock;
		}
	}
unlock:
	HT_UNLOCK(ht->lock);
	return ret;
}
