/*
* Send work request verbs
*
*$Id: swr.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*/

#include "verbs.h"
#include "stdio.h"
#include "stdlib.h"

/*
 * Post a send to the send queue
 */
iwarp_status_t iwarp_qp_post_sq(/*IN*/iwarp_rnic_handle_t rnic_hndl,
  iwarp_qp_handle_t qp_hndl, iwarp_wr_t *sq_wr)
{
    int ret;


    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    if (unlikely(rnic_ptr->qp_index[qp_hndl].connected != TRUE))
	return IWARP_NO_CONNECTION;

    ret = iwarp_send_event_dispatch_one(rnic_ptr, &rnic_ptr->qp_index[qp_hndl], sq_wr);
    if (ret)
	return ret;

    /*if it was signaled then we do nothing, if it was unsignaled then we need to throw away the CQ event*/


    if (sq_wr->cq_type == UNSIGNALED) {
	//~ printf("We have an unsignaled send\n");
	ret = v_throw_away_cqe(rnic_ptr, rnic_ptr->qp_index[qp_hndl].attributes->sq_cq);
	if(ret != IWARP_OK)
	    return IWARP_RWQ_INTERNAL_ERROR;
    }
    //~ else
	//~ printf("we have a SIGNALED SEND.....");

    return ret;
}

