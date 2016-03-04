/*
 * Memory handling header.
 *
 * $Id: test_hdrs.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <rdmap.h>
#include <ddp.h>


int main()
{
	ddp_control_field_t cf=0;
	rdmap_control_field_t rcf=0;
	ddp_set_TAGGED(cf);
	ddp_set_NOTLAST(cf);
	ddp_set_RSVD(cf);
	ddp_set_DV(cf);

	rdmap_set_RV(rcf);
	rdmap_set_RSVD(rcf);
	rdmap_set_OPCODE(rcf, TERMINATE);

	printf("%x %x %x\n", cf, ddp_get_DV(cf), rcf);
	return 0;
}
