 /*
 * Verbs Testing Program
 *
 *Performs a number of ping-pongs for the user, similar to other RTT programs
 *
 *SERVER MUST BE STARTED FIRST
 *
 * $Id: untaggedRTT.c 666 2007-08-03 15:12:59Z dennis $
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
	  "Usage: %s [-n <numiter>] [-s <size>] {-r|c <ROOT HOSTNAME>} [-p <port>]\n",
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
    iwarp_sge_t send_sge;  /*scatter gather entry*/
    iwarp_sgl_t send_sgl;  /*the scatter gather list*/
    iwarp_sge_t recv_sge;  /*scatter gather entry*/
    iwarp_sgl_t recv_sgl;  /*the scatter gather list*/
    iwarp_wr_t swr;
    iwarp_wr_t rwr;
    int send_count;
    uint64_t wall_start, wall_end, start, stop;
    double PINGPONG[MAX_ITER];
    time_t seconds;
    char unique[20];
    char iter_str[7];
    char size_str[14];
    char filename[43];
    FILE *outputFile;
    double ping_latency, ping_pong_latency, total_size, stddev;
    volatile char *bufend;
    iwarp_work_completion_t wc;
    char *remote_private_data;

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
			    masterhost = NULL;
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
    my_info.send_buf = malloc(bufsize);
    my_info.recv_buf = malloc(bufsize);

    if(!my_info.send_buf || !my_info.recv_buf){
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

    /* XXX: for speed, turn off mpa crc and markers */
    qp_attrs.disable_mpa_markers = TRUE;
    qp_attrs.disable_mpa_crc = TRUE;

    ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id); /*create the QP*/
    if(ret != IWARP_OK)
	iwarp_untagged_err("QP Create failed", ret);

    /*Register memory*/
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, my_info.send_buf, bufsize, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &send_stag_index, &send_mem_region);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to regsiter memory", ret);

    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, my_info.recv_buf, bufsize, prot_id, 0, REMOTE_READ|REMOTE_WRITE, &recv_stag_index, &recv_mem_region);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to regsiter memory", ret);

    /*set up the sgl*/
    ret = iwarp_create_sgl(rnic_hndl, &send_sgl);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to create SGL", ret);

    /*set up sge*/
    send_sge.length = bufsize;
    recv_sge.length = bufsize;
    send_sge.stag = send_stag_index;
    recv_sge.stag = recv_stag_index;
    send_sge.to = (uint64_t)(unsigned long)my_info.send_buf;
    recv_sge.to = (uint64_t)(unsigned long)my_info.recv_buf;

    /*add the sge to the sgl*/
    iwarp_create_sgl(rnic_hndl, &send_sgl);
    ret = iwarp_register_sge(rnic_hndl, &send_sgl, &send_sge);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to add sge to sgl", ret);

    iwarp_create_sgl(rnic_hndl, &recv_sgl);
    ret = iwarp_register_sge(rnic_hndl, &recv_sgl, &recv_sge);
    if(ret != IWARP_OK)
	iwarp_untagged_err("Unable to add sge to sgl", ret);

    /*build two work requests one for send one for recv*/
    swr.wr_id = (uint64_t)(unsigned long)&swr;
    swr.sgl = &send_sgl;
    swr.wr_type = IWARP_WR_TYPE_SEND;
    swr.cq_type = UNSIGNALED;

    rwr.wr_id = (uint64_t)(unsigned long)&rwr;
    rwr.sgl = &recv_sgl;
    rwr.wr_type = IWARP_WR_TYPE_SEND;
    rwr.cq_type = SIGNALED;

    VERBOSE("\nAll iWARP data structures initialized, trying connection\n");

    remote_private_data = malloc(512);
    if(my_info.am_server == 1){
	/*server*/
	memset(my_info.send_buf, 'R', bufsize);
	memset(my_info.recv_buf, '\0', bufsize);

	OUTPUT("waiting on connection.....");



	ret = iwarp_qp_passive_connect(rnic_hndl, port, qp_id, "Passive Side Priv Data", remote_private_data, 512);
	if(ret == IWARP_OK){
	    OUTPUT("Connection Made\n");
	}
	else{
	    iwarp_untagged_err("Connection Failed",ret);
	}




    }
    else{
	/*client*/
	memset(my_info.send_buf, 'C', bufsize); /*init send buffer only 1 time*/
	memset(my_info.recv_buf, '\0', bufsize); /*init recv buff*/

	ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);   /*pre post the recv*/
	if(ret != IWARP_OK)
	    iwarp_untagged_err("Unable to post work request", ret);





	OUTPUT("connecting to %s.....", masterhost);
	ret = iwarp_qp_active_connect(rnic_hndl, port, masterhost, 300000, 1, qp_id, "Active Side Priv Data",
					remote_private_data, 512);

	if(ret == IWARP_OK){
	    OUTPUT("Connection Made\n");
	}
	else{
	    iwarp_untagged_err("Connection Failed", ret);
	}




    }



    VERBOSE("Received Private Data: %s\n", remote_private_data);





    /*******************************************/
    /*READY FOR MAIN BENCHMARK CODE*/
    /*******************************************/
    send_count = 1;

    if(my_info.am_server == 0) { /*client*/

	bufend = &my_info.recv_buf[bufsize-1];
	OUTPUT("Running....");
	for(i=0; i<iters; i++){
	    /*Wait for PING to arrive*/

	    ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc); /*just assume it read CQ ok, we'll check the buff below to verify*/
	    if(ret != IWARP_OK)
		iwarp_untagged_err("Could not poll CQ", -1);

	    if (*bufend != 'R')
		iwarp_untagged_err("Corrupted Data", -1);

	    VERBOSE("Received the PING!\n");


	    /*send the pong*/
	    memset(my_info.recv_buf, '\0', bufsize); /*reset our recv buffer*/

	    //~ printf("before postig r-wrq queue has %d entries\n", rnic_ptr->recv_q.size);

	    ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);  /*pre-post a recv buffer*/
	    if(ret != IWARP_OK){
		iwarp_untagged_err("Unable to post work request to catch the next pong", ret);
	    }



	    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);  /*post a send*/
	    if(ret != IWARP_OK)
		iwarp_untagged_err("Unable to post to SQ", ret);
	    send_count++;

	    VERBOSE("Send PONG\n");



	}//end for loop
    }
    else{  /*server*/

	bufend = &my_info.recv_buf[bufsize-1];
	rdtsc(wall_start);
	for(i=0; i<iters; i++){
	    /*Send Ping*/
	    rdtsc(start);
	    memset(my_info.recv_buf, '\0', bufsize); /*reset our recv buffer*/
	    ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);  /*pre-post a recv buffer*/
	    if(ret != IWARP_OK)
		iwarp_untagged_err("Unable to post work request to catch the ping", ret);


	    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr); /*post a send*/
	    if(ret != IWARP_OK)
		iwarp_untagged_err("Unable to post to SQ", ret);



	    send_count++;


	    VERBOSE("Sent PING\n");



	    /*Now check for PONG to complete*/
	    ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc); /*just assume it read CQ ok, we'll check the buff below to verify*/
	    if(ret != IWARP_OK)
		iwarp_untagged_err("Could not poll CQ", -1);

	    if (*bufend != 'C'){
		printf("the char we got is %c\n", *bufend);
		iwarp_untagged_err("Corrupted Data", -1);

	    }


		rdtsc(stop);

	    VERBOSE("Got PONG\n");

	    //~ PINGPONG[i] = (double)(stop - start ) / mhz;
	    PINGPONG[i] =elapsed_wall_time(start, stop, MICROSECONDS);
	    VERBOSE("Ping Pong %d = %f uS\n", i, PINGPONG[i]);

	    //~ exit(0);

	} //end for loop
	rdtsc(wall_end);


	OUTPUT("All data gathered and collected....\n");
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
	stddev = 0.;
	total_size = 2 * bufsize; /*total bytes*/
	total_size = total_size / (1024 * 1024); /*total in Megabytes*/
	total_size = total_size * 8; /*now total in Megabits*/

	if (iters > 0) {
	    for(i=0; i<iters; i++){
		ping_pong_latency += PINGPONG[i] / 2.;

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

	    ping_pong_latency /= iters;

	    if (iters > 1) {
		for (i=0; i<iters; i++) {
		    double diff = PINGPONG[i]/2. - ping_pong_latency;
		    stddev += diff * diff;
		}
		stddev /= (double) (iters - 1);
		stddev = sqrt(stddev);
	    }
	}
	/* OUTPUT("------------------------------------------------------------------------------\n"); */
	/* OUTPUT("AVERAGES\n"); */
	/* OUTPUT("------------------------------------------------------------------------------\n"); */

	total_size = total_size * iters;

	OUTPUT("RTT/2= %11.6f uS +- %11.6f\n", ping_pong_latency, stddev);
	/* OUTPUT("RTT= %f uS\n",ping_pong_latency / iters); */
	/* OUTPUT("Bandwidth= %f Mbps\n\n", total_size / (double)(ping_pong_latency / 1000000 )); */

	if (outputfile) {
	    fprintf(outputFile,"RTT/2= %f\n",(ping_pong_latency / 2) / iters);
	    /* fprintf(outputFile,"RTT= %f\n",ping_pong_latency / iters); */
	    /* fprintf(outputFile,"Bandwidth= %f\n", total_size / (double)(ping_pong_latency / 1000000 )); */
	    fprintf(outputFile,"SIZE= %d\n", bufsize);
	    fprintf(outputFile,"ITERS= %d\n", iters);
	}


	/*need a call to close connection verb!*/

    }

    OUTPUT("Completed successfully, closing connection!\n");

    ret = iwarp_qp_disconnect(rnic_hndl, qp_id);
    if(ret != IWARP_OK)
	    fprintf(stderr,"FAILED to disconnect QP\n");

    ret = iwarp_rnic_close(rnic_hndl);
    if(ret != IWARP_OK)
	    fprintf(stderr,"FAILED to close RNIC\n");


    return 0;
 }
