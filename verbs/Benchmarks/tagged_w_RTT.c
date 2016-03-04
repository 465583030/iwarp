 /*
 * Verbs Testing Program
 *
 *Performs a number of ping-pongs for the user, similar to other RTT programs
 *
 *SERVER SHOULD BE STARTED FIRST
 *
 * $Id: tagged_w_RTT.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> /*for uint64_t*/
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <math.h>
#include "../verbs.h"
#include "../perfmon.h"


#define verbose 0  /*1=verbose output 0=only necessary output*/
#define test 0 /*1= test output 0=quiet test output*/
#define output 1 /*1= some output 0 = nothing*/
#define outputfile 0  /* 1= generate output file, 0= no output file */
#define detailed_output 0 /*show the numbers for each round but not verbose messages*/
//~ #define BUF_PRINT_OK /*comment out to not print buffer contents*/
#define VERBOSE if(verbose)printf
#define TEST if(test)printf
#define OUTPUT if(output)printf
#define DETAILED_OUTPUT if(detailed_output)printf

//~ static char hostname[1024];
static int iters = 10;
//~ static int max_retry = 10;
//~ static int max_poll = 10000000;
int event = 0;
int OK_TO_QUIT = 0;
static int bufsize = 1024;
int port = 6789;

typedef struct{
    int am_server;
    char *send_buf;
    char *recv_buf;
    char *rdma_buf;
}info_t;

/*Function Prototypes*/
static void usage(void);
unsigned long parse_number(const char *cp);
void buff_printer(char buf[], int len);
void iwarp_untagged_err(const char *msg, iwarp_status_t code);

#define BUF_MAX 10240000
#define MAX_ITER 10000
#define ITER_LIMIT 200


static void usage(void)
/*Print out the usage syntax
Numbers  can be given with suffix "k", "m", or "g" to scale by 10^3, 6, or 9,
 e.g.:  iWarpRTT -r 10.0.0.2 -n 1k -s 1m */
{
    	fprintf(stderr,
	  "Usage: %s [-n <numiter>] [-s <size>] [-p <port>] {-r|c <ROOT HOSTNAME>}\n",
	  progname);
	exit(1);
}

unsigned long parse_number(const char *cp)
/*Parse out numbers given on command line ie 220k = 2000
Taken from ardma.c*/
{
    unsigned long v;
    char *cq;

    v = strtoul(cp, &cq, 0);
    if (*cq) {
	if (!strcasecmp(cq, "k"))
	    v *= 1000;
	else if (!strcasecmp(cq, "m"))
	    v *= 1000000;
	else if (!strcasecmp(cq, "g"))
	    v *= 1000000000;
	else
	    usage();
    }
    return v;
}

void buff_printer(char buf[], int len)
/*just print out a buffer passed in*/
{
	int j,i;
	int columns = 10;
	printf("--------------------------------\n");
	printf("------CONTENTS OF BUFFER:-------\n");
	printf("--------------------------------\n");
	for(i=0; i<len;){
		for(j=0; j<columns; j++){
			printf("%d: %c  |  ",i, buf[i]);
			i++;
		}
		printf("\n");
	}
	printf("---------END CONTENTS----------\n");
}

void iwarp_untagged_err(const char *msg, iwarp_status_t code)
/*Handle printing Ammasso errors and exiting */
{
	fprintf(stderr,"----------ERROR----------\n");
	fprintf(stderr, "%s : ERROR CODE = %d = %s\n", msg, code, iwarp_string_from_errno(code));
	fprintf(stderr,"----------ERROR----------\n");
	exit(1);

}


int main(int argc, char **argv)
{
    int i;
    info_t my_info;
    char *masterhost = NULL;
    iwarp_status_t ret;
    iwarp_rnic_handle_t rnic_hndl;
    iwarp_prot_id prot_id;
    iwarp_cq_handle_t cq_hndl;
    iwarp_qp_attrs_t qp_attrs;
    iwarp_qp_handle_t qp_id;
    iwarp_stag_index_t send_stag_index;
    iwarp_mem_desc_t send_mem_region;
    iwarp_stag_index_t recv_stag_index;
    iwarp_mem_desc_t recv_mem_region;
    iwarp_stag_index_t rdma_stag_index;
    iwarp_mem_desc_t rdma_mem_region;
    iwarp_sge_t send_sge;  /*scatter gather entry*/
    iwarp_sgl_t send_sgl;  /*the scatter gather list*/
    iwarp_sge_t recv_sge;  /*scatter gather entry*/
    iwarp_sgl_t recv_sgl;  /*the scatter gather list*/
    iwarp_sge_t remote_sge;
    iwarp_sgl_t remote_sgl;
    iwarp_sge_t rdma_sge;
    iwarp_sgl_t rdma_sgl;
    iwarp_wr_t swr;
    iwarp_wr_t rwr;
    iwarp_wr_t rdma_wr;
    int send_count;
    int control_size = 1024;
    uint64_t wall_start, wall_end, start, stop;
    double PINGPONG[MAX_ITER];
    time_t seconds;
    char unique[20];
    char iter_str[7];
    char size_str[14];
    char filename[43];
    FILE *outputFile;
    double ping_latency, ping_pong_latency, total_size;
    volatile char *bufend;
    iwarp_work_completion_t wc;
    int rounds;
    char *remote_priv_data;

    /********************************************************/
    /*Parse out the arguments passed in so we can set stuff up*/
    /********************************************************/
     set_progname(argc, argv);
     my_info.am_server = -1;
     while (++argv, --argc > 0) {
	    const char *cp;
	    if (**argv == '-') switch ((*argv)[1]) {
		    case 'n':
			    cp = *argv + 2;
			    for (i = 1; *cp && *cp == "numiter"[i]; ++cp, ++i)  ;
			    if (*cp) usage();
			    if (++argv, --argc == 0) usage();
			    iters = parse_number(*argv);

			    if(iters > MAX_ITER){
				    OUTPUT("\n\nWARNING %d ITERATIONS TOO MANY USING ONLY %d ITERATIONS\n\n",
					    iters,MAX_ITER);
				    iters = MAX_ITER;
			    }

			    break;
		    case 's':
			    cp = *argv + 2;
			    for (i = 1; *cp && *cp == "size"[i]; ++cp, ++i);
			    if (*cp) usage();
			    if (++argv, --argc == 0) usage();
			    bufsize = parse_number(*argv);

			    if(bufsize > BUF_MAX){
				    OUTPUT("\n\nWARNING BUFFER OF %d BYTES TOO LARGE USING MAX SIZE = %d BYTES\n\n",
					    bufsize,BUF_MAX);
				    bufsize = BUF_MAX;
			    }
			    break;
		    case 'r':
			    my_info.am_server = 1;
			    VERBOSE("I am the server. ");
			    break;
		    case 'c':
			    my_info.am_server = 0;
			    if (++argv, --argc == 0) usage();
			    masterhost = *argv;
			    VERBOSE("I am the client. ");
			    VERBOSE("The server/master host is %s\n",masterhost);
			    break;
		    case 'p':
			    if (++argv, --argc == 0) usage();
			    port = atoi(*argv);
			    break;
		    //~ case 'm':
			    //~ VERBOSE("Changing trans mode....");
			    //~ trans_mode =  atoi(argv[1]);
			    //~ if(trans_mode != 1 && trans_mode != 2){
				    //~ fprintf(stderr,"Unknonw test type\n");
				    //~ exit(1);
			    //~ }
			    //~ break;
	    }

    }

    /************************/
    /*Now set up our buffers*/
    /************************/
    my_info.send_buf = malloc(control_size);
    my_info.recv_buf = malloc(control_size);
    my_info.rdma_buf = malloc(bufsize);

    if(!my_info.send_buf || !my_info.recv_buf || !my_info.rdma_buf){
	iwarp_untagged_err("Error - unable to allocate memory", -1);
    }

    if (my_info.am_server == -1)
		usage();

    /*********************/
    /*Set up iWARP stuff*/
    /*********************/
    ret = iwarp_rnic_open (0, PAGE_MODE, NULL, &rnic_hndl); /*open rnic*/
    if(ret != IWARP_OK)
	iwarp_untagged_err("RNIC open failed", ret);

    ret = iwarp_pd_allocate(rnic_hndl, &prot_id); /*allocate pd*/
    if(ret != IWARP_OK)
	iwarp_untagged_err("PD allocate failed", ret);

    ret = iwarp_cq_create(rnic_hndl, NULL, MAX_CQ_DEPTH / 2, &cq_hndl); /*create the completion queue*/
    if(ret != IWARP_OK)
	iwarp_untagged_err("Create CQ failed", ret);

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
    qp_attrs.disable_mpa_markers = TRUE;
    qp_attrs.disable_mpa_crc = FALSE;



    ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id); /*create the QP*/
    if(ret != IWARP_OK)
	iwarp_untagged_err("QP Create failed", ret);

    /*Register memory*/
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, my_info.send_buf, control_size, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &send_stag_index, &send_mem_region);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to regsiter memory", ret);

    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, my_info.recv_buf, control_size, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &recv_stag_index, &recv_mem_region);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to regsiter memory", ret);

    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, my_info.rdma_buf, bufsize, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &rdma_stag_index, &rdma_mem_region);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to regsiter memory", ret);

    /*set up the sgl*/
    ret = iwarp_create_sgl(rnic_hndl, &send_sgl);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to create send SGL", ret);

    ret = iwarp_create_sgl(rnic_hndl, &recv_sgl);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to create recv SGL", ret);

    ret = iwarp_create_sgl(rnic_hndl, &rdma_sgl);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to create rdma SGL", ret);

    ret = iwarp_create_sgl(rnic_hndl, &remote_sgl);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to create remote SGL", ret);


    /*set up sges*/
    send_sge.length = control_size;
    recv_sge.length = control_size;
    rdma_sge.length = bufsize;
    send_sge.stag = send_stag_index;
    rdma_sge.stag = rdma_stag_index;
    recv_sge.stag = recv_stag_index;
    send_sge.to = (uint64_t)(unsigned long)my_info.send_buf;
    recv_sge.to = (uint64_t)(unsigned long)my_info.recv_buf;
    rdma_sge.to = (uint64_t)(unsigned long)my_info.rdma_buf;

    /*add the sges to the sgls*/
    ret = iwarp_register_sge(rnic_hndl, &send_sgl, &send_sge);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to add sge to send sgl", ret);

    ret = iwarp_register_sge(rnic_hndl, &recv_sgl, &recv_sge);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to add sge to recv sgl", ret);

    ret = iwarp_register_sge(rnic_hndl, &rdma_sgl, &rdma_sge);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to add sge to rdma sgl", ret);

    /*build three  work requests one for send one for recv and one for RDMA*/
    swr.wr_id = (uint64_t)(unsigned long)&swr;
    swr.sgl = &send_sgl;
    swr.wr_type = IWARP_WR_TYPE_SEND;
    swr.cq_type = SIGNALED;

    rwr.wr_id = (uint64_t)(unsigned long)&rwr;
    rwr.sgl = &recv_sgl;
    rwr.wr_type = IWARP_WR_TYPE_SEND;
    rwr.cq_type = SIGNALED;


     ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);   /*pre post the recv*/
	if(ret != IWARP_OK)
	    iwarp_untagged_err("Unable to post work request", ret);

    rdma_wr.wr_id = (uint64_t)(unsigned long)&rdma_wr;
    rdma_wr.sgl = &rdma_sgl;
    rdma_wr.remote_sgl = &remote_sgl;
    rdma_wr.wr_type = IWARP_WR_TYPE_RDMA_WRITE;
    rdma_wr.cq_type = SIGNALED;


    VERBOSE("All iWARP data structures initialized, pre-posted a recv, trying connection\n");
    remote_priv_data = malloc(512);
    if(my_info.am_server == 1){
	/*server*/
	VERBOSE("waiting on connection.....");
	ret = iwarp_qp_passive_connect(rnic_hndl, port, qp_id, "Passive Side Priv Data", remote_priv_data, 512);
	if(ret == IWARP_OK){
	    VERBOSE("Connection Made\n");
	}
	else{
	    iwarp_untagged_err("Connection Failed",ret);
	}

	VERBOSE("Received private data %s\n", remote_priv_data);



	/*Send client my STag, TO and Len*/
	memcpy(my_info.send_buf, &rdma_sge, sizeof(iwarp_sge_t));
	VERBOSE("buffer is %p\n", my_info.rdma_buf);
	VERBOSE("local stag is %x len is %d and to is %x \n", rdma_sge.stag, rdma_sge.length, (unsigned)rdma_sge.to);

	VERBOSE("Sending STag To and Len.....");
	ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}




	/*this swr used here has a signaled completion type so poll the CQ*/
	VERBOSE("Polling CQ for send completion.....");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}


	/*Get the clients STag, TO, and Len*/
	VERBOSE("Getting remote nodes STag, TO, and Len.....");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}

	/*set up remote  sge*/
	memcpy(&remote_sge, my_info.recv_buf, sizeof(iwarp_sge_t));
	VERBOSE("remote stag is %x len is %d and to is %x \n", remote_sge.stag, remote_sge.length, (unsigned)remote_sge.to);
	/*add the sge to the sgl*/
	VERBOSE("Adding SGE to RDMA SGL.....");
	ret = iwarp_register_sge(rnic_hndl, &remote_sgl, &remote_sge);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}

    }
    else{
	/*client*/
	OUTPUT("connecting to %s.....", masterhost);
	ret = iwarp_qp_active_connect(rnic_hndl, port, masterhost, 300000, 1, qp_id, "Active Side Priv Data",
				    remote_priv_data, 512);

	if(ret == IWARP_OK){
	    OUTPUT("Connection Made\n");
	}
	else{
	    iwarp_untagged_err("Connection Failed erorr is %d", ret);
	}

	VERBOSE("Received private data %s\n", remote_priv_data);



	/*wait for servers STag, TO, and Len*/
	VERBOSE("Getting remote nodes STag, TO, and Len.....");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");
	}
	else{
	    VERBOSE("FAIL\n");
	}

	/*set up remote  sge*/
	memcpy(&remote_sge, my_info.recv_buf, sizeof(iwarp_sge_t));
	VERBOSE("remote stag is %x len is %d and to is %x \n", remote_sge.stag, remote_sge.length, (unsigned)remote_sge.to);
	/*add the sge to the sgl*/
	VERBOSE("Adding SGE to RDMA SGL.....");
	ret = iwarp_register_sge(rnic_hndl, &remote_sgl, &remote_sge);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}

	/*Send server my STag, TO, and Len*/
	memcpy(my_info.send_buf, &rdma_sge, sizeof(iwarp_sge_t));
	VERBOSE("buffer is %p\n", my_info.rdma_buf);
	VERBOSE("local stag is %x len is %d and to is %x \n", rdma_sge.stag, rdma_sge.length, (unsigned)rdma_sge.to);

	VERBOSE("Sending STag To and Len.....");
	ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
	if(ret == IWARP_OK){
	    VERBOSE("PASS\n");}
	else{
	    VERBOSE("FAIL\n");}

	/*this swr used here has a signaled completion type so poll the CQ*/
	VERBOSE("Polling CQ for send completion.....");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if(ret != IWARP_OK){
	    VERBOSE("FAIL\n");}
	else{
	    VERBOSE("PASS\n");}

    }



    /*******************************************/
    /*READY FOR MAIN BENCHMARK CODE*/
    /*******************************************/
    send_count = 1;
    rounds = 0;
    if(my_info.am_server == 0) { /*client*/
	bufend = &my_info.rdma_buf[bufsize-1];
	OUTPUT("Running....");
	for(i=0; i<iters; i++){
	    /*Wait for PING to arrive*/
	    while(1){
		if(*bufend == 'R')
		    break;
		ret = iwarp_rnic_advance(rnic_hndl);
		if(ret != 0){
		    fprintf(stderr,"RDMAP Poll failed with %d\n", ret);
		    exit(-1);
		}
		rounds++;
	    }
	    VERBOSE("After %d rounds ", rounds);
	    VERBOSE("Received the PING!\n");

	    /*send the pong*/
	    memset(my_info.rdma_buf, 'C', bufsize);
	    VERBOSE("Sending Pong.....");
	    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_wr);
	    if(ret == IWARP_OK){
		VERBOSE("PASS\n");}
	    else{
		VERBOSE("FAIL\n");}
	    /*this rdma_wr used here has a signaled completion type so poll the CQ*/
	   VERBOSE("Polling CQ.....");
	    ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	    if(ret != IWARP_OK){
		VERBOSE("FAIL\n"); }
	    else{
		VERBOSE("PASS\n");}
	    VERBOSE("Sent PONG\n");



	}//end for loop

	while(1){
	    if(*bufend == 'Q')
		break;
	    ret = iwarp_rnic_advance(rnic_hndl);
	    if(ret != 0){
		fprintf(stderr,"RDMAP Poll failed with %d\n", ret);
		exit(-1);
	    }

	}
	VERBOSE("Received Quit Message\n");
	OUTPUT("Success\n");

    }
    else{  /*server*/

	bufend = &my_info.rdma_buf[bufsize-1];
	rdtsc(wall_start);
	for(i=0; i<iters; i++){
	    rdtsc(start);
	    /*Send Ping*/
	    memset(my_info.rdma_buf, 'R', bufsize);
	    VERBOSE("Sending Ping.....");
	    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_wr);
	    if(ret == IWARP_OK){
		VERBOSE("PASS\n");}
	    else{
		VERBOSE("FAIL\n");}
	    /*this rdma_wr used here has a signaled completion type so poll the CQ*/
	   VERBOSE("Polling CQ.....");
	    ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	    if(ret != IWARP_OK){
		VERBOSE("FAIL\n"); }
	    else{
		VERBOSE("PASS\n");}

	    send_count++;
	    VERBOSE("Sent PING\n");



	    /*Now check for PONG*/
	    while(1){
		if(*bufend == 'C')
		    break;
		ret = iwarp_rnic_advance(rnic_hndl);
		if(ret != 0){
		    fprintf(stderr,"RDMAP Poll failed with %d\n", ret);
		    exit(-1);
		}
		rounds++;
	    }
	    rdtsc(stop);

	    VERBOSE("Got PONG\n");

	    PINGPONG[i] =elapsed_wall_time(start, stop, MICROSECONDS);
	    VERBOSE("Ping Pong %d = %f uS\n", i, PINGPONG[i]);



	} //end for loop
	rdtsc(wall_end);


	VERBOSE("All data gathered and collected....\n");
	DETAILED_OUTPUT("Bufsize = %d, #Iterations = %d\n",bufsize, iters);

	DETAILED_OUTPUT("#\tRTT/2\t\tPING-PONG\tBANDWIDTH\n");

	/*create the output files name*/
	if (outputfile) {
	    seconds = time(NULL);
	    sprintf(unique, "%ld", seconds);
	    sprintf(iter_str, "%d",iters);
	    sprintf(size_str, "%d", bufsize);
	    strcpy(filename,"untaggedRTTtest.");
	    strcat(filename, unique);
	    strcat(filename,".i_");
	    strcat(filename, iter_str);
	    strcat(filename,".s_");
	    strcat(filename, size_str);
	    strcat(filename,".txt");

	    /*create the output file*/
	    outputFile=fopen(filename, "w+");
	    if(outputFile == NULL){
		    fprintf(stderr,"Unable to create output file\n");
		    exit(-1);
	    }
	}
	ping_latency = 0.0;
	ping_pong_latency = 0.0;
	total_size = 2 * bufsize; /*total bytes*/
	total_size = total_size / (1024 * 1024); /*total in Megabytes*/
	total_size = total_size * 8; /*now total in Megabits*/






	for(i=0; i<iters; i++){
	    ping_pong_latency += PINGPONG[i];

	    DETAILED_OUTPUT("%d\t%f\t%f\t%f\n",
		    i+1,
		    PINGPONG[i] / 2,
		    PINGPONG[i],
		    total_size / (double)(PINGPONG[i] / 1000000 )
	    );

	    if (outputfile)
		fprintf(outputFile,"~\t%d\t%f\t%f\t%f\n",
			i+1,
			PINGPONG[i] / 2,
			PINGPONG[i],
			total_size / (double)(PINGPONG[i] / 1000000 )
		);
	}










#if 0
	OUTPUT("------------------------------------------------------------------------------\n");
	OUTPUT("AVERAGES\n");
	OUTPUT("------------------------------------------------------------------------------\n");
#endif

	total_size = total_size * iters;


	double avg = 0., stdev = 0.;
	if (iters > 0) {
	    /* convert time to half round trip */
	    for (i=0; i<iters; i++)
		PINGPONG[i] /= 2.;

	    /* toss out any "outliers" > 110% min */
	    if (1) {
		double min = 1.0e20;
		for (i=0; i<iters; i++)
		    if (PINGPONG[i] < min)
			min = PINGPONG[i];
		min = 1.1 * min;
		for (i=0; i<iters; i++)
		    if (PINGPONG[i] > min) {
			int j;
			--iters;
			for (j=i; j<iters; j++)
			    PINGPONG[j] = PINGPONG[j+1];
			--i;
		    }
	    }

	    for (i=0; i<iters; i++)
		avg += PINGPONG[i];
	    avg /= iters;

	    if (iters > 1) {
		for (i=0; i<iters; i++) {
		    double diff = PINGPONG[i] - avg;
		    stdev += diff * diff;
		}
		stdev = sqrt(stdev / (iters - 1));
	    }
	}


#if 0
	OUTPUT("RTT/2= %f uS\n",(ping_pong_latency / 2) / iters);
	OUTPUT("StdDev= %f\n", stdev);
	OUTPUT("RTT= %f uS\n",ping_pong_latency / iters);
	OUTPUT("Bandwidth= %f Mbps\n\n", total_size / (double)(ping_pong_latency / 1000000 ));
#endif
	printf("%7d %13.6f +/- %13.6f\n", bufsize, avg, stdev);





	if (outputfile) {
	    fprintf(outputFile,"RTT/2= %f\n",(ping_pong_latency / 2) / iters);
	    fprintf(outputFile,"StdDev= %f\n", stdev);
	    fprintf(outputFile,"RTT= %f\n",ping_pong_latency / iters);
	    fprintf(outputFile,"Bandwidth= %f\n", total_size / (double)(ping_pong_latency / 1000000 ));
	    fprintf(outputFile,"SIZE= %d\n", bufsize);
	    fprintf(outputFile,"ITERS= %d\n", iters);
	}
	VERBOSE("Completed successfully, closing connection!\n");

	/*send the quit message*/
	memset(my_info.rdma_buf, 'Q', bufsize);
	VERBOSE("Sending Ping.....");
	ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &rdma_wr);
	if(ret == IWARP_OK){
	VERBOSE("PASS\n");}
	else{
	VERBOSE("FAIL\n");}
	/*this rdma_wr used here has a signaled completion type so poll the CQ*/
	VERBOSE("Polling CQ.....");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if(ret != IWARP_OK){
	VERBOSE("FAIL\n"); }
	else{
	VERBOSE("PASS\n");}

    }





    ret = iwarp_qp_disconnect(rnic_hndl, qp_id);
    if(ret != IWARP_OK)
	    fprintf(stderr,"FAILED to disconnect QP\n");

    ret = iwarp_rnic_close(rnic_hndl);
    if(ret != IWARP_OK)
	    fprintf(stderr,"FAILED to close RNIC\n");


    return 0;
 }
