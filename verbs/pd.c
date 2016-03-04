/*
*Functions for creating and maintaining
*Protection Domains
*
*$Id: pd.c 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*/
#include "verbs.h"

/*Notes:


*/


iwarp_status_t iwarp_pd_allocate(/*IN*/iwarp_rnic_handle_t rnic_hndl,
                                 /*OUT*/iwarp_prot_id *prot_id)
/*
Allocate a protection domain, we are attaching this to the object that rnic_hndl points too
We are also using prot_id as the index into the array of protection domains pointed to by the
rnic_hndl
*/
{
    int i;

    /*Look through the protection domains and find one that is available*/
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);

    for(i=0; i<MAX_PROT_DOMAIN; i++){
	if(rnic_ptr->pd_index[i].available == 1){
	    *prot_id = i;
	    rnic_ptr->pd_index[i].available = 0;
	    rnic_ptr->pd_index[i].in_use = 0;
	    return IWARP_OK;
	}
    }

    return IWARP_INSUFFICIENT_RESOURCES;
}



iwarp_status_t iwarp_pd_deallocate(/*IN*/iwarp_rnic_handle_t rnic_hndl,
                                   /*IN*/iwarp_prot_id prot_id)
/*
Deallocate a protection domain, basically just change its available status
*/
{
    iwarp_rnic_t *rnic_ptr = (iwarp_rnic_t *)ptr_from_int64(rnic_hndl);
    int index = prot_id;

    if(index < 0 || index > MAX_PROT_DOMAIN)
	return IWARP_INVALID_PD_ID;


    if(rnic_ptr->pd_index[index].in_use > 0)
	return IWARP_PD_INUSE;


    if(rnic_ptr->pd_index[index].available == 1)
	return IWARP_INVALID_PD_ID;


    rnic_ptr->pd_index[index].available = 1;

    return IWARP_OK;

}







