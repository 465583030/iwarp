 /*
 * Verbs Testing Program
 *
 * $Id: verbsTest.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

/*To Do

Implement the rest of the RNIC limits Pg 161 of spec has them defined as does page 7 of ammasso ccil docs
	just really boring and tedious and not really much use for us

Check for RNIC in use with a global variable



*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> /*for uint64_t*/
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "../verbs.h"

static void usage(void)
{
    	fprintf(stderr, "Usage: %s s <port>\n", progname);
    	fprintf(stderr, "   or  %s c <port> <server>\n", progname);
	exit(1);
}

int main(int argc, char **argv)
{


    int failure = 0;
    int connected = 0;
    int rounds = 0;
    iwarp_status_t ret;
    iwarp_context_t context = NULL;  /*user data structure to be passed around to functions*/
    iwarp_rnic_query_attrs_t attrs;
    iwarp_rnic_handle_t rnic_hndl;
    iwarp_prot_id prot_id;
    int i;

    iwarp_qp_attrs_t qp_attrs;
    iwarp_qp_handle_t qp_id;

    iwarp_cq_handle_t  cq_hndl;
    uint32_t cq_evts;

    int *temp = NULL;

    char *buffer;

    int length = 1024;
    iwarp_stag_index_t stag_index;

    iwarp_mem_desc_t mem_region;

    iwarp_sge_t sge;  /*scatter gather entry*/
    iwarp_sgl_t sgl;  /*the scatter gather list*/
    iwarp_sgl_t remote_sgl; /*scatter gather list for RDMA*/
    iwarp_sge_t remote_sge;  /*remote hosts info*/

    iwarp_wr_t swr;
    iwarp_wr_t rwr;
    iwarp_wr_t rdma_w_wr;

    int server;

    iwarp_work_completion_t wc;

    char *remote_priv_data;

    set_progname(argc, argv);
    temp = malloc(sizeof(int *));
    *temp = 2;


    cq_evts =  MAX_CQ_DEPTH;


    printf("------------------------------------\n");
    printf("RNIC Interface\n");
    printf("------------------------------------\n");


{  //START RNIC Section
    printf("Testing Open RNIC call.....\n");

    printf(".....Testing for invalid index (should be 0).....");
    ret = iwarp_rnic_open (1, PAGE_MODE, context, &rnic_hndl);
    if(ret == IWARP_INVALID_MODIFIER)
	printf("PASS\n");
    else{
	printf("Failed!\n");
	failure++;
    }

    printf(".....Testing for invalid Physical Buffer Mode (only allow BLOCK).....");
    ret = iwarp_rnic_open (0, BLOCK_MODE, context, &rnic_hndl);
    if(ret == IWARP_BLOCK_LIST_NOT_SUPPORTED)
	printf("PASS\n");
     else{
	printf("Failed!\n");
	failure++;
    }

    //~ printf(".....Testing for correct return.....");

    //~ if(ret != IWARP_OK){
	//~ printf("Failed!\n");
	//~ failure++;
    //~ }
    //~ else
	//~ printf("PASS\n");



    printf("Testing Open RNIC so we can test close RNIC....");
    ret = iwarp_rnic_open (0, PAGE_MODE, context, &rnic_hndl);
    if(ret != IWARP_OK){
	printf("Failed!\n\n");
	failure++;
    }
    else
	printf("PASS\n\n");





    printf("\nTesting Query RNIC call:\n");
    ret = iwarp_rnic_query(rnic_hndl, &attrs);
    if(ret != IWARP_OK)
	printf("Query RNIC Failure\n\n");
    else{
	printf(".....VendorName=%s\n", attrs.vendor_name);
	printf(".....Version=%d\n", attrs.version);
	printf(".....Max QPs=%d\n",attrs.max_qp);
	printf(".....Max WRQs=%d\n",attrs.max_wrq);
	printf(".....Max SRQ (shared)=%d\n", attrs.max_srq);
	printf(".....Local Host Name=%s\n",attrs.local_hostname);
	printf(".....Official Host Name=%s\n", attrs.official_hostname);
	printf(".....FD of the device=%d\n", attrs.fd);

	printf("\n");
    }




    printf("Testing the Close RNIC call.....");
    ret = iwarp_rnic_close(rnic_hndl);
    if(ret != IWARP_OK)
	printf("Close Failed\n\n");
    else
	printf("Close Complete\n\n");



    printf("Need to have an Open RNIC for rest of tests SO OPEN ANOTHER ONE....\n");


    printf("Opening RNIC....");
    ret = iwarp_rnic_open (0, PAGE_MODE, context, &rnic_hndl);
    if(ret != IWARP_OK){
	printf("Failed!\n\n");
	failure++;
    }
    else
	printf("PASS\n\n");





} //End RNIC Section

    printf("------------------------------------\n");
    printf("Protection Domain\n");
    printf("------------------------------------\n");

{ //Start PD Section
    printf("Testing Allocate PD.....\n");
    for(i=0; i<MAX_PROT_DOMAIN; i++){
        ret = iwarp_pd_allocate(rnic_hndl, &prot_id);
	if(ret == IWARP_OK){
	    printf(".....Looks like %d is an open PD - PASS\n", prot_id);
	}
	else
	    printf("Protection Domain Allocation Failure!\n");
    }

    printf("\nTesting for Allocation Failure");
    ret = iwarp_pd_allocate(rnic_hndl, &prot_id);
    if(ret == IWARP_INSUFFICIENT_RESOURCES)
	printf(".....PASS\n");
    else
	printf(".....FAILURE\n");

    printf("\nTesting Deallocate PD.....\n");
     for(i=0; i<MAX_PROT_DOMAIN; i++){
        ret = iwarp_pd_deallocate(rnic_hndl, i);
	if(ret == IWARP_OK){
	    printf(".....Successfully Deallocated %d\n",i);
	}
	else
	    printf("Protection Domain Dealocation Failure for %d!\n", i);
    }

    /*Go ahead and keep one PD allocated*/
    ret = iwarp_pd_allocate(rnic_hndl, &prot_id);
    if(ret != IWARP_OK)
	printf("\nError Unable to Allocate A PD to keep\n\n");
    else
	printf("\nSucessfully Allocated PD %d\n\n", prot_id);

}   //End PD Section




    printf("-----------------------------------\n");
    printf("Doing CQ Tets\n");
    printf("-----------------------------------\n");

{ //Start CQ Section
    printf(".....Attempting to block use of CQE Handler  ");
    ret = iwarp_cq_create(rnic_hndl, temp, cq_evts, &cq_hndl);
    if(ret != IWARP_OK)
	printf("PASSED\n");
    else
	printf("FAILED\n");

    printf(".....Attempting to use too many cq_evts  ");
    ret = iwarp_cq_create(rnic_hndl, NULL, MAX_CQ_DEPTH + 1, &cq_hndl);
    if(ret != IWARP_OK)
	printf("PASSED\n");
    else
	printf("FAILED\n");

    printf(".....Attemtping to create a CQ to test destroy CQ with %d events  ", cq_evts);
    ret = iwarp_cq_create(rnic_hndl, NULL, cq_evts, &cq_hndl);
    if(ret != IWARP_OK)
	printf("FAILED\n");
    else
	printf("PASSED\n");

    printf(".....Attempting to destroy CQ  ");
    ret = iwarp_cq_destroy(rnic_hndl, cq_hndl);
    if(ret != IWARP_OK)
	printf("FAILED\n");
    else
	printf("PASSED\n");

    printf("\nCreating a CQ to use for later tests.....");
     ret = iwarp_cq_create(rnic_hndl, NULL, MAX_CQ_DEPTH / 2, &cq_hndl);
    if(ret != IWARP_OK)
	printf("FAILED\n");
    else
	printf("PASSED - depth = %d\n", cq_evts);

    printf("\n");
} //end CQ Section
    printf("-----------------------------------\n");
    printf("Testing Establishment of QPs\n");
    printf("-----------------------------------\n");

{ //Start QP Section

    //~ QP Attrs
    qp_attrs.sq_cq = cq_hndl;
    qp_attrs.rq_cq = cq_hndl;
    qp_attrs.sq_depth = MAX_WRQ;
    qp_attrs.rq_depth = MAX_WRQ ;
    qp_attrs.rdma_r_enable = TRUE;
    qp_attrs.rdma_w_enable = TRUE;
    qp_attrs.bind_mem_window_enable = FALSE;
    qp_attrs.send_sgl_max = MAX_S_SGL;
    qp_attrs.rdma_w_sgl_max = MAX_RDMA_W_SGL;
    qp_attrs.recv_sgl_max = MAX_R_SGL;
    qp_attrs.ord = MAX_ORD;
    qp_attrs.ird = MAX_IRD;
    qp_attrs.prot_d_id = prot_id;
    qp_attrs.zero_stag_enable = FALSE;

    qp_attrs.disable_mpa_markers = TRUE;  /*Need to set this*/
    qp_attrs.disable_mpa_crc = FALSE;
    //~ printf("qp attrs fro mpa markers are %d and crc is %d\n",  qp_attrs.disable_mpa_markers, qp_attrs.disable_mpa_crc);



    printf("Testing Create QP.....\n");
    for(i=0; i<MAX_QP; i++){
        ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id);
	if(ret == IWARP_OK){
	    printf(".....Looks like %d is an open QP - PASS\n", qp_id);
	}
	else
	    printf("QP Create Failure!\n");
    }

    printf("\nTesting for Create Failure");
    ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id);
    if(ret == IWARP_INSUFFICIENT_RESOURCES)
	printf(".....PASS\n");
    else
	printf(".....FAILURE\n");

    printf("\nTesting Destory QP.....\n");
     for(i=0; i<MAX_QP; i++){
        ret = iwarp_qp_destroy(rnic_hndl, i);
	if(ret == IWARP_OK){
	    printf(".....Successfully Destroyed %d\n",i);
	}
	else
	    printf(".....Queue Pair Destruction Failure for %d!\n", i);
    }

    //~ /*We need one QP for the rest of the tests*/
    ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id);
    if(ret != IWARP_OK)
	printf("Error creating QP\n");
    else
	printf("\nSuccessfully created QP with id %d\n", qp_id);

    printf("\n");
} //End QP Section

    printf("-----------------------------------\n");
    printf("Testing Memory Registration Stuff\n");
    printf("-----------------------------------\n");
{//Start Mem Reg Section
    printf("Testing memory registration.....");
    buffer = malloc(sizeof(char)*length);
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, buffer, length, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &stag_index, &mem_region);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");

    printf("mem_desc is %lx\n", (unsigned long)mem_region);
    printf("Stag is %d\n\n", stag_index);

    printf("Testing deallocate PD bound to MR.....");
    /*now lets try to deallocate the PD associated with that MR*/
    ret = iwarp_pd_deallocate(rnic_hndl, prot_id);
    if(ret == IWARP_PD_INUSE)
	printf("PASS - return code = %d\n", ret);
    else
	printf("FAILED\n");

    printf("Testing deallocate STag.....");
    ret = iwarp_deallocate_stag(rnic_hndl, stag_index);
    if(ret != IWARP_OK)
	printf("FAILED\n");
    else
	printf("PASS\n");

    printf("Testing for deallocate STag to fail.....");
    ret = iwarp_deallocate_stag(rnic_hndl, stag_index);
    if(ret != IWARP_OK)
	printf("PASS\n");
    else
	printf("FAIL\n");

    printf("Testing for deregister mem.....");
    ret = iwarp_deregister_mem(rnic_hndl, prot_id, mem_region);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");


    printf("Now lets get a new STag and a new MR for later use.....");
    buffer = malloc(sizeof(char)*length);

    //~ strcpy(buffer, "THIS IS A TEST HOPE IT WORKS!!!");

    printf("buffer contents are\n:%s:\n", buffer);
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, buffer, length, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &stag_index, &mem_region);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");

    printf("mem_desc is %lx\n", (unsigned long)mem_region);
    printf("Stag is %d\n\n", stag_index);


    printf("\n");


    //~ printf("buffer contents are\n:%s:\n", buffer);
    //~ ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, buffer - 10, length - 30, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &stag_index, &mem_region);
    //~ if(ret != IWARP_OK)
	//~ printf("FAIL\n");
    //~ else
	//~ printf("PASS\n");

    //~ printf("mem_desc is %lx\n", (unsigned long)mem_region);
    //~ printf("Stag is %d\n\n", stag_index);




} //End Mem Reg section

    printf("-----------------------------------\n");
    printf("Scatter Gather Testing\n");
    printf("-----------------------------------\n");
{ // Start SGL Test section
    /*set up the sgl*/
    printf("Creating Scatter Gather List.....");
    ret = iwarp_create_sgl(rnic_hndl, &sgl);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");

    /*set up sge*/
    sge.length = length;
    sge.stag = stag_index;
    sge.to = (uint64_t)(unsigned long)buffer;

    /*add the sge to the sgl*/
    printf("Adding SGE to SGL.....");
    ret = iwarp_register_sge(rnic_hndl, &sgl, &sge);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");

    printf("Testing for SGL overflow.....");
    ret = iwarp_register_sge(rnic_hndl, &sgl, &sge);
    if(ret != IWARP_SGL_FULL)
	printf("FAIL\n");
    else
	printf("PASS\n");

    /*create a work request*/
    swr.wr_id = (uint64_t)(unsigned long)&swr;
    swr.sgl = &sgl;
    swr.wr_type = IWARP_WR_TYPE_SEND;
    swr.cq_type = SIGNALED;

    rwr.wr_id = (uint64_t)(unsigned long)&rwr;
    rwr.sgl = &sgl;
    rwr.wr_type = IWARP_WR_TYPE_SEND;
    rwr.cq_type = SIGNALED;

} //End SGL Section

    printf("\n");
    printf("-----------------------------------\n");
    printf("Testing Preposting of Recvs\n");
    printf("-----------------------------------\n");
{// Start preposting section
    printf("Posting to the receive work queue.....");
    ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);
    if(ret == IWARP_OK)
	printf("PASS\n");
    else
	printf("FAIL\n");

    printf("\n");

}// End preposting section

    printf("-----------------------------------\n");
    printf("Testing Connection Buildup\n");
    printf("-----------------------------------\n");

{ //Test connection section

    /*print out the socket fd that we get after the function returns also checkit in the function to make sure
            we are getting the same value and no pointer errors or anything like that*/

    if (!(argc == 3 || argc == 4))
	usage();
    printf("argv[1] = Server/Client = %s, argv[2] = Port = %s\n", argv[1], argv[2]);

    /*test a post send work request for failure before connection*/


    remote_priv_data = malloc(512);





    if(strcmp(argv[1], "s") == 0 || strcmp(argv[1], "S") == 0){
	/*server*/
	server = 1;
	/*lets reset the recv buffer first*/
	memset(buffer, 0, length);
	if (argc != 3)
	    usage();
	ret = iwarp_qp_passive_connect(rnic_hndl, atoi(argv[2]), qp_id, "Server Priv Data", remote_priv_data, 512);
	if(ret == IWARP_OK){
	    connected = 1;
	    printf("Connection Made received private data %s\n", remote_priv_data);
	}
	else
	    printf("Connection Failed %d\n", ret);

    }
    else{
	/*client*/
	strcpy(buffer, "THIS IS A TEST HOPE IT WORKS!!!");
	server = 0;
	if (argc != 4)
	    usage();
	printf("connecting to %s\n", argv[3]);
	ret = iwarp_qp_active_connect(rnic_hndl, atoi(argv[2]), argv[3], 300000, 20, qp_id, "Client Priv Data",
					remote_priv_data, 512);

	if(ret == IWARP_OK){
	    printf("Connection Made received private data %s\n", remote_priv_data);
	    connected = 1;
	}
	else
	    printf("Connection Failed erorr is %d\n", ret);

    }




    if(!connected)
	exit(-1);





} //End connection section
    printf("\n");
    printf("-----------------------------------\n");
    printf("Testing Work Processing Verbs\n");
    printf("-----------------------------------\n");

 { //start work processing section

    printf("Not Testing WR overflow.....Need to change WRQ limit in limits.h to 1 for this test to work....");
    //~ ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);
    //~ if(ret == IWARP_RWQ_FULL)
	//~ printf("PASS\n");
    //~ else
	//~ printf("FAIL\n");

    /*set up the sgl for RDMA*/

    printf("Creating REMOTE Scatter Gather List.....");
    ret = iwarp_create_sgl(rnic_hndl, &remote_sgl);
    if(ret != IWARP_OK)
	printf("FAIL\n");
    else
	printf("PASS\n");



    /*build up the RDMA work request*/
    rdma_w_wr.wr_id = (uint64_t)(unsigned long)&rdma_w_wr;
    rdma_w_wr.sgl = &sgl;
    rdma_w_wr.remote_sgl = &remote_sgl;
    rdma_w_wr.wr_type = IWARP_WR_TYPE_RDMA_WRITE;
    rdma_w_wr.cq_type = SIGNALED;

    if(server){
	/*Get Remote STAG - TO - LEN*/
		printf("Testing Untagged Recv (polling CQ) -- Getting remote nodes STag, TO, and Len.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");
		printf("\n");

		/*set up remote  sge*/
		/*save the recv buffer into an sge for RDMA*/
		memcpy(&remote_sge, buffer, sizeof(iwarp_sge_t));
		printf("remote stag is %x len is %d and to is %x \n", remote_sge.stag, remote_sge.length, (unsigned)remote_sge.to);
		/*add the sge to the sgl*/
		printf("Adding SGE to RDMA SGL.....");
		ret = iwarp_register_sge(rnic_hndl, &remote_sgl, &remote_sge);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

		/*put into the buffer my Stag, Len, and TO*/
		memcpy(buffer, &sge, sizeof(iwarp_sge_t));
		printf("buffer is %p\n", buffer);
		printf("local stag is %x len is %d and to is %x \n", sge.stag, sge.length, (unsigned)sge.to);

	/*Send My STAG - TO - LEN*/
		/*Need to prepost a recv before sending this*/
		printf("Posting to the receive work queue.....");
		ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*Now send the info*/
		printf("Testing Untagged Send --STag To and Len.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this swr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

	/*Get Ready For RDMA from Remote*/
		printf("Polling CQ for RDMA ready.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");
		printf("\n");
		printf("Buffer is now %s\nRemote is ready for RDMA Write\n", buffer);

	/*Write the RDMA to the client*/

		//~ sleep(5);

		memset(buffer, 'D', length);
		printf("Testing RDMA Write.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_w_wr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this rdma_w_wr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");












	/*now wait for the client to ack RDMA and change its buffer for RDMA read test*/
		while(1){
		    //~ /*lets see what buffer is*/
		    if(buffer[length-1] == 'E')
			break;
		    ret = iwarp_rnic_advance(rnic_hndl);
		    if(ret != 0){
			printf("RDMAP Poll failed with %d\n", ret);
			exit(-1);
		    }
		    rounds++;
		}
		printf("After %d rounds BUFFER IS: %s\n", rounds, buffer);





	/*do an RDMA read*/
		rdma_w_wr.wr_type = IWARP_WR_TYPE_RDMA_READ;  /*set the type to RDMA_READ*/
		memset(buffer, '\0', length);
		printf("Buffer is cleared :%s:\n", buffer);

		printf("Testing RDMA Read.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_w_wr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this swr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ for RDMA Read.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK){
		    printf("FAIL with %s\n", iwarp_string_from_errno(ret));

		}
		else
		    printf("PASS\n");

		printf("After RDMA Read buffer is %s\n", buffer);




	/*send the quit message*/
		rdma_w_wr.wr_type = IWARP_WR_TYPE_RDMA_WRITE; /*set type back to RDMA_WRITE*/
		memset(buffer, 'Q', length);
		printf("Testing RDMA Write.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_w_wr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this swr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");


    }
    else{
	/*Send the server my Info*/
		/*put into the buffer my Stag, Len, and TO*/
		memcpy(buffer, &sge, sizeof(iwarp_sge_t));
		printf("buffer is %p\n", buffer);
		printf("local stag is %x len is %d and to is %x \n", sge.stag, sge.length, (unsigned)sge.to);

		/*post untagged send STag, TO and Len*/
		printf("Testing Untagged Send --STag To and Len.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");


		/*this swr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

		/*Get the servers Info*/
		/*now get the servers Stag, TO , and Len*/
		printf("Testing Untagged Recv (polling CQ) -- Getting remote nodes STag, TO, and Len.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

		/*set up remote  sge*/
		/*save the recv buffer into an sge for RDMA*/
		memcpy(&remote_sge, buffer, sizeof(iwarp_sge_t));
		printf("remote stag is %x len is %d and to is %x \n", remote_sge.stag, remote_sge.length, (unsigned)remote_sge.to);
		/*add the sge to the sgl*/
		printf("Adding SGE to RDMA SGL.....");
		ret = iwarp_register_sge(rnic_hndl, &remote_sgl, &remote_sge);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

	/*Signal Ready for RDMA write*/
		strcpy(buffer, "I AM READY YOU FOOL\n");
		printf("Sending RDMA ready notification.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this swr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");

	/*Wait for the RDMA from the server*/
		while(1){

		    /*lets see what buffer is*/
		    //~ printf("Buffer is: %s\n", buffer);
		    if(buffer[length-1] == 'D')
			break;
		    ret = iwarp_rnic_advance(rnic_hndl);
		    if(ret != 0){
			printf("RDMAP Poll failed with %d\n", ret);
			//~ exit(-1);
		    }
		    rounds++;
		}
		printf("inside %s After %d rounds BUFFER IS: %s\n", __FILE__, rounds, buffer);





	/*Write the RDMA to the client*/
		memset(buffer, 'E', length);
		printf("Testing RDMA Write.....");
		ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_w_wr);
		if(ret == IWARP_OK)
		    printf("PASS\n");
		else
		    printf("FAIL\n");

		/*this rdma_w_wr used here has a signaled completion type so poll the CQ*/
		printf("Testing Poll CQ.....");
		ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
		if(ret != IWARP_OK)
		    printf("FAIL\n");
		else
		    printf("PASS\n");




	/*Wait for the Quit Message*/
		while(1){
		    if(buffer[length-1] == 'Q')
			break;
		    ret = iwarp_rnic_advance(rnic_hndl);
		    if(ret != 0){
			printf("RDMAP Poll failed with %d\n", ret);
			exit(-1);
		    }
		    rounds++;
		}
		printf("inside %s After %d rounds BUFFER IS: %s\n", __FILE__, rounds, buffer);




    }


} //end work processing section
    printf("-----------------------------------\n");
    printf("Testing Connection Teardown\n");
    printf("-----------------------------------\n");

    printf("Disconnecting QP's.....");
    ret = iwarp_qp_disconnect(rnic_hndl, qp_id);
    if(ret != IWARP_OK)
	    printf("FAIL\n");
	else
	    printf("PASS\n");


    return 0;

    printf("All tests completed!\n");

}


