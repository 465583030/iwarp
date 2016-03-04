/*
 * hash table for manipulating socket related data structures.
 *
 * $Id: ht.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#ifndef __HT_H
#define __HT_H

#include <stdint.h>

typedef int hkey_t;
typedef void (*dtor_t)(void *v);

typedef struct node {
	hkey_t key;
	void *val;
	struct node *next;
} node_t;

typedef struct ht {
	uint32_t sz;
	uint32_t nnodes;
	node_t **nodes;
	dtor_t	val_dtor_func;
} ht_t;

ht_t *ht_create(uint32_t sz, dtor_t vdf);
void ht_destroy(ht_t *ht);
void ht_insert(ht_t *ht, hkey_t k, void *v);
void ht_delete(ht_t *ht, hkey_t k);
void *ht_lookup(ht_t *ht, hkey_t k);

#endif /* __HT_H */
