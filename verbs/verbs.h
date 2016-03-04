 /*
* Header file for Verbs layer
*
*$Id: verbs.h 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*Lots of influence from ccil api code (Ammasso)
*
*
*/
/*Header files which are shared by both Verbs layer as well as RDMAP/DDP layers*/
#ifndef VERBS_INC
#define VERBS_TEST

#define VERBS_TEST

/* upper directory headers */
#ifndef KERNEL_IWARP
	#include "cq.h"
	#include "mem.h"
	#include "mpa.h"

#endif

#include "rdmap.h"




/*Now stick with verbs specific header files*/
#include "limits.h"
#include "types.h"
#include "stubs.h"




#define IWARP_VERBS
#define ptr_from_int64(p) (void *)(unsigned long)(p)
#define int64_from_ptr(p) (u_int64_t)(unsigned long)(p)
#define ignore(p) (p)=(p)

#define TEST_VAL 4

#define MAX_ID_LEN 20

/********/
/*RNIC*/
/********/
/*RNIC OPEN
We have no real RNIC but we do have a data structure that we hang important information off of, we call it an RNIC just to sound more iwarp like.
NOTE: iwarp_pblmode must always be PAGE_MODE we do not support block mode.  Nothing is done with the context yet so NULL should just be
passed for it.  Index is always 0 for now.
*/
iwarp_status_t iwarp_rnic_open(/*IN*/int index, iwarp_pblmode_t mode, iwarp_context_t context,
			       /*OUT*/iwarp_rnic_handle_t* rnic_hndl);

/*RNIC QUERY
Just gets some information about the system.  Not necessary for most applications, called by the connection related verbs to get the host name
of the machine we are running on.
*/
iwarp_status_t iwarp_rnic_query(/*IN*/iwarp_rnic_handle_t rnic,
			        /*OUT*/iwarp_rnic_query_attrs_t *attrs);

/*RNIC CLOSE
Clost the RNIC which means we free any memory still held and terminate any remaining connections.
*/
iwarp_status_t iwarp_rnic_close(/*IN*/ iwarp_rnic_handle_t rnic_hndl);

/*RDMA ADVANCE
Since we don't want to export rdmap_poll() to the user we can use this to abstract that call we need to introduce this
because we don't have a thread to actively read the socket as data comes in.  We call rdmap_poll to see what data is
in the buffers and then place it.*/
iwarp_status_t iwarp_rnic_advance(iwarp_rnic_handle_t rnic_hndl);


/********************/
/*Protection Domain*/
/********************/
/*ALLOCATE PROTECTION DOMAIN
Allocate a proection domain ID, basically just reserve one from the RNIC.
*/
iwarp_status_t iwarp_pd_allocate(/*IN*/iwarp_rnic_handle_t rnic_hndl,
                                 /*OUT*/iwarp_prot_id *prot_id);

/*DEALLOCATE PROTECTION DOMAIN
Deallocate the previously held protection domain so another QP may use it.
Can only deallocate once it has no more resources associated with it, included QP's and Memory Regions
*/
iwarp_status_t iwarp_pd_deallocate(/*IN*/iwarp_rnic_handle_t rnic_hndl,
                                   /*IN*/iwarp_prot_id prot_id);

/********************/
/*Completion Queue*/
/********************/
/*CREATE COMPLETION QUEUE
Create a completion queue, this is a datastructure which is shared with the RDMA layer beneath the verbs layer.
*/
iwarp_status_t iwarp_cq_create(/*IN*/iwarp_rnic_handle_t rnic_hndl, int *cqe_hndlr,
				/*INOUT*/uint32_t num_evts,
				/*OUT*/ iwarp_cq_handle_t *cq_hndl);

/*DESTROY COMPLETION QUEUE
Destroys the Completion queue.
*/
iwarp_status_t iwarp_cq_destroy(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_cq_handle_t cq_hndl);


/*POLL CQ
Poll the requested completion queue - actuall does the reads on the socket on on the lower layers
Pass number of times to retry if IWARP_INFINITY (-1) try forever
Time out is in microseconds
*/
iwarp_status_t iwarp_cq_poll(iwarp_rnic_handle_t rnic_hndl,
					    iwarp_cq_handle_t cq_hndl,
					    int retrys,
					    int time_out,
					    iwarp_work_completion_t *wc);

iwarp_status_t iwarp_cq_poll_block(iwarp_rnic_handle_t rnic_hndl,
						    iwarp_cq_handle_t cq_hndl,
						    iwarp_qp_handle_t qp_id,
						    iwarp_work_completion_t *wc);


/***************/
/*Queue Pairs*/
/***************/
/*CREATE QUEUE PAIR
qp_attrs is local data type to verbs consumer - verbs keeps its own qp_attrs for the qp
*/
iwarp_status_t iwarp_qp_create(/*IN*/iwarp_rnic_handle_t rnic_hndl,
		        /*INOUT*/iwarp_qp_attrs_t *qp_attrs,
			/*OUT*/ iwarp_qp_handle_t *qp_id);

/*DESTROY QUEUE PAIR
Can not destroy QP until EVERYTHING associated is destroyed
*/
iwarp_status_t iwarp_qp_destroy(/*IN*/iwarp_rnic_handle_t rnic_hndl,
		       		/*OUT*/ iwarp_qp_handle_t qp_id);

/*QP PASSIVE CONNECT
Wait for a remote QP to connect on the specified port.
Space for private data must be allocated, size rpd is the max size of the remote private data
If mpa can't handle priv data as large as passed it will only deal with its max possible
If remote_private_data comes back with no null character on the end then we know it overflowed
*/
iwarp_status_t iwarp_qp_passive_connect(/*INOUT*/iwarp_rnic_handle_t rnic_hndl,
				    /*IN*/iwarp_port_t port, iwarp_qp_handle_t qp_id, const char private_data[],
				    /*OUT*/char *remote_private_data,
				    /*IN*/int size_rpd);

/*QP ACTIVE CONNECT
Actively attempt to connect to another QP
Space for private data must be allocated, size rpd is the max size of the remote private data
If mpa can't handle priv data as large as passed it will only deal with its max possible
If remote_private_data comes back with no null character on the end then we know it overflowed
*/
iwarp_status_t iwarp_qp_active_connect(/*INOUT*/iwarp_rnic_handle_t rnic_hndl,
				  /*IN*/iwarp_port_t port, const char *servername, int sleep_time,
				  /*IN*/int retrys, iwarp_qp_handle_t qp_id, const char private_data[],
				  /*OUT*/char *remote_private_data,
				  /*IN*/int size_rpd);

/*QP DISCONNECT
disconnect the queue pair
*/
iwarp_status_t iwarp_qp_disconnect(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_qp_handle_t qp_id);


/*************************/
/*Memory Management*/
/*************************/
/*REGISTER NON SHARED MEMORY
Returns an STag and a non iWarp spec mem_region, used for our new verb to deregister memory regions, deregister != free
*/
iwarp_status_t iwarp_nsmr_register(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_addr_t addr_t, void *buffer,
				    /*IN*/uint32_t length, iwarp_prot_id pd, iwarp_stag_key_t stag_key,
				    /*IN*/iwarp_access_control_t access_flags,
				    /*OUT*/iwarp_stag_index_t *stag_index, iwarp_mem_desc_t *mem_region);

/*DEALLOCATE STAG
Just what it says get rid of the STag, does not free nor deregister memory!
*/
iwarp_status_t iwarp_deallocate_stag(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_stag_index_t stag_index);

/*DEREGISTER MEMORY
Actaully deregister memory in the mem layer
*/
iwarp_status_t iwarp_deregister_mem(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_prot_id pd, iwarp_mem_desc_t mem_region);


/*********************************/
/*WORK REQUEST PROCESSING*/
/*********************************/
/*CREATE SGL
Create/initialize the scatter gather list
*/
iwarp_status_t iwarp_create_sgl(/*IN*/iwarp_rnic_handle_t rnic_hndl,
			    /*OUT*/iwarp_sgl_t *sgl);

/*REGISTER SGE
Register/Insert the SGE into an SGL
*/
iwarp_status_t iwarp_register_sge(/*IN*/iwarp_rnic_handle_t rnic_hndl,
    /*INOUT*/iwarp_sgl_t *sgl,
    /*IN*/ iwarp_sge_t *sge);

/*POST RQ
Post a work request to the receive queue on behalf of the queue pair
*/
iwarp_status_t iwarp_qp_post_rq(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_qp_handle_t qp_hndl, iwarp_wr_t *rq_wr);

/*POST SQ
Post a work request to the send queue on behalf of the queue pair
*/
iwarp_status_t iwarp_qp_post_sq(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_qp_handle_t qp_hndl, iwarp_wr_t *sq_wr);

/* errno.c */
const char *iwarp_string_from_errno(iwarp_status_t en);

/*****************************/
/*PROTECTED FUNCTIONS*/
/*****************************/
/*
Dispatch an event off the queue, or a # of events
*/
int iwarp_recv_event_dispatch_one(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, const iwarp_wr_t *rq_wr);
int iwarp_recv_event_dispatcher(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, iwarp_wr_q_t *work_q);
int iwarp_send_event_dispatch_one(iwarp_rnic_t *rnic_ptr, const iwarp_qp_t *qp, const iwarp_wr_t *sq_wr);
#endif
