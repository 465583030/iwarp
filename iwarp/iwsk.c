/*
 * iwarp socket implementation
 *
 * $Id: iwsk.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ht.h"
#include "iwsk.h"
#include "util.h"

static ht_t *iwht = NULL;

inline void
iwsk_init(void)
{
	iwht = ht_create(MAX_SOCKETS, free);
}

inline void
iwsk_fin(void)
{
	ht_destroy(iwht);
}

inline void
iwsk_insert(socket_t sock)
{
	iwsk_t *s = Malloc(sizeof(*s));
	memset(s, 0, sizeof(*s));
	s->sk = sock;
	ht_insert(iwht, sock, s);
}

inline void
iwsk_delete(socket_t sock)
{
	ht_delete(iwht, sock);
}

inline iwsk_t *
iwsk_lookup(socket_t sock)
{
	return ht_lookup(iwht, sock);
}
