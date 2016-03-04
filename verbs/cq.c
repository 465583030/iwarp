/*
* Implementation of the completion queue Interface for software iwarp
*
*Mostly just wrapper arouind cq code base in the parent directory
*
*$Id: cq.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*/
//~ #include <stdio.h>
#include "verbs.h"
#include <unistd.h> /*for usleep*/
#include <stdio.h>



iwarp_status_t iwarp_cq_create(/*IN*/iwarp_rnic_handle_t rnic_hndl, int *cqe_hndlr,
				/*INOUT*/uint32_t num_evts,
				/*OUT*/ iwarp_cq_handle_t *cq_hndl)
/*
Create a completion queue
TODO: pass in num_evts as a pointer so we can get something back in it
*/
{
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    if(!ENABLE_CQE_HANDLER) /*make sure we are supporting completion event handlers if they try to use one*/
	if(cqe_hndlr != NULL)
	    return IWARP_CQE_HANDLER_UNSUPPORTED;

    /*check depth requested*/
    if(num_evts > MAX_CQ_DEPTH)
	return IWARP_INSUFFICIENT_RESOURCES;


    return v_create_cq(rnic_ptr, (int *) &num_evts, cq_hndl);

}


iwarp_status_t iwarp_cq_destroy(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_cq_handle_t cq_hndl)
/*
Destroy the completion queue
Need to check and make sure no work requests are outstanding for this completion queue or else there is trouble
*/
{
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    return v_destroy_cq(rnic_ptr, cq_hndl);





}

iwarp_status_t iwarp_cq_poll(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_cq_handle_t cq_hndl,
			    /*IN*/int retrys, int time_out,
			    /*OUT*/iwarp_work_completion_t *wc)
/*
Poll the completion queue and return in the wc struct the status and type
*/
{


    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    return v_poll_cq(rnic_ptr, cq_hndl, retrys, time_out, wc);



}

iwarp_status_t iwarp_cq_poll_block(iwarp_rnic_handle_t rnic_hndl,
    iwarp_cq_handle_t cq_hndl, iwarp_qp_handle_t qp_id,
    iwarp_work_completion_t *wc)
{
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    return v_poll_block_qp(rnic_ptr, cq_hndl, qp_id, wc);
}

