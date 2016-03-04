/*
* Functions to stub between the kernel and software versions
*
*$Id: stubs.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*/




#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "verbs.h"
#include "stubs.h"


#ifdef KERNEL_IWARP
int KERNEL_MODE = 1;
#include "kiwarp/user.h"
#else
int KERNEL_MODE = 0;
#endif

iwarp_status_t v_RNIC_open(int index, iwarp_rnic_t *rnic)
/*
Open the RNIC, doesn't really do anything in user mode
TODO: get rid of const string below, set it somewher else or pass it in as an argument
*/
{

    ignore(index);
    #ifdef KERNEL_IWARP
	static const char *kiwarp_dev = "/dev/kiwarp";
	int fd;
	fd = open(kiwarp_dev, O_RDWR);
	//~ printf("the fd we got from the open is %d", fd);
	if(fd < 0)
	    return fd;
	else
	    rnic->fd = fd;
	    return IWARP_OK;

    #else
	ignore(rnic);
	return IWARP_OK;

    #endif
}


iwarp_status_t v_rnic_close(iwarp_rnic_t *rnic_ptr)
/*
Close the RNIC device from the users perspective
*/
{
    int ret;


    #ifdef KERNEL_IWARP
	ret = close(rnic_ptr->fd);

	if(ret != 0)
	    return IWARP_RNIC_CLOSE_FAILURE;
	else
	    return IWARP_OK;

    #else
	ignore(rnic_ptr);
	v_mem_fini();
	ret = v_rdmap_fin();
	if(ret != 0)
	    return IWARP_RDMAP_FIN_FAILURE;
	else
	    return IWARP_OK;
    #endif






}

iwarp_status_t v_mem_init()
/*
Call init on the memory module
*/
{
    #ifdef KERNEL_IWARP
	return IWARP_OK;
    #else
	mem_init();
	return IWARP_OK;
    #endif
}

iwarp_status_t v_rdmap_init()
/*
Start up the RDMAP stuff
*/
{
    #ifdef KERNEL_IWARP
	return IWARP_OK;
    #else
	return rdmap_init();  /*TODO: check what is returned by rdmap_init() then if thats is ok return IWARP_OK dont just return the result*/
    #endif
}

iwarp_status_t v_mem_fini()
/*
Stop the memory module
*/
{
    #ifdef KERNEL_IWARP
	return IWARP_OK;
    #else
	mem_fini();
	return IWARP_OK;
    #endif
}

iwarp_status_t v_rdmap_fin()
/*
Stop the RDMAP module
*/
{
    #ifdef KERNEL_IWARP
	return IWARP_OK;
    #else
	return rdmap_fin();
    #endif
}

iwarp_status_t v_mem_register(iwarp_rnic_t *rnic_ptr, void *buffer, uint32_t length, iwarp_mem_desc_t *mem_region)
/*
Register memory region with memory module
*/
{
    #ifdef KERNEL_IWARP
	int ret;
	struct user_mem_reg req_buf;
	req_buf.address = buffer;
	req_buf.len = length;
	req_buf.cmd = IWARP_MEM_REG;
	req_buf.mem_desc = (unsigned long *) mem_region;  /*TODO: Fix this*/
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	//~ printf("After the write mem_desc is %lx\n", (unsigned long) *mem_region);
	if(ret != sizeof(req_buf)){
	    //~ printf("ret is %d\n", ret);
	    return -1;  /*TODO: verbs error code*/
	}
	else
	    return IWARP_OK;
    #else
	ignore (rnic_ptr);
	ignore(mem_region);
	*mem_region = mem_register(buffer, length);

	return IWARP_OK;  /*TODO: check what is returned above to see if it registered ok*/

    #endif
}

iwarp_status_t v_mem_stag_create(iwarp_rnic_t *rnic_ptr, iwarp_mem_desc_t *mem_region,
    void *start, uint32_t length, iwarp_access_control_t access_flags, iwarp_prot_id pd, iwarp_stag_index_t *stag_index)
/*
Create the STag
*/
{
    #ifdef KERNEL_IWARP
	int ret;
	struct user_stag_create req_buf;
	req_buf.cmd = IWARP_STAG_CREATE;
	//~ printf("Passing mem region %lx to kernel\n", (unsigned long) *mem_region);
	req_buf.md = *mem_region;
	req_buf.start = start;
	req_buf.len = length;
	req_buf.rw = access_flags;
	req_buf.prot_domain = pd;
	req_buf.stag = (iwarp_stag_index_t *) stag_index;
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	//~ printf("value of ret is %d\n", ret);
	//~ printf("The Stag index from kernel is %d\n", *stag_index);
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else
	    return IWARP_OK;
	return 0;
    #else

	ignore(start);
	ignore(rnic_ptr);
	ignore(stag_index);
	*stag_index = mem_stag_create(0, *mem_region, 0, length, access_flags, pd);

	if(*stag_index < 0)
		return -1;  /*TODO: verbs error code*/
	else
		return IWARP_OK;
    #endif
}

iwarp_status_t v_mem_stag_destroy(iwarp_rnic_t *rnic_ptr, iwarp_stag_index_t stag_index)
/*
Destroy the STag to deregister memory
*/
{
    #ifdef KERNEL_IWARP
	struct user_stag_destroy req_buf;
	int ret;
	req_buf.cmd=IWARP_STAG_DESTROY;
	req_buf.stag=stag_index;
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else
	    return IWARP_OK;

    #else
	ignore(rnic_ptr);
	return  mem_stag_destroy(stag_index);
    #endif
}

iwarp_status_t v_mem_deregister(iwarp_rnic_t *rnic_ptr, iwarp_mem_desc_t mem_region)
/*
Deregister the memory from the memory module
*/
{
    #ifdef KERNEL_IWARP
	int ret;
	struct user_mem_dereg req_buf;
	req_buf.cmd = IWARP_MEM_DEREG;
	req_buf.md = mem_region;
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else
	    return IWARP_OK;

    #else
	ignore(rnic_ptr);
	return mem_deregister(mem_region);
    #endif
}

iwarp_status_t v_rnic_advance(iwarp_rnic_t *rnic_ptr)
/*
Advance the RNIC
*/
{
    #ifdef KERNEL_IWARP
	int ret;
	struct user_encourage req_buf;

	req_buf.cmd = IWARP_ENCOURAGE;
	//~ printf("doing rdmap encourage stuff\n");
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	//~ printf("back from rdmap encourage ret is %d but only wrote %d bytes\n", ret, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else
	    return IWARP_OK;


    #else
	ignore(rnic_ptr);
	return rdmap_poll();
    #endif
}

iwarp_status_t v_create_cq(iwarp_rnic_t *rnic_ptr, int *num_evts, iwarp_cq_handle_t *cq_hndl)
/*
Create the completion queue in user mode or have the kernel create it in kernel mode
*/
{

    #ifdef KERNEL_IWARP
	struct user_cq_create req_buf;
	 int ret;
	req_buf.cmd = IWARP_CQ_CREATE;
	req_buf.depth = *num_evts;
	req_buf.cq_handle = (uint64_t *)cq_hndl;


	//~ do the write here
	ret = write(rnic_ptr->fd, &req_buf, sizeof(struct user_cq_create));



	if(ret != sizeof(struct user_cq_create))
	    return -1; /*TODO: verbs error here*/
	else
	    return IWARP_OK;

    #else
	ignore(rnic_ptr);
	*cq_hndl = cq_create(*num_evts);
        if(*cq_hndl == 0)
	    return IWARP_INSUFFICIENT_RESOURCES;
	*num_evts = (*cq_hndl)->num_cqe;
	return IWARP_OK;
    #endif


}

iwarp_status_t v_destroy_cq(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl)
/*
Destroy the completion queue
*/
{
    #ifdef KERNEL_IWARP
	struct user_cq_destroy req_buf;
	int ret;
	/*Set up the buffer to pass the kernel*/
	req_buf.cmd = IWARP_CQ_DESTROY;
	req_buf.cq_handle = cq_hndl;

	/*write to the device*/
	ret = write(rnic_ptr->fd, &req_buf, sizeof(struct user_cq_destroy));

	if(ret != sizeof(struct user_cq_destroy))
	    return -1; /*TODO: verbs error code here*/
	else
	    return IWARP_OK;

    #else
	ignore(rnic_ptr);
        cq_destroy(cq_hndl);  /*No error checking, hope it works*/
	return IWARP_OK;
    #endif


}

iwarp_status_t v_poll_cq(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl, int retrys,
	                int time_out,  iwarp_work_completion_t *wc)
/*
Poll the completion queue
TODO: This has not been tested AT ALL YET
*/
{

    #ifdef KERNEL_IWARP
	int ret;
	int i = 0;
	struct user_poll req_buf;
	struct work_completion kwc;

	/*set up the kernel buffer*/
	req_buf.cmd = IWARP_POLL;
	req_buf.cq_handle = cq_hndl;
	req_buf.wc = &kwc;

	for(;; ){
	    ret = write(rnic_ptr->fd, &req_buf, sizeof(struct user_poll));
	    if(ret == sizeof(struct user_poll))
		break;

	    //~ printf("Still in the for loop...\n");
	    /*otherwise keep on going*/
	    if(time_out > 0) /*don't call usleep unless we want to sleep, otherwise even with 0 it still delays*/
		usleep(time_out);

	    if(retrys != IWARP_INFINITY){
		i++;
		if(i > retrys)
		    break;
	    }

	}


	//~ printf("Broke the for loop so it succeeded\n");

	switch(kwc.op){  /*need to convert the op code to the completion event type*/
	    case OP_RDMA_WRITE:
		wc->wr_type = IWARP_WR_TYPE_RDMA_WRITE;
		break;
	    case OP_RDMA_READ:
		wc->wr_type = IWARP_WR_TYPE_RDMA_READ;
		break;
	    case OP_SEND:
		wc->wr_type =  IWARP_WR_TYPE_SEND;
		//~ printf("Kernel returned OP code SEND\n");
		break;
	    case OP_RECV:
		wc->wr_type = IWARP_WR_TYPE_RECV;
	    break;
	    default:
		return IWARP_UNKNOWN_WR_TYPE;
	}


	switch(kwc.status){  /*convert the status of the completion*/
	    case RDMAP_SUCCESS:
		wc->status = IWARP_WR_SUCCESS;
		//~ printf("Kerenl returned WR Success\n");
	    break;
	    case RDMAP_FAILURE:
		wc->status = IWARP_WR_FAILURE;
		//~ printf("Kerenel returned FAILURE\n");
	    break;
	    default:
		return IWARP_UNKNOWN_STATUS_TYPE;
	}


	wc->wr_id = kwc.id;  /*work request ID*/

	/*how do we get the QP that this was associated with - more importantly WHY would we need to do this?*/
	//~ wc->qp_hndl =

	wc->bytes_recvd = kwc.msg_len;  /*how much data we really got*/
	//~ printf("got %d bytes sent\n", kwc.msg_len);

	/*How do we find out about invalidate STag,, do we really care?*/
	//~ wc->stag_invalidate =
	//~ iwarp_stag_index_t stag;  /*the stag that was invalidated*/

	return IWARP_OK;

    #else

	ignore(rnic_ptr);
	cqe_t cq_evt;
	int i = 0;
	int ret = 0;


	for(;; ){
	    ret = rdmap_poll();
	    if(ret != 0)
		return IWARP_RDMAP_POLL_FAILURE;

	    ret = cq_consume(cq_hndl, &cq_evt);  /*pop off a cq entry*/
	    if(ret == 0){  /*got one*/
		break;
	    }
	    /*otherwise keep on going*/
	    if(time_out > 0) /*don't call usleep unless we want to sleep, otherwise even with 0 it still delays*/
		usleep(time_out);

	    if(retrys != IWARP_INFINITY){
		i++;
		if(i > retrys)
		    break;
	    }

	}


	//~ printf("\n\nCQ EVENT IS %d\n\n", cq_evt.op);

	switch(cq_evt.op){  /*need to convert the op code to the completion event type*/
	    case OP_RDMA_WRITE:
		wc->wr_type = IWARP_WR_TYPE_RDMA_WRITE;
		break;
	    case OP_RDMA_READ:
		 wc->wr_type = IWARP_WR_TYPE_RDMA_READ;
		break;
	    case OP_SEND:
		wc->wr_type =  IWARP_WR_TYPE_SEND;
		break;
	    case OP_RECV:
		 wc->wr_type = IWARP_WR_TYPE_RECV;
	    break;
	    default:
		return IWARP_UNKNOWN_WR_TYPE;
	}

	switch(cq_evt.status){  /*convert the status of the completion*/
	    case RDMAP_SUCCESS:
		wc->status = IWARP_WR_SUCCESS;
	    break;
	    case RDMAP_FAILURE:
		wc->status = IWARP_WR_FAILURE;
	    break;
	    default:
		return IWARP_UNKNOWN_STATUS_TYPE;
	}

	wc->wr_id = cq_evt.id;  /*work request ID*/

	/*how do we get the QP that this was associated with - more importantly WHY would we need to do this?*/
	//~ wc->qp_hndl =

	wc->bytes_recvd = cq_evt.msg_len;  /*how much data we really got*/

	/*How do we find out about invalidate STag,, do we really care?*/
	//~ wc->stag_invalidate =
	//~ iwarp_stag_index_t stag;  /*the stag that was invalidated*/

	return IWARP_OK;
    #endif


}

iwarp_status_t v_poll_block_qp(iwarp_rnic_t *rnic_ptr,
                               iwarp_cq_handle_t cq_hndl,
			       iwarp_qp_handle_t qp_id,
			       iwarp_work_completion_t *wc)
{
#ifdef KERNEL_IWARP
    int ret;
    struct user_poll_block req_buf;
    struct work_completion kwc;

    /*set up the kernel buffer*/
    req_buf.cmd = IWARP_POLL_BLOCK;
    req_buf.fd = rnic_ptr->qp_index[qp_id].socket_fd;
    req_buf.cq_handle = cq_hndl;
    req_buf.wc = &kwc;

    ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
    if (ret != sizeof(req_buf))
	error("%s: write failed", __func__);

    switch (kwc.op) {
	case OP_RDMA_WRITE:
	    wc->wr_type = IWARP_WR_TYPE_RDMA_WRITE;
	    break;
	case OP_RDMA_READ:
	    wc->wr_type = IWARP_WR_TYPE_RDMA_READ;
	    break;
	case OP_SEND:
	    wc->wr_type =  IWARP_WR_TYPE_SEND;
	    //~ printf("Kernel returned OP code SEND\n");
	    break;
	case OP_RECV:
	    wc->wr_type = IWARP_WR_TYPE_RECV;
	    break;
	default:
	    return IWARP_UNKNOWN_WR_TYPE;
    }

    switch (kwc.status){
	case RDMAP_SUCCESS:
	    wc->status = IWARP_WR_SUCCESS;
	    break;
	case RDMAP_FAILURE:
	    wc->status = IWARP_WR_FAILURE;
	    break;
	default:
	    return IWARP_UNKNOWN_STATUS_TYPE;
    }

    wc->wr_id = kwc.id;

    wc->bytes_recvd = kwc.msg_len;
    return IWARP_OK;
#else
    error("%s: not implemented for userspace iwarp", __func__);
    rnic_ptr = 0;
    cq_hndl = 0;
    qp_id = 0;
    wc = 0;
#endif
}

iwarp_status_t v_rdmap_register_connection(iwarp_rnic_t *rnic_ptr, iwarp_qp_handle_t qp_id, const char private_data[],
				           char *remote_private_data, int rpd, iwarp_host_t type)
/*
Register the socket as well as set markers and crc settings in RDMAP
*/
{
    int ret;

    #ifdef KERNEL_IWARP
	struct user_register_sock req_buf;
	struct user_sock_attrs req_buf2;
	struct user_init_startup req_buf3;
	char *temp;
    	int local_pd_len = strlen(private_data);

	temp = malloc(local_pd_len);  /*make a temp char * to hold the local private data for passing to kernel throw away when done*/
	strcpy(temp, private_data);

	/*register the socket*/
	req_buf.cmd = IWARP_REGISTER_SOCK;
	req_buf.fd = rnic_ptr->qp_index[qp_id].socket_fd;
	req_buf.scq_handle = rnic_ptr->qp_index[qp_id].attributes->sq_cq;
	req_buf.rcq_handle = rnic_ptr->qp_index[qp_id].attributes->rq_cq;
	//~ printf("Telling the Kernel %d for socket\n", rnic_ptr->qp_index[qp_id].socket_fd);
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/


	/*now to set up socket options*/
	req_buf2.cmd = IWARP_SET_SOCK_ATTRS;
	req_buf2.fd = req_buf.fd = rnic_ptr->qp_index[qp_id].socket_fd;;
	req_buf2.use_crc = !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_crc;
	req_buf2.use_mrkr = !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_markers;
	ret = write(rnic_ptr->fd, &req_buf2, sizeof(req_buf2));
	if(ret != sizeof(req_buf2))
	    return -1;  /*TODO: verbs error code*/

	/*tell kernel to do handshake negotiation for markers and crc*/

	req_buf3.cmd = IWARP_INIT_STARTUP;
	req_buf3.fd = rnic_ptr->qp_index[qp_id].socket_fd;
	req_buf3.is_initiator = type;
	req_buf3.pd_in = temp;
	req_buf3.len_in = local_pd_len;
	req_buf3.pd_out = remote_private_data;
	req_buf3.len_out = rpd;
	ret = write(rnic_ptr->fd, &req_buf3, sizeof(req_buf3));
	if(ret != sizeof(req_buf3))
	    return -1;  /*TODO: verbs error code*/

	return IWARP_OK;

    #else

	//~ printf("getting ready to do rdmap register sock on %d cqs are %p and %p \n", rnic_ptr->qp_index[qp_id].socket_fd,  rnic_ptr->qp_index[qp_id].attributes->sq_cq, rnic_ptr->qp_index[qp_id].attributes->rq_cq);

	ret = rdmap_register_sock(rnic_ptr->qp_index[qp_id].socket_fd, rnic_ptr->qp_index[qp_id].attributes->sq_cq, rnic_ptr->qp_index[qp_id].attributes->rq_cq);
	if(ret != 0)
	    return IWARP_RDMAP_REGISTER_SOCKET_FAILURE;

	//~ printf("back from register sock\n");

	ret = rdmap_mpa_use_markers(rnic_ptr->qp_index[qp_id].socket_fd, !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_markers);
	if(ret != 0)
	    return IWARP_RDMAP_SET_MARKER_FAILURE;

	ret = rdmap_mpa_use_crc(rnic_ptr->qp_index[qp_id].socket_fd, !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_crc);
	if(ret != 0)
	    return IWARP_RDMAP_SET_CRC_FAILURE;

	ret = rdmap_init_startup(rnic_ptr->qp_index[qp_id].socket_fd, type, private_data, remote_private_data, rpd);
	if(ret != 0)
	    return IWARP_MPA_INIT_FAILURE;

	ignore(type);

	return IWARP_OK;
    #endif

}

iwarp_status_t v_throw_away_cqe(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl)
/*
Throw away a completion queue event because user wants an unsignaled completion
*/
{

    #ifdef KERNEL_IWARP
	iwarp_work_completion_t wc;

	return v_poll_cq(rnic_ptr, cq_hndl, 1, 0, &wc);



    #else
    int ret = 0;
    cqe_t cq_evt;
    ret = cq_consume(cq_hndl, &cq_evt);  /*pop off a cq entry just throw it away*/
    if (ret)
	return IWARP_RWQ_INTERNAL_ERROR;

    return IWARP_OK;
    ignore(rnic_ptr);

    #endif

}

iwarp_status_t v_rdmap_post_recv(iwarp_rnic_t *rnic_ptr, int socket_fd, void *buffer, uint32_t length, iwarp_wr_id_t wr_id, iwarp_stag_index_t local_stag)
/*
Post a recv
*/
{
    #ifdef KERNEL_IWARP
	struct user_post_recv req_buf;
	int ret;
	req_buf.local_stag = local_stag;	/*Pass STag to kernel*/
	req_buf.cmd = IWARP_POST_RECV;
	req_buf.fd = socket_fd;
	req_buf.id = wr_id;
	//~ req_buf.post_type = IWARP_POST_RECV;
	req_buf.buf = buffer;
	req_buf.len = length;

	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));



	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else return IWARP_OK;


    #else
    int ret;
    local_stag = local_stag;  /* unused */
    ret = rdmap_post_recv(socket_fd, buffer, length, wr_id);
    if (ret)
	return IWARP_RDMAP_POST_RECV_FAILURE;
    else
	return IWARP_OK;

    ignore(rnic_ptr);

    #endif
}


iwarp_status_t v_rdmap_post_send(iwarp_rnic_t *rnic_ptr, int socket_fd, void *buffer, uint32_t length, iwarp_wr_id_t wr_id, iwarp_stag_index_t local_stag)
/*
Post a recv
*/
{
    #ifdef KERNEL_IWARP
	struct user_send req_buf;
	int ret;
	req_buf.local_stag = local_stag;	/*Pass STag to kernel*/
	req_buf.cmd = IWARP_SEND;
	req_buf.fd = socket_fd;
	req_buf.id = wr_id;
	//~ req_buf.post_type = IWARP_POST_SEND;
	req_buf.buf = buffer;
	req_buf.len = length;

	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else
	    return IWARP_OK;

    #else
    int ret;
    local_stag = local_stag;  /* unused */
    ret = rdmap_send(socket_fd, buffer, length, wr_id);
    if (ret)
	return IWARP_RDMAP_POST_SEND_FAILURE;
    else
	return IWARP_OK;
    ignore(rnic_ptr);
    #endif

}


iwarp_status_t v_rdmap_rdma_write(iwarp_rnic_t *rnic_ptr, int socket_fd, iwarp_stag_index_t remote_stag, uint64_t to,  void *buffer, uint32_t len, iwarp_wr_id_t wr_id,
						    iwarp_stag_index_t local_stag)
/*
Post and RDMA write
*/
{

    #ifdef KERNEL_IWARP
	struct user_rdma_write req_buf;
	int ret;

	req_buf.local_stag = local_stag;	/*Pass STag to kernel*/
	req_buf.cmd = IWARP_RDMA_WRITE;
	req_buf.fd = socket_fd;
	req_buf.id = wr_id;
	req_buf.buf = buffer;
	req_buf.len = len;
	req_buf.sink_stag = remote_stag;
	req_buf.sink_to = to;

	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1;  /*TODO: verbs error code*/
	else return IWARP_OK;

    #else
	int err;
	local_stag = local_stag;  /* unused */
	err = rdmap_rdma_write(socket_fd, remote_stag, to, buffer, len, wr_id);
	if (err != 0)
	    return IWARP_RDMAP_RDMA_WRITE_FAILURE;
	ignore(rnic_ptr);
	return IWARP_OK;
    #endif



}


iwarp_status_t v_rdmap_rdma_read(iwarp_rnic_t *rnic_ptr, int socket_fd, iwarp_stag_index_t local_stag, uint64_t to, uint32_t len,
				iwarp_stag_index_t remote_stag, uint64_t remote_to, iwarp_wr_id_t wr_id)
/*
RDMA Read
*/
{
    #ifdef KERNEL_IWARP
	struct user_rdma_read req_buf;
	int ret;

	req_buf.cmd = IWARP_RDMA_READ;
	req_buf.fd = socket_fd;
	req_buf.id = wr_id;
	req_buf.sink_stag = local_stag;
	req_buf.sink_to = to;
	req_buf.len = len;
	req_buf.src_stag = remote_stag;
	req_buf.src_to = remote_to;

	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1; /*TODO: verbs error code*/
	else
	    return IWARP_OK;
    #else
	int err;
	err = rdmap_rdma_read(socket_fd, local_stag, to, len, remote_stag, remote_to, wr_id);
	if (err != 0)
		    return IWARP_RDMAP_RDMA_READ_FAILURE;

	ignore(rnic_ptr);
	return IWARP_OK;
    #endif

}

iwarp_status_t v_rdmap_deregister_sock(iwarp_rnic_t *rnic_ptr, int socket_fd)
/*
Free resources that the kernel is holding related to the socket
*/
{
    int ret;
    #ifdef KERNEL_IWARP
	struct user_deregister_sock req_buf;

	req_buf.cmd = IWARP_DEREGISTER_SOCK;
	req_buf.fd = socket_fd;
	ret = write(rnic_ptr->fd, &req_buf, sizeof(req_buf));
	if(ret != sizeof(req_buf))
	    return -1; /*TODO: verbs error code*/
	else
	    return IWARP_OK;



    #else
	ret = rdmap_deregister_sock(socket_fd);
	if(ret != 0)
	    return IWARP_RDMAP_DEREGISTER_SOCKET_FAILURE;
	else
	    return IWARP_OK;
	ignore(rnic_ptr);
    #endif


}


