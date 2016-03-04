/*
 * hash table interface
 *
 * $Id: ht.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#ifndef __HT_H
#define __HT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>

typedef struct {
	struct hlist_node hnod;
	void *key;
	void *val;
} ht_node_t;

typedef struct {
	uint32_t sz;
	uint32_t lg_sz;
	uint32_t numnodes;
	struct hlist_head *htab;
	spinlock_t lock;
} ht_t;

int ht_create(uint8_t order, ht_t *ht);
int ht_destroy_callback(ht_t *ht, void (*func)(void *x));
int ht_destroy(ht_t *ht);
int ht_insert(void *key, void *val, ht_t *ht);
int ht_lookup(void *key, void **val, ht_t *ht);
int ht_delete_callback(void *key, ht_t *ht, void (*func)(void *x));
int ht_delete(void *key, ht_t *ht);
int ht_iterate_callback(ht_t *ht, int (*func)(void *arg1, void *arg2),
			void *arg);

#endif /* __HT_H */
