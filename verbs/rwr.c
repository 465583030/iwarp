/*
* Receive work request verbs
*
*$Id: rwr.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*/
//~ #include <stdio.h>
#include "verbs.h"
//~ #include <stdlib.h>

/*
 * Put a receive work request onto the receive queue.
 */
iwarp_status_t iwarp_qp_post_rq(/*IN*/iwarp_rnic_handle_t rnic_hndl,
  iwarp_qp_handle_t qp_hndl, iwarp_wr_t *rq_wr)
{
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);

    int ret;
    unsigned int i;
    //~ cqe_t cq_evt;

    /*only if connected do we dispatch the queue*/
    if (rnic_ptr->qp_index[qp_hndl].connected) {
	ret = iwarp_recv_event_dispatch_one(rnic_ptr, &rnic_ptr->qp_index[qp_hndl], rq_wr);
	if (ret)
	    return ret;
    } else {
	/* add WR to the queue, flushed when connection made */
	if (unlikely(rnic_ptr->recv_q.size == MAX_WRQ))
	    return IWARP_RWQ_FULL;

	/* struct copy */
	rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size] = *rq_wr;

	/*need to copy the SGLs over in case user deletes them*/
	rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl = malloc(sizeof (iwarp_sgl_t));
	for(i=0; i<rq_wr->sgl->sge_count; i++){
	    /*copy length of each one*/
	    rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl->sge[i].length = rq_wr->sgl->sge[i].length;
	    /*now stag*/
	    rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl->sge[i].stag = rq_wr->sgl->sge[i].stag;
	    /*last the to*/
	    rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl->sge[i].to = rq_wr->sgl->sge[i].to;
	}
	rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl->sge_count = rq_wr->sgl->sge_count;

	++rnic_ptr->recv_q.size;
	/*keep track of how many we post before connecting*/
	++rnic_ptr->qp_index[qp_hndl].pre_connection_posts;
	ret = IWARP_OK;
    }

     if (rq_wr->cq_type == UNSIGNALED) {  /*TODO: Test what happens when is unsignaled and before connection made if no connection
	 and its unsignaled there will be noting to pull off cq*/
	//~ ret = v_throw_away_cqe(rnic_ptr, rnic_ptr->qp_index[qp_hndl].attributes->rq_cq);
	//~ if(ret != IWARP_OK)
	    //~ return IWARP_RWQ_INTERNAL_ERROR;

	//~ ret = cq_consume(rnic_ptr->qp_index[qp_hndl].attributes->rq_cq, &cq_evt);  /*pop off a cq entry just throw it away*/
	//~ if (ret)
	    //~ return IWARP_RWQ_INTERNAL_ERROR;
	return IWARP_UNSUPORTED_COMPL_TYPE;

    }

    return ret;
}

