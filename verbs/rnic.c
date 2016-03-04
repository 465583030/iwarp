/*
* Implementation of the RNIC Interface for software iwarp
*
*$Id: rnic.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*/

#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include "verbs.h"


#include <stdio.h>


iwarp_status_t iwarp_rnic_open (/*IN*/int index, iwarp_pblmode_t mode, iwarp_context_t context,
			       /*OUT*/iwarp_rnic_handle_t* rnic_hndl)
/*
Opens access to the RNIC (we have no real RNIC so this is an abstract RNIC) just do some simple init things
*/
{
    iwarp_rnic_t *rnic;
    int i;
    int ret;
    iwarp_status_t status;


    v_mem_init();

    ret = v_rdmap_init();
    if(ret != 0)
	return IWARP_RDMAP_INIT_FAILURE;


    if(index != 0)
	return IWARP_INVALID_MODIFIER;

    /*we can force the page mode though*/
    if(mode != PAGE_MODE)
	return IWARP_BLOCK_LIST_NOT_SUPPORTED;



    /*if NIC is in use we don't care since this is all software implenetation*/

    /*context is useless, just pass becuase Spec says so*/
    /*it is useless because we have no dev to open and thus no need to use ioctl which is
            what that context is useful for*/
    ignore(context); /*hack to avoid compiler blather*/

    /*Create and allocate the data struct to hold rnic info*/
    rnic = malloc(sizeof(iwarp_rnic_t));
    if(rnic == NULL)
	return IWARP_INSUFFICIENT_RESOURCES;

    /*Do some initializations - we are attaching various objects to RNIC object*/

    /*init prot domain array*/
    for(i=0; i<MAX_PROT_DOMAIN; i++){
	rnic->pd_index[i].available = TRUE;

    }

    for(i=0; i<MAX_QP; i++){
	rnic->qp_index[i].available = TRUE;
    }




    /* initialize the work request queue */
    rnic->recv_q.size = 0;




    /*for kernel version we really do have a "device" so we need to stub this*/
    status = v_RNIC_open(index, rnic);
    if(status != IWARP_OK)
	return IWARP_UNABLE_TO_OPEN_RNIC;




    *rnic_hndl = int64_from_ptr(rnic);


    return IWARP_OK;

}


iwarp_status_t iwarp_rnic_query(/*IN*/iwarp_rnic_handle_t rnic,
			        /*OUT*/iwarp_rnic_query_attrs_t *attrs)
/*
Query the status, settings, limits, etc of the RNIC
Mainly used to get hostname if we need anything else from the hostent structure get it from here
*/
{
    // char my_hostname[HOST_MAX];
    // int ret;
    // struct hostent *host_info;

    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic);

    //~ printf("we got for fd %d\n", rnic_ptr->fd);
    //~ ignore(rnic);



    /*vendor name*/
    attrs->vendor_name = strsave(VENDOR_NAME);

    /*version*/
    attrs->version = VERSION;

    /*max qps*/
    attrs->max_qp = MAX_QP;

    /*max outstanding WRQs*/
    attrs->max_wrq = MAX_WRQ;

    /*max outstanding shared request queues - NOT SUPPORTED so 0*/
    attrs->max_srq = 0;

    /*what the fd we are using is*/
    attrs->fd = rnic_ptr->fd;


    /*TODO: re-enable this stuff not drastically important though*/
    /*The important Information*/
    //~ ret = gethostname(my_hostname, HOST_MAX);
    //~ if(ret == -1)
	//~ return IWARP_RNIC_QUERY_FAILURE;
    //~ strcpy((char *)attrs->local_hostname, (char *)my_hostname);

    //~ host_info = gethostbyname(my_hostname);

    //~ if(host_info == NULL)
	//~ return IWARP_RNIC_QUERY_FAILURE;


    //~ /*Put hostname in struct*/
    //~ strcpy(attrs->official_hostname, host_info->h_name);

    //~ /*Put address in the struct*/
    //~ attrs->address_type = host_info->h_addrtype;

    //~ /*Put the length in the struct*/
    //~ attrs->length = host_info->h_length;

    //~ /*Now the address needs to go as well*/
    //~ attrs->address = host_info->h_addr_list[0];




    return IWARP_OK;

}

iwarp_status_t iwarp_rnic_close(/*IN*/ iwarp_rnic_handle_t rnic_hndl)
/*
Close the iWARP - do whatever else needs to be done to tell DDP/RDMAP that we are done
*/
{
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);

    v_rnic_close(rnic_ptr);

    free(rnic_ptr);  /*probbly a better thing to do,, prone to seg faults when we start adding and forgetting stuff*/

    return IWARP_OK;
}

iwarp_status_t iwarp_nsmr_register(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_addr_t addr_t, void *buffer,
				    /*IN*/uint32_t length, iwarp_prot_id pd, iwarp_stag_key_t stag_key,
				    /*IN*/iwarp_access_control_t access_flags,
				    /*OUT*/iwarp_stag_index_t *stag_index, iwarp_mem_desc_t *mem_region)
/*
Register a region of memory with the mem layer and create an STag
Assumes the memory has already been allocaetd by the user!
*/
{
    iwarp_status_t ret;
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    //~ iwarp_mem_desc_t mem_region;  /*pointer to memory region just typedef'd a few times*/

    if(addr_t != VA_ADDR_T)
	return IWARP_UNSUPPORTED_ADDRESS_TYPE;

    /*register memory region with the memory module*/
    ret = v_mem_register(rnic_ptr, buffer, length, mem_region);
    if(ret != IWARP_OK)
	return IWARP_MEMORY_REGISTRATION_FAILURE;


    /*TODO: Remove this from here and make it its own verb*/
    /*create the STag for this memory region - NOTE this is not the STag as specified in iWarp spec!*/
    ret = v_mem_stag_create(rnic_ptr, mem_region, buffer, length, access_flags, pd, stag_index);
    if(ret != IWARP_OK)
	return IWARP_STAG_REGISTRATION_FAILURE;

    /*mark the pd as being bound to a new memory region*/
    rnic_ptr->pd_index[pd].in_use++;

    ignore(stag_key); /*Since we are not creating a Spec compliant STag we can ignore the key that was passed in - to bad this is useful for debugging*/

    return IWARP_OK;
}


iwarp_status_t iwarp_deallocate_stag(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_stag_index_t stag_index)
/*
Deallocate the stag specified, no verb for reregistering memory

TODO: Need to combine deallocate and deregister as one single call

*/
{
    int ret;
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    ignore(rnic_hndl);

    ret = v_mem_stag_destroy(rnic_ptr, stag_index);
    if(ret < 0)
	return IWARP_UNABLE_DESTROY_STAG;

    return IWARP_OK;

}

iwarp_status_t iwarp_deregister_mem(/*IN*/iwarp_rnic_handle_t rnic_hndl, iwarp_prot_id pd, iwarp_mem_desc_t mem_region)
/*
Added verb to deregister a mem region
*/
{
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);
    int ret;

    ret = v_mem_deregister(rnic_ptr, mem_region);

    if(ret < 0)
	return IWARP_INVALID_MEM_REGION;
    else {
	--rnic_ptr->pd_index[pd].in_use;
	return IWARP_OK;
    }

}


iwarp_status_t iwarp_create_sgl(/*IN*/iwarp_rnic_handle_t rnic_hndl,
				/*OUT*/iwarp_sgl_t *sgl)
/*
Creat the scatter gather list, just initialize data structs and the counts, not really much use
in current implementation but helps make it extendible later
*/
{

    /*SGL has an array for its list of SGEs so no need to malloc anything*/


    ignore(rnic_hndl);
    sgl->sge_count = 0;
    return IWARP_OK;

}

iwarp_status_t iwarp_register_sge(/*IN*/iwarp_rnic_handle_t rnic_hndl,
				/*INOUT*/iwarp_sgl_t *sgl,
				/*IN*/ iwarp_sge_t *sge)
/*
Insert the sge into the sgl
*/
{
    if(sgl->sge_count == MAX_SGE) /*no more room in the SGL*/
	return IWARP_SGL_FULL;

    /*Again sgl->sge is an array so we don't need anything malloc'd*/

    sgl->sge[sgl->sge_count].length = sge->length;  /*don't depend on users sge*/
    sgl->sge[sgl->sge_count].stag = sge->stag;
    sgl->sge[sgl->sge_count].to = sge->to;



    sgl->sge_count++;


    ignore(rnic_hndl);
    return IWARP_OK;
}

iwarp_status_t iwarp_rnic_advance(iwarp_rnic_handle_t rnic_hndl)
/*
Since we don't want to export rdmap_poll() to the user we can use this to abstract that call we need to introduce this
because we don't have a thread to actively read the socket as data comes in.  We call rdmap_poll to see what data is
in the buffers and then place it.
*/
{
    int ret;
    iwarp_rnic_t *rnic_ptr = ptr_from_int64(rnic_hndl);
    ret = v_rnic_advance(rnic_ptr);

    if(ret != IWARP_OK)
	return IWARP_RDMAP_POLL_FAILURE;
    else
	return IWARP_OK;

}


