/*
* Implementation of the Queue Pair Interface for software iwarp
*
*$Id: qp.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include "verbs.h"


/*Notes
QP still needs to save completion queue handles in its attributes

*/


iwarp_status_t iwarp_qp_create(/*IN*/iwarp_rnic_handle_t rnic_hndl,
		        /*INOUT*/iwarp_qp_attrs_t *qp_attrs,
			/*OUT*/ iwarp_qp_handle_t *qp_id)
/*
Creat the QP for the user insert it into the QP array of the RNIC insert at qp_id
Attach a new qp_attrs struct to this QP so the user can destroy their data struct

We are not accepting out of bounds qp_attributes so qp_attrs is really just an IN
parameter not an INOUT
*/
{
    int i, index;

    /*get a pointer to the RNIC like usual*/
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);

    index = -1;
    /*find a free QP id*/
    for(i=0; i<MAX_QP; i++){
	if(rnic_ptr->qp_index[i].available == TRUE){
	    *qp_id = i;

	    //~ return IWARP_OK;
	    index = i;
	    break;
	}
    }
    if(index == -1)
	return IWARP_INSUFFICIENT_RESOURCES;


    rnic_ptr->qp_index[index].attributes = malloc(sizeof(iwarp_qp_attrs_t));

    if(rnic_ptr->qp_index[index].attributes == NULL)
	return IWARP_INSUFFICIENT_RESOURCES;


    /*check the attributes passed in and save them in the qp's data structure*/

    rnic_ptr->qp_index[index].attributes->sq_cq = qp_attrs->sq_cq;
    rnic_ptr->qp_index[index].attributes->rq_cq = qp_attrs->rq_cq;

    if(qp_attrs->sq_depth <= MAX_WRQ && qp_attrs->rq_depth <= MAX_WRQ){
	rnic_ptr->qp_index[index].attributes->sq_depth = qp_attrs->sq_depth;
	rnic_ptr->qp_index[index].attributes->rq_depth = qp_attrs->rq_depth;
    }
    else{
	debug(0, "sq depth > MAX WRQ or rq depth > MAX_WRQ");
	return IWARP_INVALID_QP_ATTR;
    }


    if(qp_attrs->rdma_r_enable == TRUE || qp_attrs->rdma_r_enable == FALSE)
	rnic_ptr->qp_index[index].attributes->rdma_r_enable = qp_attrs->rdma_r_enable;
    else{
	debug(0, "rdma_r_enable is true or rdma_r_enable  is false,, what the heck?");
	return IWARP_INVALID_QP_ATTR;
    }

    if(qp_attrs->rdma_w_enable == TRUE || qp_attrs->rdma_w_enable == FALSE)
	rnic_ptr->qp_index[index].attributes->rdma_w_enable = qp_attrs->rdma_w_enable;
    else{
	debug(0, "rdma_w_enable is TRUE or its FALSE, again, what in the world do we make this check for..");
	return IWARP_INVALID_QP_ATTR;
    }

    if(!BIND_MEM_WINDOW_ENABLE){ /*if we are not enablilng binding memory windows*/
	/*make sure the caller is not trying to enable it*/
	if(qp_attrs->bind_mem_window_enable == TRUE){
	    debug(0, "Can not bind memory window");
	    return IWARP_INVALID_QP_ATTR;
	}
    }
    rnic_ptr->qp_index[index].attributes->bind_mem_window_enable = qp_attrs->bind_mem_window_enable;

    if(qp_attrs->send_sgl_max <= MAX_S_SGL)
	rnic_ptr->qp_index[index].attributes->send_sgl_max = qp_attrs->send_sgl_max;
    else{
	debug(0, "Send sgl is > MAX_S_SGL");
	return IWARP_INVALID_QP_ATTR;
    }

    if(qp_attrs->rdma_w_sgl_max <= MAX_RDMA_W_SGL)
	rnic_ptr->qp_index[index].attributes->rdma_w_sgl_max = qp_attrs->rdma_w_sgl_max;
    else{
	debug(0, "rdma_w_sgl_max is smaller than MAX_RDMA_W_SGL");
	return IWARP_INVALID_QP_ATTR;
    }

    if(qp_attrs->recv_sgl_max <= MAX_R_SGL)
	rnic_ptr->qp_index[index].attributes->recv_sgl_max = qp_attrs->recv_sgl_max;
    else
	return IWARP_INVALID_QP_ATTR;

    if(qp_attrs->ord <= MAX_ORD && qp_attrs->ird <= MAX_IRD){
	rnic_ptr->qp_index[index].attributes->ord = qp_attrs->ord;
	rnic_ptr->qp_index[index].attributes->ird = qp_attrs->ird;
    }
    else{
	debug(0, "ord smaller than MAX_ORD and ird smaller than MAX_IRD");
	return IWARP_INVALID_QP_ATTR;
    }

    /*check to make sure the PD has been allocated*/
    if(rnic_ptr->pd_index[qp_attrs->prot_d_id].available == TRUE){
	debug(0, "Protection Domain was not allocated properly");
	return IWARP_INVALID_QP_ATTR;
    }

    rnic_ptr->qp_index[index].attributes->prot_d_id = qp_attrs->prot_d_id;

    if(!ENABLE_ZERO_STAG){ /*if we are not allowing enable zero stag make sure they are not trying to use it*/
	if(qp_attrs->zero_stag_enable == TRUE){
	    debug(0, "Trying to use zero STag which is unsupported");
	    return IWARP_INVALID_QP_ATTR;
	}
    }
    rnic_ptr->qp_index[index].attributes->zero_stag_enable = qp_attrs->zero_stag_enable;

    /*Finally makr the QP as not being available*/
    rnic_ptr->qp_index[index].available = FALSE;

    /*Mark the QP as not being connected*/
    rnic_ptr->qp_index[index].connected = FALSE;

    /*Mark Pre-connection posted recvs as being empty*/
    rnic_ptr->qp_index[index].pre_connection_posts = 0;

    /*User HAS to set what attributes the QPs will use for markers and CRC, can not rely on system to fill in 0's
            and can not make an assumption on what the user wanted*/
    rnic_ptr->qp_index[index].attributes->disable_mpa_markers = qp_attrs->disable_mpa_markers;
    rnic_ptr->qp_index[index].attributes->disable_mpa_crc     = qp_attrs->disable_mpa_crc;

    return IWARP_OK;


}


iwarp_status_t iwarp_qp_destroy(/*IN*/iwarp_rnic_handle_t rnic_hndl,
		       		/*OUT*/ iwarp_qp_handle_t qp_id)
/*
destroy the QP - need to free the memory assocaited with the qp's attributes
*/
{
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);

    int index = qp_id;

    if(rnic_ptr->qp_index[index].connected == TRUE) /*make sure its not already available*/
	return IWARP_CONNECTED_QP;

    if(index < 0 || index > MAX_QP) /*make sure the id is in the valid range*/
	return IWARP_INVALID_QP_ID;

    if(rnic_ptr->qp_index[index].available == TRUE) /*make sure its not already available*/
	return IWARP_INVALID_QP_ID;

    free(rnic_ptr->qp_index[index].attributes); /*free the memory*/

    rnic_ptr->qp_index[index].available = TRUE; /*Finally mark it as being available*/

    return IWARP_OK;
}

iwarp_status_t iwarp_qp_passive_connect(/*INOUT*/iwarp_rnic_handle_t rnic_hndl,
				    /*IN*/iwarp_port_t port, iwarp_qp_handle_t qp_id, const char private_data[],
				    /*OUT*/char *remote_private_data,
				    /*IN*/int rpd)
/*
passively wait for connection from remote side
this is the server in the client server model
*/
{
    //~ int flags = 1;
    //~ int err;
    iwarp_status_t ret;
    iwarp_rnic_query_attrs_t attrs;
    struct sockaddr_in passive_socket;
    socklen_t passive_s_len = sizeof(passive_socket);
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    int i;


    ret = iwarp_rnic_query(rnic_hndl, &attrs);

    if(ret != IWARP_OK)
	return ret;

    memset(&passive_socket, 0, passive_s_len);
    passive_socket.sin_family = attrs.address_type;
    free(attrs.vendor_name);  /* allocated by _query */
    /* do not bind on the interface IP; it prevents loopback tests */
    /* memcpy(&passive_socket.sin_addr, attrs.address, attrs.length); */
    passive_socket.sin_port = htons(port);
    rnic_ptr->qp_index[qp_id].socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (rnic_ptr->qp_index[qp_id].socket_fd < 0)
	return IWARP_CAN_NOT_BUILD_SOCKET;

    /*if we want to set socket as reusable probably not important*/
    //~ err = setsockopt(rnic_ptr->qp_index[qp_id].socket_fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    //~ if(err < 0){
	    //~ fprintf(stderr,"Warning: Unable to set Socket address to SO_REUSEADDR!\n");
	    //~ exit(1);
    //~ }

    /* okay to reuse same local port number */
    i = 1;
    if (setsockopt(rnic_ptr->qp_index[qp_id].socket_fd, SOL_SOCKET,
      SO_REUSEADDR, &i, sizeof(i)) < 0)
	return IWARP_CAN_NOT_BIND_SOCKET;

    /*Attempt to bind socket to port*/
    if (bind(rnic_ptr->qp_index[qp_id].socket_fd, (struct sockaddr *)&passive_socket, passive_s_len) < 0)
	return IWARP_CAN_NOT_BIND_SOCKET;

    /*Listen on the socket*/
    if (listen(rnic_ptr->qp_index[qp_id].socket_fd, 5) < 0)
	return IWARP_CAN_NOT_LISTEN_SOCKET;

  //~ printf("going to be listening on socket %d on port %d\n", rnic_ptr->qp_index[qp_id].socket_fd, port);

    /*Accept the connection*/
    passive_s_len = sizeof(passive_socket);
    rnic_ptr->qp_index[qp_id].socket_fd = accept(rnic_ptr->qp_index[qp_id].socket_fd, (struct sockaddr *) &passive_socket, &passive_s_len);
    if(rnic_ptr->qp_index[qp_id].socket_fd < 0)
	return IWARP_CAN_NOT_ACCEPT_CONNECTION;



    /*more socket options - set it to nonblocking- sticking with blocking for now, leave as comments for later*/
    //~ if(NONBLOCK){
	    //~ VERBOSE("Setting non blocking...");
	    //~ flags |= O_NONBLOCK;
	    //~ err = fcntl(socket_fd, F_SETFL, O_NONBLOCK);
	    //~ if(err != 0){
		    //~ fprintf(stderr,"FAILED setting nonblocking\n");
		    //~ exit(1);
	    //~ }
	    //~ VERBOSE("Done\n");
    //~ }


    //~ /*now we need to register the socket with the RDMAP layer*/
    ret = v_rdmap_register_connection(rnic_ptr, qp_id, private_data, remote_private_data, rpd, IWARP_PASSIVE_SERVER);




    /*move this code to stubs.c*/
    //~ ret = rdmap_register_sock(rnic_ptr->qp_index[qp_id].socket_fd, rnic_ptr->qp_index[qp_id].attributes->sq_cq, rnic_ptr->qp_index[qp_id].attributes->rq_cq);
    //~ if(ret != 0)
	//~ return IWARP_RDMAP_REGISTER_SOCKET_FAILURE;

    //~ ret = rdmap_mpa_use_markers(rnic_ptr->qp_index[qp_id].socket_fd,!rnic_ptr->qp_index[qp_id].attributes->disable_mpa_markers);
    //~ if(ret != 0)
	//~ return IWARP_RDMAP_SET_MARKER_FAILURE;

    //~ ret = rdmap_mpa_use_crc(rnic_ptr->qp_index[qp_id].socket_fd, !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_crc);
    //~ if(ret != 0)
	//~ return IWARP_RDMAP_SET_CRC_FAILURE;


    /*!! Now we need to do the CRC and Marker MPA negotiation - we also have the private data to pass on*/
    //~ ret = rdmap_init_startup(rnic_ptr->qp_index[qp_id].socket_fd, 0, private_data, remote_private_data, rpd);
    //~ if(ret != 0)
	    //~ return IWARP_MPA_INIT_FAILURE;






    /*mark the QP as connected*/
    rnic_ptr->qp_index[qp_id].connected = TRUE;

    /*dispatch anything that was preposted to the recv workQ*/
    ret = iwarp_recv_event_dispatcher(rnic_ptr, &rnic_ptr->qp_index[qp_id], &rnic_ptr->recv_q);

    //~ printf("pre connection posted requests are %d\n", rnic_ptr->qp_index[qp_id].pre_connection_posts);
    //~ printf("just dispatched %d of those\n", num_posted);

    if (ret)
	return IWARP_RECV_DISPATCHING_FAILURE;

    return IWARP_OK;


}

iwarp_status_t iwarp_qp_active_connect(/*INOUT*/iwarp_rnic_handle_t rnic_hndl,
				  /*IN*/iwarp_port_t port, const char *servername, int sleep_time, int retrys,
				  /*IN*/iwarp_qp_handle_t qp_id, const char private_data[],
				  /*OUT*/char *remote_private_data,
				  /*IN*/int rpd)
/*
actively try to connect to remote side
this is the client in the client server model
NOTE: sleep time is in microseconds
*/
{
    int  ret;
    //~ iwarp_rnic_query_attrs_t attrs;
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    struct hostent *host_info;
    struct sockaddr_in server_address, local_address;
    int k;
    int connected;

    //~ int flags = 1;
    //~ int err;

    host_info = gethostbyname(servername);


    if(host_info == NULL)
	return IWARP_SERVER_QUERY_FAILURE;


    server_address.sin_family = host_info->h_addrtype;

    memcpy((char *) &server_address.sin_addr.s_addr, host_info->h_addr_list[0], host_info->h_length);
    server_address.sin_port = htons(port);

    /*Open the socket*/
    rnic_ptr->qp_index[qp_id].socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (rnic_ptr->qp_index[qp_id].socket_fd < 0)
	return IWARP_CAN_NOT_BUILD_SOCKET;

    /*Bind socket to the port*/
    local_address.sin_family = PF_INET;
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);
    local_address.sin_port = htons(0);


    if( bind(rnic_ptr->qp_index[qp_id].socket_fd, (struct sockaddr *) &local_address, sizeof(local_address)) < 0)
	return IWARP_CAN_NOT_BIND_SOCKET;

    connected = 0;

    for(k=0; k<retrys; k++){
	ret = connect(rnic_ptr->qp_index[qp_id].socket_fd, (struct sockaddr *)&server_address, sizeof(server_address));
	if(ret < 0){
	    //~ printf("did not connect\n");

	    //~ perror("Reason for not connecting");

	    if(sleep_time > 0)   /*don't call usleep unless they want to sleep, even passing 0 causes it to waste time sleeping*/
		usleep(sleep_time);
	}
	else{
	    connected = 1;
	    break;
	}
    }

    if(connected){

	/*now we need to register the socket with the RDMAP layer*/
	ret = v_rdmap_register_connection(rnic_ptr, qp_id, private_data, remote_private_data, rpd, IWARP_ACTIVE_CLIENT);

	//~move this code to stubs.c
	//~ ret = rdmap_register_sock(rnic_ptr->qp_index[qp_id].socket_fd, rnic_ptr->qp_index[qp_id].attributes->sq_cq, rnic_ptr->qp_index[qp_id].attributes->rq_cq);
	//~ if(ret != 0)
	    //~ return IWARP_RDMAP_REGISTER_SOCKET_FAILURE;

	//~ ret = rdmap_mpa_use_markers(rnic_ptr->qp_index[qp_id].socket_fd, !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_markers);
	//~ if(ret != 0)
	    //~ return IWARP_RDMAP_SET_MARKER_FAILURE;

	//~ ret = rdmap_mpa_use_crc(rnic_ptr->qp_index[qp_id].socket_fd, !rnic_ptr->qp_index[qp_id].attributes->disable_mpa_crc);
	//~ if(ret != 0)
	    //~ return IWARP_RDMAP_SET_CRC_FAILURE;


	/*!! Now we need to do the CRC and Marker MPA negotiation - we also have the private data to pass on*/

	//~ ret = rdmap_init_startup(rnic_ptr->qp_index[qp_id].socket_fd, 1, private_data, remote_private_data, rpd);
	//~ if(ret != 0)
	    //~ return IWARP_MPA_INIT_FAILURE;





	/*mark the QP as connected*/
	rnic_ptr->qp_index[qp_id].connected = TRUE;

	/*dispatch anything that was preposted to the recv workQ*/
	ret = iwarp_recv_event_dispatcher(rnic_ptr, &rnic_ptr->qp_index[qp_id], &rnic_ptr->recv_q);

	//~ printf("pre connection posted requests are %d\n", rnic_ptr->qp_index[qp_id].pre_connection_posts);
	//~ printf("just dispatched %d of those\n", num_posted);

	if (ret)
	    return IWARP_RECV_DISPATCHING_FAILURE;

	return IWARP_OK;
    }
    else
	return IWARP_CAN_NOT_CONNECT;


}


iwarp_status_t iwarp_qp_disconnect(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_qp_handle_t qp_id)
/*
Close the connection between the two queue pairs and deregister the socket
*/
{
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    int err;
    int ret;



    /*Mark the QP as not being connected*/
    rnic_ptr->qp_index[qp_id].connected = FALSE;

    /*Now deregister the socket with RDMAP layer*/
    //~ ret = rdmap_deregister_sock(rnic_ptr->qp_index[qp_id].socket_fd);
    ret = v_rdmap_deregister_sock(rnic_ptr, rnic_ptr->qp_index[qp_id].socket_fd);
    if(ret != IWARP_OK)
	return IWARP_RDMAP_DEREGISTER_SOCKET_FAILURE;


    /*Actually close the connection*/
    err = close(rnic_ptr->qp_index[qp_id].socket_fd);
    if(err != 0)
	return IWARP_CLOSE_SOCKET_ERROR;



    return IWARP_OK;

}



