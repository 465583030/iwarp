/*
 * hash table impl.
 *
 * $Id: ht.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ht.h"
#include "common.h"
#include "util.h"


static inline node_t **ht_lookup_node(ht_t *ht, hkey_t k);

ht_t *ht_create(uint32_t sz, dtor_t vdf)
{
	ht_t *ht = Malloc(sizeof(*ht));
	ht->sz = sz;
	ht->nnodes = 0;
	ht->nodes = Malloc(ht->sz * sizeof(*ht->nodes));
	memset(ht->nodes, 0, ht->sz * sizeof(*ht->nodes));
	if (!vdf)
	    vdf = free;
	ht->val_dtor_func = vdf;

	return ht;
}

void ht_destroy(ht_t *ht)
{
	uint32_t i, n;
	node_t *p = NULL;
	node_t *c = NULL;

	iw_assert(ht != NULL, "NULL ht %s:%d", __FILE__, __LINE__);

	n = 0;
	for(i = 0; i< ht->sz; i++) {
		p = NULL;
		c = ht->nodes[i];
		while ( c != NULL) {
			p = c;
			c = p->next;
			ht->val_dtor_func(p->val);
			free(p);
			n++;
		}
	}

	iw_assert(n == ht->nnodes, "n(%u) != ht->nnodes(%u)", n, ht->nnodes);

	free(ht->nodes);
	free(ht);
}

static inline node_t **
ht_lookup_node(ht_t *ht, hkey_t k)
{
	node_t **n = &(ht->nodes[k % (ht->sz)]);

	while(*n && (*n)->key != k)
		n = &((*n)->next);

	return n;
}

void
ht_insert(ht_t *ht, hkey_t k, void *v)
{
	iw_assert(ht && v, "ht(%p) or v(%p) %s:%d", ht, v, __FILE__, __LINE__);

	node_t **n = ht_lookup_node(ht, k);

	if (*n == NULL) {
		*n = Malloc(sizeof(node_t));
		(*n)->key = k;
		(*n)->val = v;
		(*n)->next = NULL;
		ht->nnodes++;
	} else {
		error("%s: duplicate key %d exists", __func__, k);
	}
}

void
ht_delete(ht_t *ht, hkey_t k)
{
	iw_assert(ht != NULL, "NULL ht %s:%d", __FILE__, __LINE__);

	node_t *c = ht->nodes[k % ht->sz];
	node_t *p = NULL;

	while(c && c->key != k) {
		p = c;
		c = p->next;
	}

	if (c != NULL) {
		if (p) {
			p->next = c->next;
			ht->val_dtor_func(c->val);
			free(c);
		} else {
			ht->nodes[k % ht->sz] = c->next;
			ht->val_dtor_func(c->val);
			free(c);
		}
		ht->nnodes--;
	} else {
		error("%s: key %d does not exist", __func__, k);
	}
}

void *
ht_lookup(ht_t *ht, hkey_t k)
{
	iw_assert(ht != NULL, "NULL ht %s:%d", __FILE__, __LINE__);

	node_t **n = ht_lookup_node(ht, k);

	if (*n != NULL)
		return (*n)->val;
	else
		return NULL;
}
