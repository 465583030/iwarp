/*
* Functions that are not meant to be exported to the user as verbs
*
*$Id: protected.c 653 2006-08-28 20:14:04Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*/
#include "verbs.h" 
#include <stdio.h>
//~ #include <stdlib.h>

/*
 * Dispatch just a single request, used when queueing is not needed to avoid
 * some mem copies, and called by the queue dispatcher below.  Returns 0
 * if successful, else some error.
 */
int iwarp_recv_event_dispatch_one(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, const iwarp_wr_t *rq_wr)
{
    int ret;
    uint32_t len = rq_wr->sgl->sge[0].length;
    void *buffer = ptr_from_int64(rq_wr->sgl->sge[0].to);
    iwarp_stag_index_t local_stag;
    
    local_stag = rq_wr->sgl->sge[0].stag;
    if (unlikely(rq_wr->sgl->sge_count != 1))
	return IWARP_UNSUPPORTED_WR_COUNT;
    ret = v_rdmap_post_recv(rnic_ptr, qp->socket_fd, buffer, len, rq_wr->wr_id, local_stag);
    if (ret != IWARP_OK)
	return IWARP_RDMAP_POST_RECV_FAILURE;
    //~ ret = rdmap_post_recv(qp->socket_fd, buffer, len, rq_wr->wr_id);
    //~ if (ret)
	//~ return IWARP_RDMAP_POST_RECV_FAILURE; 
    

    return 0;
}

/*
 * Dispatch an event off of the queue - enables us to prepost receives before connecting.
 * Returns 0 if successful.
*/
int iwarp_recv_event_dispatcher(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, iwarp_wr_q_t *work_q)
{
    int i, ret;
    
    //~ printf("...........before loop work q size is %d\n", work_q->size);

    for(i = 0; i < work_q->size; i++){  /*for each element in the work queue*/
	/*normally we would have to pass the scatter gather list but since we are not supporting multiple entries we won't do that*/
	ret = iwarp_recv_event_dispatch_one(rnic_ptr, qp, &work_q->queue[i]);
	if (ret)
	    return ret;
    }
    /*Reset the work queue*/
    work_q->size = 0;
    
    
    /*now we need to free the resources we allocated to save the SGLs*/
    free(rnic_ptr->recv_q.queue[rnic_ptr->recv_q.size].sgl);
    
    
     //~ printf("...........before returning work q size is %d\n", work_q->size);
    return 0;
}

/*
 * Dispatch just a single send request.
 * If we don't want to really initiate a send every time we post a request then we can use this mechanism to process the queue
 * For example we could do a hold on dispatching sends.
 * Return 0 if okay or error number.
 */ 
int iwarp_send_event_dispatch_one(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, const iwarp_wr_t *sq_wr)
{
    int ret = 0;
    int err;
    uint32_t len;
    uint64_t to, remote_to;
    void *buffer;
    iwarp_stag_index_t local_stag, remote_stag;
    
    /*if multiple scatter gather entries then we need to do more than just use array index 0*/
    
    /*save local stag*/
    local_stag = sq_wr->sgl->sge[0].stag;
    
    if (unlikely(sq_wr->sgl->sge_count != 1))
	return IWARP_UNSUPPORTED_WR_COUNT;

    switch(sq_wr->wr_type){
	case IWARP_WR_TYPE_SEND:
	    len = sq_wr->sgl->sge[0].length;
	    buffer = ptr_from_int64(sq_wr->sgl->sge[0].to);
	    /*untagged send provided to verbs by rdmap layer*/
	    err = v_rdmap_post_send(rnic_ptr, qp->socket_fd, buffer, len, sq_wr->wr_id, local_stag);
	    if (err != IWARP_OK)
		ret = IWARP_RDMAP_POST_SEND_FAILURE;
	    break;
	
	case IWARP_WR_TYPE_RECV:
	    ret = IWARP_INVALID_SQ_OPERATION;
	    break;
	
	case IWARP_WR_TYPE_RDMA_WRITE:
	    if (unlikely(sq_wr->remote_sgl->sge_count != 1)) {
		ret = IWARP_UNSUPPORTED_WR_COUNT;
		break;
	    }

	    /*Set local info*/
	    len = sq_wr->sgl->sge[0].length;
	    buffer = ptr_from_int64(sq_wr->sgl->sge[0].to);
	    
	    /*Set remote info*/
	    to = sq_wr->remote_sgl->sge[0].to;
	    remote_stag = sq_wr->remote_sgl->sge[0].stag;
	
	    /*Do the RDMA write*/
	    //~ err = rdmap_rdma_write(qp->socket_fd, stag, to, buffer, len, sq_wr->wr_id);
	    err = v_rdmap_rdma_write(rnic_ptr, qp->socket_fd, remote_stag, to, buffer, len, sq_wr->wr_id, local_stag);
	    if (err != IWARP_OK)
		ret = IWARP_RDMAP_RDMA_WRITE_FAILURE;  
	
	    break;
	
	case IWARP_WR_TYPE_RDMA_READ:
	    if (unlikely(sq_wr->remote_sgl->sge_count != 1)) {
		ret = IWARP_UNSUPPORTED_WR_COUNT;
		break;
	    }

	    /*Local Info -- stag set above*/
	    to = sq_wr->sgl->sge[0].to;
	    len = sq_wr->sgl->sge[0].length;
	
	    /*Remote Info*/
	    remote_stag = sq_wr->remote_sgl->sge[0].stag;
	    remote_to = sq_wr->remote_sgl->sge[0].to;

	    /*Do the RDMA read*/
	    //~ err = rdmap_rdma_read(qp->socket_fd, stag, to, len, remote_stag, remote_to, sq_wr->wr_id);
	    err = v_rdmap_rdma_read(rnic_ptr, qp->socket_fd, local_stag, to, len, remote_stag, remote_to, sq_wr->wr_id);
	    if (err != IWARP_OK)
		ret = IWARP_RDMAP_RDMA_READ_FAILURE; 
	
	    break;

	default:
	    ret = IWARP_INVALID_SQ_OPERATION;
	    break;
    }
    return ret;
}

