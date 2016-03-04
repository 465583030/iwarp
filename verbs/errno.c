/*
 * Print out meaningful error messages.
 *
 * $Id: errno.c 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
 */
#include <stdint.h>
#include "verbs/verbs.h"

#define ERRNO_ENTRY(name,init) { name, #name },
static struct {
	iwarp_status_t errno;
	const char *name;
} errno_name[] = {
#include "verbs/errno.h"
};
#undef ERRNO_ENTRY
#define num_errno_name ((int)(sizeof(errno_name)/sizeof(errno_name[0])))

const char *
iwarp_string_from_errno(iwarp_status_t en)
{
	int i;

	for (i=0; i<num_errno_name; i++)
		if (errno_name[i].errno == en)
			return errno_name[i].name;
	return NULL;
}

