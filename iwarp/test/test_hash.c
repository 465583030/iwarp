/*
 * hash table test scaffold
 *
 * $Id: test_hash.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "ht.h"
#include "util.h"
#include "iwsk.h"

void test_create_destroy(void);
void test_insert(void);
void test_delete(void);
void test_chain_insert(void);
void test_chain_delete(void);
void test_repeat_lookups(void);
void test_iwsk_lookups(void);

void
test_create_destroy(void)
{
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	ht_destroy(ht);
}

void
test_insert(void)
{
	int *i = (int *)Malloc(sizeof(int));
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	ht_insert(ht, 4, i);
	ht_destroy(ht);
}

void
test_delete(void)
{
	int *i = (int *)Malloc(sizeof(int));
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	ht_insert(ht, 4, i);
	ht_delete(ht, 4);
	ht_destroy(ht);
}

/*
 * when free is called on same pointer more than once then undefined behavior
 * occurs. so test_chain_delete behaved incorrectly due to duplicate frees in
 * test_chain_insert.
 */
void
test_chain_insert(void)
{
	int *i;
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	i = (int *)Malloc(sizeof(int));
	ht_insert(ht, 4, i);
	i = (int *)Malloc(sizeof(int));
	ht_insert(ht, ht->sz+4, i);
	i = (int *)Malloc(sizeof(int));
	ht_insert(ht, ht->sz*2+4, i);
	printf("%d %d %d\n", ht->nodes[4]->key, ht->nodes[4]->next->key,
		   ht->nodes[4]->next->next->key);
	ht_destroy(ht);
}

void
test_chain_delete(void)
{
/*	int *i = (int *)Malloc(sizeof(int));
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	ht_insert(ht, 4, i);
	ht_insert(ht, ht->sz+4, i);
	ht_insert(ht, ht->sz*2+4, i);

	ht_delete(ht, ht->sz+4);
	i = (int *)Malloc(sizeof(int));
	ht_insert(ht, ht->sz+4, i);
	ht_delete(ht, 4);
	ht_destroy(ht);*/
	iwsk_t *i;
	printf("%s\n", __func__);
	ht_t *ht = ht_create(20, free);
	i = (iwsk_t *)Malloc(sizeof(iwsk_t));
	ht_insert(ht, 4, i);
	i = (iwsk_t *)Malloc(sizeof(iwsk_t));
	ht_insert(ht, ht->sz+4, i);
	i = (iwsk_t *)Malloc(sizeof(iwsk_t));
	ht_insert(ht, ht->sz*2+4, i);

	ht_delete(ht, ht->sz+4);
	i = (iwsk_t *)Malloc(sizeof(iwsk_t));
	ht_insert(ht, ht->sz+4, i);
	ht_delete(ht, 4);
	ht_destroy(ht);

}

void
test_repeat_lookups(void)
{
	ht_t *ht = ht_create(128, free);
	iwsk_t *i, *j;

	i = Malloc(sizeof(iwsk_t));
	ht_insert(ht, 4, i);

	j = ht_lookup(ht, 4);
	j = ht_lookup(ht, 4);
	j = ht_lookup(ht, 4);
	j = ht_lookup(ht, 4);
	j = ht_lookup(ht, 4);
	j = ht_lookup(ht, 4);

	ht_destroy(ht);
}

void
test_iwsk_lookups(void)
{
	iwsk_init();
	iwsk_insert(4);
	iwsk_t *i;

	i = iwsk_lookup(4);
	i = iwsk_lookup(4);
	i = iwsk_lookup(4);
	i = iwsk_lookup(4);

	iwsk_fin();
}

int main()
{
	test_create_destroy();
	test_insert();
	test_delete();
	test_chain_insert();
	test_chain_delete();
	test_repeat_lookups();
	test_iwsk_lookups();
	return 0;
}
