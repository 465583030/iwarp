/*
 * Unidirectional spray bandwidth test.
 *
 * $Id: uni-spray-bw.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include "verbs/verbs.h"
#include "verbs/perfmon.h"

static int iters = 30;
static int bufsize = 1024;
static int port = 4210;
static int blocking = 0;
static const char *masterhost = NULL;
static char *buf0, *buf1, *bufmalloc;
static char small_send_buf[64], small_recv_buf[64];

static iwarp_rnic_handle_t rnic_hndl;
static iwarp_prot_id prot_id;
static iwarp_qp_handle_t qp_id;
static iwarp_cq_handle_t cq_hndl;
static iwarp_mem_desc_t send_mr, recv_mr, buf_mr;
static iwarp_stag_index_t send_stag, recv_stag, buf_stag;

static int am_server;
static iwarp_sge_t my_sge, his_sge;

/*
 * Print out the usage syntax.
 */
static void usage(void)
{
    fprintf(stderr,
      "Usage: %s [options] -r\n"
      "   or: %s [options] -c <peer>\n", progname, progname);
    fprintf(stderr,
      "Options: [-n <numiter>] [-s <size>] [-p <port>] [-b]\n");
    exit(1);
}

/*
 * Parse out numbers given on command line.
 */
static unsigned long parse_number(const char *cp)
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

/*
 * For highly flexible argument parsing, allow an option argument
 * to appear in many places.  The following are all equivalent:
 *   --np=3
 *   --np 3
 *   --np3
 *
 * Note that the argument talked about here is not optional, it is a
 * required argument to an optional command-line option.
 */
static const char * find_optarg(const char *cp, int *argcp,
  char ***argvp, const char *const which)
{
    /* argument could be in this one, or, if not, in the next arg */
    if (*cp) {
        /* optional = */
        if (*cp == '=')
            ++cp;
    } else {
        if (++*argvp, --*argcp <= 0)
            error("%s: option -%s requires an argument", __func__, which);
        cp = **argvp;
    }
    return cp;
}

#define MAX(a,b) ((a) > (b) ? (a) : (b))

static void parse_args(int argc, char **argv)
{
    const char *cp, *cq;
    int len;

    am_server = -1;
    set_progname(argc, argv);

    while (++argv, --argc > 0) {
	cp = *argv;
	if (*cp++ != '-')
	    usage();
	if (*cp == '-')  ++cp;  /* optional second "-" */
	if ((cq = strchr(cp, '=')))  /* maybe optional = */
	    len = cq - cp;
	else
	    len = strlen(cp);
	if (len < 1)
	    usage();

	if (!strncmp(cp, "numiter", MAX(1,len))) {
	    cp += len;
	    cp = find_optarg(cp, &argc, &argv, "numiter");
	    iters = parse_number(cp);
	} else if (!strncmp(cp, "size", MAX(1,len))) {
	    cp += len;
	    cp = find_optarg(cp, &argc, &argv, "size");
	    bufsize = parse_number(cp);
	} else if (!strncmp(cp, "r", MAX(1,len))) {
	    am_server = 1;
	} else if (!strncmp(cp, "c", MAX(1,len))) {
	    cp += len;
	    masterhost = find_optarg(cp, &argc, &argv, "c");
	    am_server = 0;
	} else if (!strncmp(cp, "port", MAX(1,len))) {
	    cp += len;
	    cp = find_optarg(cp, &argc, &argv, "port");
	    port = parse_number(cp);
	} else if (!strncmp(cp, "b", MAX(1,len))) {
	    blocking = 1;
	} else {
	    usage();
	}
    }
    if (am_server == -1)
	usage();
}

/*
 * Error, fatal, with an iwarp error code.
 */
static void
iwarp_error(int ret, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", iwarp_string_from_errno(ret));
    exit(1);
}

#define uint64_from_ptr(p) ((uint64_t)(uintptr_t)(p))

/*
 * Open the NIC, set up everything, connect to the other side.
 */
static void connect_iwarp(void)
{
    int ret;
    iwarp_qp_attrs_t qp_attrs;
    iwarp_sgl_t sgl;
    iwarp_wr_t swr, rwr;
    char remote_priv_data[64];
    unsigned long pagesize = getpagesize();
    iwarp_work_completion_t wc;

    debug(1, __func__);
    ret = iwarp_rnic_open(0, PAGE_MODE, NULL, &rnic_hndl);
    if (ret)
	iwarp_error(ret, "RNIC open");

    ret = iwarp_pd_allocate(rnic_hndl, &prot_id);
    if (ret)
	iwarp_error(ret, "PD allocate");

    ret = iwarp_cq_create(rnic_hndl, NULL, MAX_CQ_DEPTH, &cq_hndl);
    if (ret)
	iwarp_error(ret, "CQ create");

    qp_attrs.sq_cq = cq_hndl;
    qp_attrs.rq_cq = cq_hndl;
    qp_attrs.sq_depth = MAX_WRQ;
    qp_attrs.rq_depth = MAX_WRQ;
    qp_attrs.rdma_r_enable = 1;
    qp_attrs.rdma_w_enable = 1;
    qp_attrs.bind_mem_window_enable = 0;
    qp_attrs.send_sgl_max = MAX_S_SGL;
    qp_attrs.rdma_w_sgl_max = MAX_RDMA_W_SGL;
    qp_attrs.recv_sgl_max = MAX_R_SGL;
    qp_attrs.ord = MAX_ORD;
    qp_attrs.ird = MAX_IRD;
    qp_attrs.prot_d_id = prot_id;
    qp_attrs.zero_stag_enable = 0;
    qp_attrs.disable_mpa_markers = 1;
    qp_attrs.disable_mpa_crc = 0;

    ret = iwarp_qp_create(rnic_hndl, &qp_attrs, &qp_id);
    if (ret)
	iwarp_error(ret, "QP create");

    /* register memory */
    bufmalloc = Malloc((bufsize + pagesize-1) * 2);
    buf0 = (char *)((uintptr_t)((bufmalloc + (pagesize-1))) & ~(pagesize-1));
    buf1 = (char *)((uintptr_t)((buf0 + bufsize + (pagesize-1)))
      & ~(pagesize-1));
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, small_send_buf,
      sizeof(small_send_buf), prot_id, 0, STAG_R,
      &send_stag, &send_mr);
    if (ret)
	iwarp_error(ret, "NSMR register send");
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, small_recv_buf,
      sizeof(small_recv_buf), prot_id, 0, STAG_W,
      &recv_stag, &recv_mr);
    if (ret)
	iwarp_error(ret, "NSMR register recv");
    ret = iwarp_nsmr_register(rnic_hndl, VA_ADDR_T, bufmalloc,
      (bufsize + pagesize-1)*2, prot_id, 0, STAG_R|STAG_W,
      &buf_stag, &buf_mr);
    if (ret)
	iwarp_error(ret, "NSMR register rdma");

    /* prepost receive before connection up */
    sgl.sge[0].to = uint64_from_ptr(small_recv_buf);
    sgl.sge[0].length = sizeof(small_recv_buf);
    sgl.sge[0].stag = recv_stag;
    sgl.sge_count = 1;
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.sgl = &sgl;
    rwr.wr_type = IWARP_WR_TYPE_RECV;
    rwr.cq_type = SIGNALED;
    ret = iwarp_qp_post_rq(rnic_hndl, qp_id, &rwr);
    if (ret)
	iwarp_error(ret, "prepost recv");

    /* TCP connection */
    if (am_server) {
	ret = iwarp_qp_passive_connect(rnic_hndl, port, qp_id, "passive",
	  remote_priv_data, sizeof(remote_priv_data));
	if (ret)
	    iwarp_error(ret, "passive connect");
    } else {
	ret = iwarp_qp_active_connect(rnic_hndl, port, masterhost,
	  300000, 1, qp_id, "active", remote_priv_data,
	  sizeof(remote_priv_data));
	if (ret)
	    iwarp_error(ret, "active connect");
    }

    /* pass data about the buf to him */
    debug(2, "%s: sending to %p len %d stag %d", __func__, buf0, bufsize,
      buf_stag);
    my_sge.to = uint64_from_ptr(buf0);
    my_sge.length = bufsize;
    my_sge.stag = buf_stag;
    memcpy(small_send_buf, &my_sge, sizeof(my_sge));
    sgl.sge[0].to = uint64_from_ptr(small_send_buf);
    sgl.sge[0].length = sizeof(my_sge);
    sgl.sge[0].stag = send_stag;
    sgl.sge_count = 1;
    swr.wr_id = uint64_from_ptr(&swr);
    swr.sgl = &sgl;
    swr.wr_type = IWARP_WR_TYPE_SEND;
    swr.cq_type = SIGNALED;

    /* server info -> client */
    if (am_server) {
	debug(1, "%s: server send info", __func__);
	ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
	if (ret)
	    iwarp_error(ret, "post send");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if (ret)
	    iwarp_error(ret, "wait send");
    } else {
	debug(1, "%s: client recv info", __func__);
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if (ret)
	    iwarp_error(ret, "wait recv");
    }

    /* client info -> server */
    if (am_server) {
	debug(1, "%s: server recv info", __func__);
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if (ret)
	    iwarp_error(ret, "wait recv");
    } else {
	debug(1, "%s: client send info", __func__);
	ret = iwarp_qp_post_sq(rnic_hndl, qp_id, &swr);
	if (ret)
	    iwarp_error(ret, "post send");
	ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
	if (ret)
	    iwarp_error(ret, "wait send");
    }

    memcpy(&his_sge, small_recv_buf, sizeof(his_sge));
    debug(2, "%s: received to 0x%Lx len %d stag %d", __func__, his_sge.to,
      his_sge.length, his_sge.stag);
}

static void shutdown_iwarp(void)
{
    int ret;

    ret = iwarp_deallocate_stag(rnic_hndl, send_stag);
    if (ret)
	iwarp_error(ret, "dealloc send stag");
    ret = iwarp_deallocate_stag(rnic_hndl, recv_stag);
    if (ret)
	iwarp_error(ret, "dealloc recv stag");
    ret = iwarp_deallocate_stag(rnic_hndl, buf_stag);
    if (ret)
	iwarp_error(ret, "dealloc buf stag");

    ret = iwarp_deregister_mem(rnic_hndl, prot_id, send_mr);
    if (ret)
	iwarp_error(ret, "deregister send mem");
    ret = iwarp_deregister_mem(rnic_hndl, prot_id, recv_mr);
    if (ret)
	iwarp_error(ret, "deregister recv mem");
    ret = iwarp_deregister_mem(rnic_hndl, prot_id, buf_mr);
    if (ret)
	iwarp_error(ret, "deregister buf mem");

    free(bufmalloc);

    ret = iwarp_qp_disconnect(rnic_hndl, qp_id);
    if (ret)
	iwarp_error(ret, "QP disconnect");

    ret = iwarp_qp_destroy(rnic_hndl, qp_id);
    if (ret)
	iwarp_error(ret, "QP destroy");

    ret = iwarp_cq_destroy(rnic_hndl, cq_hndl);
    if (ret)
	iwarp_error(ret, "CQ destroy");

    ret = iwarp_pd_deallocate(rnic_hndl, prot_id);
    if (ret)
	iwarp_error(ret, "PD deallocate");

    ret = iwarp_rnic_close(rnic_hndl);
    if (ret)
	iwarp_error(ret, "RNIC close");

}

static void post_small_recv(iwarp_wr_t *rwr)
{
    int ret;

    ret = iwarp_qp_post_rq(rnic_hndl, qp_id, rwr);
    if (ret)
	iwarp_error(ret, "%s: post recv", __func__);
}

static void post_small_send(iwarp_wr_t *swr)
{
    int ret;

    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, swr);
    if (ret)
	iwarp_error(ret, "%s: qp post sq", __func__);
}

static void post_rdma_write(iwarp_wr_t *swr)
{
    int ret;

    ret = iwarp_qp_post_sq(rnic_hndl, qp_id, swr);
    if (ret)
	iwarp_error(ret, "%s: rdma send", __func__);
}

static void reap_cq(void)
{
    int ret;
    iwarp_work_completion_t wc;

    ret = iwarp_cq_poll(rnic_hndl, cq_hndl, IWARP_INFINITY, 0, &wc);
    if (ret)
	iwarp_error(ret, "%s: cq poll", __func__);
}

#ifdef KERNEL_IWARP
static void reap_cq_blocking(void)
{
    int ret;
    iwarp_work_completion_t wc;

    for (;;) {
	ret = iwarp_cq_poll_block(rnic_hndl, cq_hndl, qp_id, &wc);
	if (ret)
	    iwarp_error(ret, "%s: cq poll", __func__);

	if (ret == 0) {
	    debug(2, "%s: got cqe", __func__);
	    break;
	}
    }
}
#else
static void reap_cq_blocking(void)
{
    int ret;
    cqe_t cqe;

    for (;;) {
	ret = cq_consume(cq_hndl, &cqe);
	if (ret == 0) {
	    debug(2, "%s: got cqe", __func__);
	    break;
	}
	ret = mpa_block();
    }
}
#endif

/*
 * Do a spray test.
 */
static void spray(void)
{
    int i, j;
    int skip = 10;  /* warm-up before measurement begins */
    const int window_size = 64;   /* how many messages in flight */
    double *v = NULL;
    iwarp_wr_t swr, rwr, rdma_wr;
    iwarp_sgl_t ssgl, rsgl, rdma_sgl, rdma_remote_sgl;

    debug(1, __func__);

    /* disable for debugging */
    if (iters < skip)
	skip = 0;

    rsgl.sge[0].to = uint64_from_ptr(small_recv_buf);
    rsgl.sge[0].length = 4;
    rsgl.sge[0].stag = recv_stag;
    rsgl.sge_count = 1;
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.sgl = &rsgl;
    rwr.wr_type = IWARP_WR_TYPE_RECV;
    rwr.cq_type = SIGNALED;

    ssgl.sge[0].to = uint64_from_ptr(small_send_buf);
    ssgl.sge[0].length = 4;
    ssgl.sge[0].stag = send_stag;
    ssgl.sge_count = 1;
    swr.wr_id = uint64_from_ptr(&swr);
    swr.sgl = &ssgl;
    swr.wr_type = IWARP_WR_TYPE_SEND;
    swr.cq_type = SIGNALED;

    if (am_server) {
	rdma_sgl.sge[0].to = uint64_from_ptr(buf0);
	rdma_sgl.sge[0].length = bufsize;
	rdma_sgl.sge[0].stag = my_sge.stag;
	rdma_sgl.sge_count = 1;
	rdma_remote_sgl.sge[0].to = his_sge.to;
	rdma_remote_sgl.sge[0].length = bufsize;
	rdma_remote_sgl.sge[0].stag = his_sge.stag;
	rdma_remote_sgl.sge_count = 1;
	rdma_wr.wr_id = uint64_from_ptr(&rdma_wr);
	rdma_wr.sgl = &rdma_sgl;
	rdma_wr.remote_sgl = &rdma_remote_sgl;
	rdma_wr.wr_type = IWARP_WR_TYPE_RDMA_WRITE;
	rdma_wr.cq_type = SIGNALED;
    }

    memset(buf0, 0, bufsize);
    memset(buf1, 1, bufsize);

    if (am_server) {
	uint64_t tstart, tend;

	v = Malloc(iters * sizeof(*v));
	/* wait for ready-to-begin ping */
	debug(2, "%s: server wait ready-to-begin ping", __func__);
	post_small_recv(&rwr);
	reap_cq();
	for (i=0; i<iters+skip; i++) {
	    post_small_recv(&rwr);
	    rdtsc(tstart);
	    /* post all messages */
	    rdma_sgl.sge[0].to = uint64_from_ptr(buf0);  /* start with 0s */
	    for (j=0; j<window_size-1; j++)
		post_rdma_write(&rdma_wr);
	    rdma_sgl.sge[0].to = uint64_from_ptr(buf1);  /* switch to 1s */
	    post_rdma_write(&rdma_wr);
	    /* wait for completions */
	    debug(2, "%s: server reap completions", __func__);
	    for (j=0; j<window_size; j++)
		reap_cq();
	    if (blocking) {
		/* send him a message */
		debug(2, "%s: server post small send after rdmas", __func__);
		post_small_send(&swr);
		reap_cq();
	    }
	    /* wait for his ping */
	    debug(2, "%s: server wait for return ping", __func__);
	    reap_cq();
	    rdtsc(tend);
	    if (i >= skip)
		v[i-skip] = elapsed_wall_time(tstart, tend, SECONDS);
	}
    } else {
	if (blocking) {
	    /* post a small recv for his completion for blocking case */
	    post_small_recv(&rwr);
	}
	/* reset byte */
	buf0[bufsize-1] = 0;
	/* ready to begin ping */
	sleep(1);  /* make sure he is preposted */
	debug(2, "%s: client send ready-to-begin ping", __func__);
	post_small_send(&swr);
	reap_cq();
	debug(2, "%s: client wait for last rdma", __func__);
	for (i=0; i<iters+skip; i++) {
	    if (blocking) {
		reap_cq_blocking();
		post_small_recv(&rwr);
	    } else {
		/* actively poll for the last message */
		for (;;) {
		    if (*(volatile char *)(buf0 + bufsize-1) != 0)
			break;
		    iwarp_rnic_advance(rnic_hndl);
		}
	    }
	    /* reset byte */
	    buf0[bufsize-1] = 0;
	    /* send ping */
	    debug(2, "%s: client return ping", __func__);
	    post_small_send(&swr);
	    reap_cq();
	}
    }

    if (am_server) {
	double avg = 0., stdev = 0.;
	if (iters > 0) {

	    /* convert time to Mb/s (base 10 mega) */
	    for (i=0; i<iters; i++)
		v[i] = 1.0e-6 * (8*bufsize) * window_size / v[i];

	    /* toss out any "outliers" < 90% max */
	    if (1) {
		double max = 0.;
		for (i=0; i<iters; i++)
		    if (v[i] > max)
			max = v[i];
		max = 0.9 * max;
		for (i=0; i<iters; i++)
		    if (v[i] < max) {
			--iters;
			for (j=i; j<iters; j++)
			    v[j] = v[j+1];
			--i;
		    }
	    }

	    for (i=0; i<iters; i++)
		avg += v[i];
	    avg /= iters;

	    if (iters > 1) {
		for (i=0; i<iters; i++) {
		    double diff = v[i] - avg;
		    stdev += diff * diff;
		}
		stdev = sqrt(stdev / (iters - 1));
	    }
	}
	/* debugging, look for outliers */
	if (0) {
	    for (i=0; i<iters; i++)
		printf("%9d %12.3f\n", bufsize, v[i]);
	}
	printf("%9d %12.3f +- %12.3f\n", bufsize, avg, stdev);
	free(v);
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    connect_iwarp();
    spray();
    shutdown_iwarp();
    return 0;
}
