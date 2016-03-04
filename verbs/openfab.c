/*
* OpenFabrics compativle API calls to our verbs API calls
*
*$Id: openfab.c 670 2007-08-23 18:51:00Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*
*/

/*
Problem with sw iWARP if deallocate the SGL associated with a WR it is lost and the WR can't complete this
maybe the way its supposed to be because WR takes a pointer to a SGL, then you post, after posting it doesn't
matter if SGL goes away or not.  But with SW iWARP we don't post until connected.  So for preposted recvs there
is a time gap between when calling the post rq verb and when it actually gets posted  --FIXED

Next up is figure out private data mess then go on to data transfer

Polling CQ is not blocking,, should it be?  If it should be then how long do we poll
forever?  Blocking CQs do not work currently for userspace anyway so ignore this all together


*/

#define RNIC abi_compat
#define IGNORE __attribute__ ((unused))

#include "openfab.h"



 /*RDMA CM Functions*/
struct rdma_event_channel *rdma_create_event_channel(void){
    /*just create the rdma_event_channel data structure and return to user
    modfied the rdma_event_channel structure to hold a pointer to a struct of
    important data*/

	void *evt_chan_ptr;
	struct rdma_event_channel *p;

	evt_chan_ptr = malloc(sizeof(struct rdma_event_channel));  /*allocate it here leave up to user
									to deallocate it*/
	p = evt_chan_ptr;
	p->conn_event_valid = 1;

	return evt_chan_ptr;
}

int rdma_create_id(struct rdma_event_channel *channel, struct rdma_cm_id **id,  void *context, enum rdma_port_space ps){
    /*link the rdma_cm_id to the (created) rdma_event_channel*/
    /*create an ib verbs context which has our swinfo struct and hook it here too*/

    struct rdma_cm_id *cm_id;
    struct ibv_context *ibv;
    of_sw_info_t *swinfo;

    ps = ps;

    cm_id = malloc(sizeof(struct rdma_cm_id)); /*allocate and let user deallocate*/
    ibv = malloc(sizeof(struct ibv_context));  /*deallocate this when deallocating the cm_id*/
    swinfo = malloc(sizeof(of_sw_info_t));

    swinfo->rnic_hndl = 0; /*set so know not been opened*/

    swinfo->fd = -1; /*so we know its not in use*/
    swinfo->listening = 0;  /*so we know not to close fd*/

    ibv->swinfo = swinfo; /*deallocate this later too*/

    cm_id->channel = channel;
    cm_id->verbs=ibv;
    cm_id->context = context;
    cm_id->qp = NULL;
    cm_id->port_num = 99;  /*give it a known random value so we can debug ptr crap*/

    /*id is pointing at a pointer to an rdma_cm_id struct
    so we need to make whatever id is pointing at to be something pointing to
    an rdma_cm_id struct since cm_id is pointing to the struct we want
    we need to make whatever id is pointing at be cm_id*/
    *id = cm_id;  /*voila*/

    debug(2, "cm_id is pointing to %p", cm_id);

    return 0;
}

int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr){

    /*basically just save the port number into the rdma_cm_id copy it into the sw info struct too
    we are ready to bind anything yet*/

    int port_num;

    struct sockaddr_in listen_addr;

    memcpy(&listen_addr, addr, sizeof(struct sockaddr_in));

    port_num = ntohs(listen_addr.sin_port);

    id->port_num = port_num;
    id->verbs->swinfo->port = port_num;

    debug(4, "In function %s: port num is %d", __func__,  id->verbs->swinfo->port);

    return 0;
}

int rdma_listen(struct rdma_cm_id *id, int backlog){
    int ret;
    iwarp_rnic_query_attrs_t attrs;
    struct sockaddr_in passive_socket;
    socklen_t passive_s_len = sizeof(passive_socket);
    iwarp_rnic_t *rnic_ptr;
    int i = 1;  /*flag for socket opt*/
    struct rdma_cm_id *new_cm_id;
    struct ibv_context *ibv;
    of_sw_info_t *swinfo;


    /*since we only have a verb to block while waiting and accepting a connection
    for OF we need to rewrite and split into two to keep form needing to create CQ, PD and QP
    we will just create an FD to listen on*/


    of_sw_info_t *myinfo;
    myinfo = id->verbs->swinfo;

    debug(4, "In function %s: the port num in our context is %d", __func__, myinfo->port);

    ret = check_open(id->verbs);   /*use this to open the RNIC user doesn't need to know the rnic handle so we'll
						    keep it in the swinfo->rnic_hndl struct hanging off of rdma_cm_id->verbs*/
    if(ret != 0){
	debug(0, "Error opening RNIC %s", iwarp_string_from_errno(ret));
	goto GET_OUT;
    }

    //~ return 0;


    ret = iwarp_rnic_query(myinfo->rnic_hndl, &attrs);  /*sanity check*/
    if(ret){
	debug(0, "RNIC is not opened afterall: %s", iwarp_string_from_errno(ret));
	goto GET_OUT;
    }

    rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(myinfo->rnic_hndl);

    debug(2,"rnic handle is %ld", myinfo->rnic_hndl);

    memset(&passive_socket, 0, passive_s_len);
    passive_socket.sin_family = attrs.address_type;
    free(attrs.vendor_name);  /* allocated by _query */
    /* do not bind on the interface IP; it prevents loopback tests */
    /* memcpy(&passive_socket.sin_addr, attrs.address, attrs.length); */
    passive_socket.sin_port = htons(myinfo->port);
    myinfo->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (myinfo->fd < 0){
	ret = IWARP_CAN_NOT_BUILD_SOCKET;
	goto GET_OUT;
    }

    if (setsockopt(myinfo->fd , SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0){
	ret = IWARP_CAN_NOT_BUILD_SOCKET;
	goto GET_OUT;
    }

    /*Attempt to bind socket to port*/
    if (bind(myinfo->fd, (struct sockaddr *)&passive_socket, passive_s_len) < 0){
	ret = IWARP_CAN_NOT_BUILD_SOCKET;
	goto GET_OUT;
    }


    /*XXX Maybe make socket non blocking that way we can poll (spin) until someone tries to connect
    then break and go on to accept, this way if accept fails we can return an error
    otherwise we would get blocked until someone tried to connect*/

    debug(2, "socket size is %d", sizeof(passive_socket));

    /*Listen on the socket*/
    if (listen(myinfo->fd , backlog) < 0){
	ret = IWARP_CAN_NOT_BUILD_SOCKET;
	goto GET_OUT;
    }
    myinfo->listening = 1;
    debug(2, "Socket is opened, it is bound, and we are listening, scoket fd is %d", myinfo->fd);


    //~ fd_set readfds, writefds, exceptfds;

    //~ /*set up fd sets for select call*/
    //~ FD_ZERO(&readfds);
    //~ FD_ZERO(&writefds);
    //~ FD_ZERO(&exceptfds);
    //~ FD_SET(myinfo->fd, &readfds);

    //~ /*set up sleep time*/


    //~ /*now lets block until someone tries to connect to socket*/
    //~ for(;;){
	//~ ret = select(myinfo->fd+1, &readfds, &writefds, &exceptfds, 0);
	//~ if(ret)
	    //~ break;
	//~ usleep(1000);
    //~ }
    //~ if(ret < 0){
	//~ debug(0, "Problem waiting for soemone to connect");
	//~ ret = errno;
	//~ goto GET_OUT;
    //~ }

    /*use the new field in the rdma_evt channel dohickey as a flag to tell us what event would have been thrown*/
    id->channel->next_event = RDMA_CM_EVENT_CONNECT_REQUEST;

    /*create new rdma_cm_id this is what we will accept the new connection on the other is listening still*/
    new_cm_id = malloc(sizeof(struct rdma_cm_id)); /*allocate and let user deallocate*/
    ibv = malloc(sizeof(struct ibv_context));  /*deallocate this when deallocating the cm_id*/
    swinfo = malloc(sizeof(of_sw_info_t));

    swinfo->rnic_hndl = myinfo->rnic_hndl; /*set so know its been opened*/

    swinfo->fd = myinfo->fd; /*remember for the accept*/
    swinfo->listening = 0;  /*so we know not to close fd*/

    ibv->swinfo = swinfo; /*deallocate this later too*/

    new_cm_id->channel = id->channel;
    new_cm_id->verbs=ibv;
    new_cm_id->context = id->context;
    new_cm_id->qp = id->qp;
    new_cm_id->port_num = 80;  /*give it a known random value so we can debug ptr crap*/

    id->channel->cm_id = new_cm_id;
    id->channel->fd = swinfo->fd;
    return 0;

GET_OUT:
    id->channel->next_event = RDMA_CM_EVENT_REJECTED;
    id->channel->cm_id = id;
    return ret;

}

int rdma_resolve_addr(struct rdma_cm_id *id, IGNORE struct sockaddr *src_addr, struct sockaddr *dest_addr, IGNORE int timeout_ms){

    /*copy the port number and server name into our context*/
    /*this will open our RNIC*/

    int port_num;
    struct hostent *host;
    struct sockaddr_in server_addr;

    //~ host = malloc(sizeof(struct hostent));

    memcpy(&server_addr, dest_addr, sizeof(struct sockaddr_in));

    port_num = ntohs(server_addr.sin_port);

    host = gethostbyaddr(&server_addr.sin_addr, sizeof(server_addr.sin_addr), server_addr.sin_family);
    if(!host){
	debug(0, "Could not get hostname from IP");
	return -1;
    }

    debug(2, "This is what we are gonig to try to connect to: %s", host->h_name);

    id->port_num = port_num;
    id->verbs->swinfo->port = port_num;
    //~ id->verbs->swinfo->masterhost = malloc(strlen(host->h_name));
    //~ strcpy(id->verbs->swinfo->masterhost, host->h_name);
    id->verbs->swinfo->masterhost = strdup(host->h_name);
    debug(2, "Connect to %s On port num is %d",  id->verbs->swinfo->masterhost, id->verbs->swinfo->port);

    /*set the event to occur next and set the CM ID that event would be created on*/
    id->channel->next_event = RDMA_CM_EVENT_ADDR_RESOLVED;
    id->channel->cm_id = id;


    //~ free(host); /*XXX - why can't we free this*/

    return check_open(id->verbs);







}

int rdma_get_cm_event(struct rdma_event_channel *channel, struct rdma_cm_event **event){

    /*set the next state up in here, since no valid cm_id to pass to the other RDMA CM functions*/

    struct rdma_cm_event *temp_cm_event;
    temp_cm_event = malloc(sizeof(struct rdma_cm_event));
	int ret;

    if(channel->next_event == RDMA_CM_EVENT_CONNECT_REQUEST){
	debug(4, "RDMA_CM_EVENT_CONNECT_REQUEST");

	/*block until we get someone trying to connect*/
	debug(4, "socket is %d", channel->fd);
	fd_set readfds, writefds, exceptfds;

	/*set up fd sets for select call*/
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	FD_SET(channel->fd, &readfds);

	/*set up sleep time*/


	/*now lets block until someone tries to connect to socket*/
	for(;;){
	    ret = select(channel->fd+1, &readfds, &writefds, &exceptfds, 0); /*XXX TODO: Fix this set 0 into timeval and use
										that as the last param this gets interp. as NULL
										which means block forever possibly only block
										for a certain amount of time before failing*/
	    if(ret)
		break;
	    usleep(1000);
	}
	if(ret < 0){
	    debug(0, "Problem waiting for soemone to connect");
	    ret = errno;
	}
    }

    if (channel->next_event == RDMA_CM_EVENT_ESTABLISHED && channel->conn_event_valid == 0) {
	debug(4, "RDMA_CM_EVENT_DISCONNECTED");
	channel->next_event = RDMA_CM_EVENT_DISCONNECTED;
	char buf;
	for(;;){
		/*if try to read and get ERR then socket is closed*/
		ret = read(channel->fd, &buf, 1);
		if(ret < 0){
			debug(4, "Read returned NEG, socket must be closed");
			break;
		} else {
			debug(4, "Read retruend %d", ret);
			continue;
			}
	}
    }

    if(channel->next_event == RDMA_CM_EVENT_ADDR_RESOLVED){
	debug(4, "RDMA_CM_EVENT_ADDR_RESOLVED");
    }

    if(channel->next_event == RDMA_CM_EVENT_ROUTE_RESOLVED){
	debug(4, "RDMA_CM_EVENT_ROUTE_RESOLVED");
    }

    if(channel->next_event == RDMA_CM_EVENT_ESTABLISHED && channel->conn_event_valid == 1) {
	debug(4, "RDMA_CM_EVENT_ESTABLISHED");
	channel->conn_event_valid = 0;  /*only valid to get 1 time*/

    }

    temp_cm_event->event = channel->next_event;
    temp_cm_event->id = channel->cm_id;

    *event = temp_cm_event;


    return 0;
}

int rdma_resolve_route(struct rdma_cm_id *id, IGNORE int timeout_ms){

    debug(2, "Connect to %s On port num is %d",  id->verbs->swinfo->masterhost, id->verbs->swinfo->port);

    id->channel->next_event = RDMA_CM_EVENT_ROUTE_RESOLVED;
    id->channel->cm_id = id;

    return 0;
}

int rdma_connect(struct rdma_cm_id *id, IGNORE struct rdma_conn_param *conn_param){
    char remote_private_data[200];
    int ret;


	//~ printf("connecting to %s on %d\n", id->verbs->swinfo->masterhost, id->verbs->swinfo->port);
    /*assumes CQ, PD, QP, and all that stuff is already created*/
    ret = iwarp_qp_active_connect(id->verbs->swinfo->rnic_hndl, id->verbs->swinfo->port,
				id->verbs->swinfo->masterhost, CONN_SLEEP, MAX_RETRIES,  id->qp->sw_qp,
				  "client", remote_private_data, 200);

    if(ret != IWARP_OK)
	    return -1;


    id->channel->next_event = RDMA_CM_EVENT_ESTABLISHED;
    id->channel->cm_id = id;

    return 0;
}

int rdma_accept(struct rdma_cm_id *id, IGNORE struct rdma_conn_param *conn_param){
    struct sockaddr_in passive_socket;
    socklen_t passive_s_len = sizeof(struct sockaddr_in);
    iwarp_rnic_t *rnic_ptr;
    of_sw_info_t *myinfo;
    iwarp_qp_handle_t qp_id;
    char private_data[200];
    char remote_private_data[200];
    int ret;
    iwarp_rnic_query_attrs_t attrs;

    //~ myinfo->rnic_hndl = -1;

    myinfo = id->verbs->swinfo;  /*save some typing*/
    rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(myinfo->rnic_hndl);
    qp_id = id->qp->sw_qp;


    debug(2,"in rdma_accept rnic handle is %ld going to try to accept on %d", myinfo->rnic_hndl, myinfo->fd);


    ret = iwarp_rnic_query(myinfo->rnic_hndl, &attrs);
    if(ret != IWARP_OK){
	debug(0, "Unable to query rnic");
	return ret;
    }
    free(attrs.vendor_name);

    rnic_ptr->qp_index[qp_id].socket_fd = accept(myinfo->fd, (struct sockaddr *) &passive_socket, &passive_s_len);
    if(rnic_ptr->qp_index[qp_id].socket_fd < 0){
	debug(0, "Can't accept connection, accept (2) failed", iwarp_string_from_errno(IWARP_CAN_NOT_ACCEPT_CONNECTION));
	goto GET_OUT;
    }

    debug(5, "the cqs are %p", rnic_ptr->qp_index[qp_id].attributes);
    debug(5, "just accepted  socket %d from listening socket %d",rnic_ptr->qp_index[qp_id].socket_fd, myinfo->fd);


    /*register socket with rdmap*/
    memset(&private_data, '\0', 200);  /*XXX neteffect does not like private data so don't send it now*/
    ret = v_rdmap_register_connection(rnic_ptr, qp_id, private_data, remote_private_data, 200, IWARP_PASSIVE_SERVER);
    if(ret){
	debug(0,"unable to register connection with RDMAP layer");
	goto GET_OUT;
    }

     /*mark the QP as connected*/
    rnic_ptr->qp_index[qp_id].connected = TRUE;

    /*dispatch anything that was preposted to the recv workQ*/
    ret = iwarp_recv_event_dispatcher(rnic_ptr, &rnic_ptr->qp_index[qp_id], &rnic_ptr->recv_q);
    if (ret){
	debug(0, "unable to dispatch recvs", iwarp_string_from_errno(IWARP_RECV_DISPATCHING_FAILURE));
	goto GET_OUT;
    }


    id->channel->next_event = RDMA_CM_EVENT_ESTABLISHED;
    id->channel->cm_id = id;


    return 0;


GET_OUT:
    id->channel->next_event = RDMA_CM_EVENT_REJECTED;
    id->channel->cm_id = id;
    return IWARP_CAN_NOT_ACCEPT_CONNECTION;

}

int rdma_ack_cm_event(struct rdma_cm_event *event){
    /*the only thing to do is free the memory associated with this*/
    free(event);


    return 0;
}

int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,  struct ibv_qp_init_attr *qp_init_attr){

    iwarp_qp_attrs_t qp_attrs;
    int ret;
    struct ibv_qp *cm_qp;

    /*this is how we create the QP for the client, assumes PD, CQ and all that stuff already established
    issue is how to handle this when QP wants to be set up BEFORE all that stuff like with server
    when it needs to listen for a connection before doing this stuff*/

    if(pd->context->swinfo != id->verbs->swinfo){
	debug(0, "prot domain does not match cm id");
	return -1;
    }

    /*the only qp attributes we really care about are send/recv cq and max WR and SGE ignore the rest*/
    qp_attrs.sq_cq = qp_init_attr->send_cq->sw_cq;
    qp_attrs.rq_cq = qp_init_attr->recv_cq->sw_cq;
    if(qp_init_attr->cap.max_send_wr > MAX_WRQ || qp_init_attr->cap.max_recv_wr > MAX_WRQ){
	debug(0, "Can't supply enough slots in work requests");
	return -1;
    }
    qp_attrs.sq_depth = qp_init_attr->cap.max_send_wr;
    qp_attrs.rq_depth = qp_init_attr->cap.max_recv_wr;
    qp_attrs.rdma_r_enable = 1;
    qp_attrs.rdma_w_enable = 1;
    qp_attrs.bind_mem_window_enable = 0;
    if(qp_init_attr->cap.max_send_sge > MAX_S_SGL || qp_init_attr->cap.max_recv_sge > MAX_S_SGL){
	debug(0, "Can't supply enough slots in sgls");
	return -1;
    }
    qp_attrs.send_sgl_max = qp_init_attr->cap.max_send_sge;
    qp_attrs.rdma_w_sgl_max = MAX_RDMA_W_SGL;  /*Max rdma sgl is at least as big as send/recl sgls so this is ok*/
    qp_attrs.recv_sgl_max = qp_init_attr->cap.max_recv_sge;
    qp_attrs.ord = MAX_ORD;
    qp_attrs.ird = MAX_IRD;
    qp_attrs.prot_d_id = pd->sw_pd;
    qp_attrs.zero_stag_enable = 0;
    qp_attrs.disable_mpa_markers = 1;
    qp_attrs.disable_mpa_crc = 1;

    cm_qp = malloc(sizeof(struct ibv_qp));
    id->qp = cm_qp;

    ret = iwarp_qp_create(id->verbs->swinfo->rnic_hndl, &qp_attrs, &(id->qp->sw_qp));
    if (ret){
	debug(0, "Error creating QP %s",iwarp_string_from_errno(ret));
	return -1;
    }

    id->qp->context = id->verbs;  /*link QP to the swinfo struct*/

    return 0;

}

int rdma_destroy_id(struct rdma_cm_id *id){


    free(id->verbs);
    free(id->swinfo);
    //~ free(id->channel);  /*there is a verb to specifically do this*/
    //~ free(id->qp);
    free(id);


    return 0;

}

int rdma_disconnect(struct rdma_cm_id *id) {
	int ret;

	ret =  iwarp_qp_disconnect(id->qp->context->swinfo->rnic_hndl, id->qp->sw_qp);
	if(ret){
		debug(0, "Unable to disconnect the QP");
		return -1;
	}
	return ret;
}

void rdma_destroy_qp(struct rdma_cm_id *id) {
	int ret;


	ret = iwarp_qp_destroy(id->qp->context->swinfo->rnic_hndl, id->qp->sw_qp);
	if(ret){
		debug(0, "Unable to destroy QP");

	}

	free(id->qp);

}





void rdma_destroy_event_channel(struct rdma_event_channel *channel){
    free(channel);

}

/*----------------------------------------------*/
/*----------------------------------------------*/
/*Supported OpenFabrics Verbs*/
/*----------------------------------------------*/
/*----------------------------------------------*/

struct ibv_pd *ibv_alloc_pd(struct ibv_context *context){

    int ret;
    struct ibv_pd *pd;
    int sw_iwarp_pd;

    //~ if(context->swinfo->pd_ready == 1){ /*already been allocated in listen connection above so use that*/
	//~ sw_iwarp_pd = context->swinfo->prot_id;
	//~ context->swinfo->pd_ready = 0;
    //~ }
    //~ else{
    ret = iwarp_pd_allocate(context->swinfo->rnic_hndl, &sw_iwarp_pd);
    if (ret){
	debug(0, "Error creating PD %s", iwarp_string_from_errno(ret));
	return NULL;
    }
    //~ }
    pd = malloc(sizeof(struct ibv_pd));

    pd->context = context;
    pd->sw_pd = sw_iwarp_pd;

    return pd;

}

int ibv_query_device(IGNORE struct ibv_context *context,  struct ibv_device_attr *device_attr){

    /*we don't support very many attributes in SW iWARP so just make something up
    for the heck of it fill in what we do know though*/

    memset(device_attr, 0, sizeof(struct ibv_device_attr));

    device_attr->max_qp = MAX_QP;
    device_attr->max_qp_wr = MAX_WRQ;
    device_attr->max_sge = MAX_SGE;
    device_attr->max_cq = OF_MAX_CQ;  /*defined in OpenFab verbs.h*/
    device_attr->max_cqe = MAX_CQ_DEPTH;
    device_attr->max_pd = MAX_PROT_DOMAIN;



    return 0;

}

struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,  IGNORE void *cq_context,
					    IGNORE struct ibv_comp_channel *channel,  IGNORE int comp_vector){


    int ret;
    struct ibv_cq *of_cq;

    if(cqe > MAX_CQ_DEPTH)
	return NULL;

    of_cq = malloc(sizeof(struct ibv_cq));

    ret = iwarp_cq_create(context->swinfo->rnic_hndl, NULL, cqe, &(of_cq->sw_cq));
    if (ret){
	debug(0, "Error creating CQ %s", iwarp_string_from_errno(ret));
	return NULL;
    }

    of_cq->context = context;

    return of_cq;

}

struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr,  size_t length, enum ibv_access_flags access){

    int ret;
    int flags = 0;
    iwarp_stag_index_t stag;
    iwarp_mem_desc_t mem_region;
    struct ibv_mr *of_mr;


    /*figure out what to pass as access flags, only worry about 3 of OpenFabrics access types*/
    //~ IBV_ACCESS_LOCAL_WRITE		= 1,
    //~ IBV_ACCESS_REMOTE_WRITE		= (1<<1),
    //~ IBV_ACCESS_REMOTE_READ		= (1<<2),

    /*local write is always a given for iWARP the question is if there is REMOTE READ and REMOTE WRITE*/
    if(access & IBV_ACCESS_REMOTE_WRITE){
	/*then has remote write capabilities*/
	debug(2, "Has remote Write");
	flags= flags | REMOTE_WRITE;
    }

    if(access & IBV_ACCESS_REMOTE_READ){
	/*ten has remote read capabilities*/
	debug(2, "Has remote Read");
	flags = flags | REMOTE_READ;
    }
    ret = iwarp_nsmr_register(pd->context->swinfo->rnic_hndl, VA_ADDR_T, addr,
				    length, pd->sw_pd, 0, flags, &stag, &mem_region);
    if(ret){
	debug(0, "Error registering memory %s", iwarp_string_from_errno(ret));
	return NULL;
    }

    of_mr = malloc(sizeof(struct ibv_mr));

    /*set up the OF mr data structure*/

    of_mr->context = pd->context;
    of_mr->pd = pd;
    of_mr->lkey=stag;
    of_mr->rkey=stag;

    //~ of_mr->sw_mr = malloc(sizeof(iwarp_mem_desc_t));

    //~ memcpy(of_mr->sw_mr, &mem_region, sizeof(iwarp_mem_desc_t));

    of_mr->sw_mr=mem_region;

    //~ debug(2, "sw_mr is %p - %p", of_mr->sw_mr, mem_region);

    //~ sleep(5);

    return of_mr;

}

int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, IGNORE struct ibv_recv_wr **bad_wr){
    int ret;
    iwarp_wr_t rq_wr;
    iwarp_sge_t sge;  /*scatter gather entry*/
    iwarp_sgl_t sgl;  /*the scatter gather list*/

    if(wr->num_sge != 1){
	debug(0, "Unable to support %d SGEs only support 1", wr->num_sge);
    }

    /*first need to make a SGL*/
    ret = iwarp_create_sgl(qp->context->swinfo->rnic_hndl, &sgl);
    if(ret){
	debug(0, "Unable to create SGL: %s", iwarp_string_from_errno(ret));
	return ret;
    }

    /*set up SGE*/
    sge.length = wr->sg_list->length;
    sge.stag = wr->sg_list->lkey; /*assumes STag and lkey are compatible with each oher */
    sge.to = wr->sg_list->addr;

    /*add SGE to the SGL*/
    ret = iwarp_register_sge(qp->context->swinfo->rnic_hndl, &sgl, &sge);
    if(ret){
	debug(0, "Unable to register SGE with SGL: %s", iwarp_string_from_errno(ret));
	return ret;
    }

    debug(2, "created sgl, sge, and added sge to sgl");

    rq_wr.wr_id = wr->wr_id;
    rq_wr.sgl = &sgl;
    rq_wr.wr_type = IWARP_WR_TYPE_RECV;
    rq_wr.cq_type = SIGNALED;

    ret = iwarp_qp_post_rq(qp->context->swinfo->rnic_hndl, qp->sw_qp,  &rq_wr);
    if(ret){
	debug(0, "Unable to post recv to rq: %s", iwarp_string_from_errno(ret));
	return ret;
    }
    debug(2, "posted rq");
    debug(2, "after posting RQ SGL count is %d", rq_wr.sgl->sge_count);
    return 0;
}

int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, IGNORE struct ibv_send_wr **bad_wr){

    int ret;
    iwarp_wr_t sq_wr;
    iwarp_sge_t sge, remote_sge;  /*scatter gather entry*/
    iwarp_sgl_t sgl, remote_sgl;  /*the scatter gather list*/

    if(wr->num_sge != 1){
	debug(0, "Unable to support %d SGEs only support 1", wr->num_sge);
    }

    /*first need to make a SGL*/
    ret = iwarp_create_sgl(qp->context->swinfo->rnic_hndl, &sgl);
    if(ret){
	debug(0, "Unable to create SGL: %s", iwarp_string_from_errno(ret));
	return ret;
    }

    /*set up SGE*/
    sge.length = wr->sg_list->length;
    sge.stag = wr->sg_list->lkey; /*assumes STag and lkey are compatible with each oher */
    sge.to = wr->sg_list->addr;

    /*add SGE to the SGL*/
    ret = iwarp_register_sge(qp->context->swinfo->rnic_hndl, &sgl, &sge);
    if(ret){
	debug(0, "Unable to register SGE with SGL: %s", iwarp_string_from_errno(ret));
	return ret;
    }

    debug(2, "created sgl, sge, and added sge to sgl");

    sq_wr.wr_id = wr->wr_id;
    sq_wr.sgl = &sgl;

    /*NEED TO HANDLE RDMA NOW TOO*/
    if(wr->opcode == IBV_WR_SEND){
	sq_wr.wr_type = IWARP_WR_TYPE_SEND;
    }
    else if(wr->opcode == IBV_WR_RDMA_WRITE){
	sq_wr.wr_type = IWARP_WR_TYPE_RDMA_WRITE;

	/*now need to create remote SGL*/
	if(wr->num_sge != 1){
	    debug(0, "Unable to support %d SGEs only support 1", wr->num_sge);
	}

	/*first need to make a SGL*/
	ret = iwarp_create_sgl(qp->context->swinfo->rnic_hndl, &remote_sgl);
	if(ret){
	    debug(0, "Unable to create SGL: %s", iwarp_string_from_errno(ret));
	    return ret;
	}

	/*set up SGE*/
	remote_sge.length = wr->sg_list->length;
	remote_sge.stag = wr->wr.rdma.rkey;
	remote_sge.to = wr->wr.rdma.remote_addr;

	/*add SGE to the SGL*/
	ret = iwarp_register_sge(qp->context->swinfo->rnic_hndl, &remote_sgl, &remote_sge);
	if(ret){
	    debug(0, "Unable to register SGE with SGL: %s", iwarp_string_from_errno(ret));
	    return ret;
	}

	debug(2, "created remote sgl, sge, and added sge to sgl");

	sq_wr.remote_sgl = &remote_sgl;
    }
    else{
	debug(0, "Unsupported operation type");
	return -1;
    }

    if(wr->send_flags == IBV_SEND_SIGNALED)
	sq_wr.cq_type = SIGNALED;
    else
	sq_wr.cq_type = UNSIGNALED;

    ret = iwarp_qp_post_sq(qp->context->swinfo->rnic_hndl, qp->sw_qp,  &sq_wr);
    if(ret){
	debug(0, "Unable to post send to sq: %s", iwarp_string_from_errno(ret));
	return ret;
    }
    debug(2, "posted sq");
    debug(2, "after posting SQ SGL count is %d", sq_wr.sgl->sge_count);



    return 0;
}

int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc){
    /*return 0 when nothing, -1 when error and 1 when we got something*/
    /*we are only going to pull off 1 each time, let the application handle
    getting multiple ones*/

    int ret;
    iwarp_work_completion_t iw_wc;

    iw_wc.status = -1;
    iw_wc.wr_type = -1;

    if(num_entries != 1){
	debug(0, "Only can handle 1 wc at a time");
	return -1;
    }

    ret = iwarp_cq_poll(cq->context->swinfo->rnic_hndl, cq->sw_cq, IWARP_INFINITY, 0, &iw_wc);
    if(ret){
	//~ debug(0, "perhaps got nothing if there was an error that is an event and let it get picked up below");
	return 0;
    }

    /*we'll set status and opcode the rest,,,,,, who cares about*/

    wc->wr_id = iw_wc.wr_id;

    if(iw_wc.status == IWARP_WR_SUCCESS)
	wc->status = IBV_WC_SUCCESS;
    else
	wc->status = IBV_WC_FATAL_ERR;


    switch(iw_wc.wr_type){
	case IWARP_WR_TYPE_SEND:
	    wc->opcode = IBV_WC_SEND;
	break;

	case IWARP_WR_TYPE_RECV:
	    wc->opcode = IBV_WC_RECV;
	break;

	case IWARP_WR_TYPE_RDMA_WRITE:
	    wc->opcode = IBV_WC_RDMA_WRITE;
	break;

	case IWARP_WR_TYPE_RDMA_READ:
	    wc->opcode = IBV_WC_RDMA_READ;
	break;

	default:
	    debug(0, "Unknown op type");
	    return -1;
    }

    return 1;
}

int ibv_dereg_mr(struct ibv_mr *mr){
    int ret;
    //~ debug(2, "sw_mr is %p ", mr->sw_mr);

    if(mr->lkey != mr->rkey){
	debug(0, "For SW iWARP mode pkey MUST EQUAL rkey");
	return-1;
    }

    ret =  iwarp_deallocate_stag(mr->context->swinfo->rnic_hndl, mr->lkey);
    if(ret)
	debug(0, "Unable to deallocate stag thus won't be able to deregister mem");

    ret = iwarp_deregister_mem(mr->context->swinfo->rnic_hndl, mr->pd->sw_pd, mr->sw_mr);
    if(ret)
	debug(0, "couldn't deregister because %s", iwarp_string_from_errno(ret));

    free(mr);

    return ret;


}

int ibv_destroy_qp(struct ibv_qp *qp){
    int ret;

    ret =  iwarp_qp_disconnect(qp->context->swinfo->rnic_hndl, qp->sw_qp);
    if(ret){
	debug(0, "Unable to disconnect the QP");
	return -1;
    }

    ret = iwarp_qp_destroy(qp->context->swinfo->rnic_hndl, qp->sw_qp);
    if(ret){
	debug(0, "Unable to destroy QP");
	return -1;
    }

    free(qp);

    return 0;

}

int ibv_destroy_cq(struct ibv_cq *cq){
    int ret;

    ret = iwarp_cq_destroy(cq->context->swinfo->rnic_hndl, cq->sw_cq);
    if(ret){
	debug(0, "Can't destory CQ %s", iwarp_string_from_errno(ret));
	return -1;
    }

    free(cq);

    return 0;
}

int ibv_dealloc_pd(struct ibv_pd *pd){
    int ret;

    ret = iwarp_pd_deallocate(pd->context->swinfo->rnic_hndl, pd->sw_pd);
    if(ret){
	debug(0,"Can't deallocate PD");
	return -1;
    }

    free(pd);

    return 0;
}


int ibv_advance_sw_rnic(struct ibv_context *context){

    /*even for OpenFabrics still need to be able to advance the rnic*/

    return iwarp_rnic_advance(context->swinfo->rnic_hndl);
}

/*----------------------------------------------*/
/*----------------------------------------------*/
/*Private Functions for OpenFab*/
/*----------------------------------------------*/
/*----------------------------------------------*/

int check_open(struct ibv_context *context){
    int ret;



    if(context->swinfo->rnic_hndl == 0){
        ret = iwarp_rnic_open(0, PAGE_MODE, NULL, &context->swinfo->rnic_hndl);
	if(ret){
	    debug(0, "Error opening RNIC %s", iwarp_string_from_errno(ret));
	    return -1;
	}
	return 0;
    }
    else
	return 0;

}


