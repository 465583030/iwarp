/*
 * Unidirectional spray bandwidth test.  Ammasso API version.
 *
 * $Id: uni-spray-bw-ams.c 666 2007-08-03 15:12:59Z dennis $
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
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "util.h"
#include "verbs/perfmon.h"
#include "ccil_api.h"

static int iters = 30;
static int bufsize = 1024;
static cc_inet_port_t port = 4210;
static int blocking = 0;
static const char *masterhost = NULL;
static char *buf0, *buf1, *bufmalloc;
static char small_send_buf[64], small_recv_buf[64];

static cc_rnic_handle_t rnic_hndl;
static cc_pdid_t prot_id;
static cc_cq_handle_t cq_hndl;
static cc_qp_handle_t qp_id;
static cc_stag_index_t send_stag, recv_stag, buf_stag;

/* locks */
static pthread_mutex_t cqeh_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cqeh_cond = PTHREAD_COND_INITIALIZER;

static int am_server;
static struct {
    uint32_t length;
    uint32_t stag;
    uint64_t to;
} my_sge, his_sge;

static void reap_cq(void);

/*
 * Print out the usage syntax.
 */
static void usage(void)
{
    fprintf(stderr,
      "Usage: %s [options] -r <self>\n"
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
	    cp += len;
	    masterhost = find_optarg(cp, &argc, &argv, "r");
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
static void am_error(int ret, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", cc_strerror(ret));
    exit(1);
}

static cc_ep_handle_t cr_ep_hndl = 0;
static int connected = 0;

static void async_evh(cc_rnic_handle_t rh ATTR_UNUSED, cc_event_record_t *er,
  void *uc ATTR_UNUSED)
{
    int ret;

    if (er->event_id == CCAE_CONNECTION_REQUEST) {
	debug(2, "%s: storing connection request handle", __func__);
	cr_ep_hndl = er->event_data.connection_request.cr_handle;
    } else if (er->event_id == CCAE_ACTIVE_CONNECT_RESULTS) {
	if (er->event_data.active_connect_results.status
	  == CC_CONN_STATUS_SUCCESS) {
	    debug(2, "%s: connected", __func__);
	    connected = 1;
	} else {
	    error("%s: connection failed", __func__);
	}
    } else if (er->event_id == CCAE_BAD_CLOSE)
	error("%s: bad close", __func__);
    else
	error("%s: unknown event id %d", __func__, er->event_id);

    /* done with this */
    ret = cc_eh_set_async_handler(rnic_hndl, NULL, NULL);
    if (ret)
	am_error(ret, "%s: unset async handler", __func__);
}

static void
cqe_evh(cc_rnic_handle_t rnic, cc_cq_handle_t cqh, void *context ATTR_UNUSED)
{
	cc_status_t ret;

	/* need to request notification for getting rnic's response */
	ret = cc_cq_request_notification(rnic, cqh,
	  CC_CQ_NOTIFICATION_TYPE_NEXT);
	if (ret != CC_OK)
		am_error(ret, "%s: cc_cq_request_notification", __func__);

	/* signal the waiting thread */
	pthread_mutex_lock(&cqeh_lock);
	pthread_cond_signal(&cqeh_cond);
	pthread_mutex_unlock(&cqeh_lock);
}


#define uint64_from_ptr(p) ((uint64_t)(uintptr_t)(p))

/*
 * Open the NIC, set up everything, connect to the other side.
 */
static void connect_iwarp(void)
{
    int ret;
    cc_qp_create_attrs_t qp_attrs;
    cc_data_addr_t sgl;
    cc_rq_wr_t rwr;
    cc_sq_wr_t swr;
    unsigned long pagesize = getpagesize();
    cc_uint32_t depth = 1024;
    cc_qp_id_t qp_ignore_id;
    cc_uint32_t num_posted;
    cc_eh_ce_handler_id_t eh_id = 0;

    debug(1, __func__);
    ret = cc_rnic_open(0, CC_PBL_PAGE_MODE, NULL, &rnic_hndl);
    if (ret)
	am_error(ret, "RNIC open");

    ret = cc_eh_set_async_handler(rnic_hndl, async_evh, NULL);
    if (ret)
	am_error(ret, "async handler");

    if (blocking) {
	ret = cc_eh_set_ce_handler(rnic_hndl, cqe_evh, &eh_id);
	if (ret)
		am_error(ret, "cqe handler");
    }

    ret = cc_pd_alloc(rnic_hndl, &prot_id);
    if (ret)
	am_error(ret, "PD allocate");

    ret = cc_cq_create(rnic_hndl, &depth, eh_id, NULL, &cq_hndl);
    if (ret)
	am_error(ret, "CQ create");

    if (blocking) {
	/* request notification for next event */
	ret = cc_cq_request_notification(rnic_hndl, cq_hndl,
	  CC_CQ_NOTIFICATION_TYPE_NEXT);
	if (ret)
	    am_error(ret, "%s: cc_cq_request_notification", __func__);
    }

    memset(&qp_attrs, 0, sizeof(qp_attrs));
    qp_attrs.sq_cq = cq_hndl;
    qp_attrs.rq_cq = cq_hndl;
    qp_attrs.sq_depth = 100;
    qp_attrs.rq_depth = 100;
    qp_attrs.rdma_read_enabled = 1;
    qp_attrs.rdma_write_enabled = 1;
    qp_attrs.send_sgl_depth = 1;
    qp_attrs.recv_sgl_depth = 1;
    qp_attrs.rdma_write_sgl_depth = 1;
    qp_attrs.ord = 1;
    qp_attrs.ird = 1;
    qp_attrs.pdid = prot_id;
    qp_attrs.zero_stag_enabled = 0;

    ret = cc_qp_create(rnic_hndl, &qp_attrs, &qp_id, &qp_ignore_id);
    if (ret)
	am_error(ret, "QP create");

    /* register memory */
    bufmalloc = Malloc((bufsize + pagesize-1) * 2);
    buf0 = (char *)((uintptr_t)((bufmalloc + (pagesize-1))) & ~(pagesize-1));
    buf1 = (char *)((uintptr_t)((buf0 + bufsize + (pagesize-1)))
      & ~(pagesize-1));
    ret = cc_nsmr_register_virt(rnic_hndl, CC_ADDR_TYPE_VA_BASED,
      small_send_buf, sizeof(small_send_buf), prot_id, 0, 0,
      CC_ACF_LOCAL_READ, &send_stag);
    if (ret)
	am_error(ret, "NSMR register send");
    ret = cc_nsmr_register_virt(rnic_hndl, CC_ADDR_TYPE_VA_BASED,
      small_recv_buf, sizeof(small_recv_buf), prot_id, 0, 0,
      CC_ACF_LOCAL_WRITE, &recv_stag);
    if (ret)
	am_error(ret, "NSMR register recv");
    ret = cc_nsmr_register_virt(rnic_hndl, CC_ADDR_TYPE_VA_BASED, bufmalloc,
      (bufsize + pagesize-1)*2, prot_id, 0, 0,
      CC_ACF_LOCAL_READ|CC_ACF_REMOTE_WRITE, &buf_stag);
    if (ret)
	am_error(ret, "NSMR register rdma");

    /* prepost receive before connection up */
    sgl.to = uint64_from_ptr(small_recv_buf);
    sgl.length = sizeof(small_recv_buf);
    sgl.stag = recv_stag;
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.local_sgl.sge_list = &sgl;
    rwr.local_sgl.sge_count = 1;
    ret = cc_qp_post_rq(rnic_hndl, qp_id, &rwr, 1, &num_posted);
    if (ret)
	am_error(ret, "prepost recv");

    struct hostent *hp = gethostbyname(masterhost);
    if (!hp)
    	error("lookup host %s", masterhost);
    in_addr_t addr = *(in_addr_t *) hp->h_addr_list[0];

    /* TCP connection */
    if (am_server) {
	char self[] = "passive";
	cc_ep_handle_t listen_ep_hndl;
	port = htons(port);
	ret = cc_ep_listen_create(rnic_hndl, addr, &port, 3, NULL,
	  &listen_ep_hndl);
	if (ret)
	    am_error(ret, "listen create");
	while (!cr_ep_hndl) {
	    usleep(200000);
	}
	ret = cc_cr_accept(rnic_hndl, cr_ep_hndl, qp_id, sizeof(self), self);
	if (ret)
	    am_error(ret, "accept");
    } else {
	char self[] = "active";
	ret = cc_qp_connect(rnic_hndl, qp_id, addr, htons(port),
	  sizeof(self), self);
	if (ret)
	    am_error(ret, "active connect");
	while (!connected) {
	    usleep(200000);
	}
    }

    /* pass data about the buf to him */
    debug(2, "%s: sending to %p len %d stag %d", __func__, buf0, bufsize,
      buf_stag);
    my_sge.to = uint64_from_ptr(buf0);
    my_sge.length = bufsize;
    my_sge.stag = buf_stag;
    memcpy(small_send_buf, &my_sge, sizeof(my_sge));
    sgl.to = uint64_from_ptr(small_send_buf);
    sgl.length = sizeof(my_sge);
    sgl.stag = send_stag;
    swr.wr_id = uint64_from_ptr(&swr);
    swr.wr_u.send.local_sgl.sge_list = &sgl;
    swr.wr_u.send.local_sgl.sge_count = 1;
    swr.wr_type = CCWR_SEND;
    swr.signaled = 1;

    /* server info -> client */
    if (am_server) {
	debug(1, "%s: server send info", __func__);
	ret = cc_qp_post_sq(rnic_hndl, qp_id, &swr, 1, &num_posted);
	if (ret)
	    am_error(ret, "post send");
	reap_cq();
    } else {
	debug(1, "%s: client recv info", __func__);
	reap_cq();
    }

    /* client info -> server */
    if (am_server) {
	debug(1, "%s: server recv info", __func__);
	reap_cq();
    } else {
	debug(1, "%s: client send info", __func__);
	ret = cc_qp_post_sq(rnic_hndl, qp_id, &swr, 1, &num_posted);
	if (ret)
	    am_error(ret, "post send");
	reap_cq();
    }

    memcpy(&his_sge, small_recv_buf, sizeof(his_sge));
    debug(2, "%s: received to 0x%Lx len %d stag %d", __func__, his_sge.to,
      his_sge.length, his_sge.stag);
}

static void shutdown_iwarp(void)
{
    int ret;

    ret = cc_rnic_close(rnic_hndl);
    if (ret)
	am_error(ret, "rnic close");
}

static void post_small_recv(cc_rq_wr_t *rwr)
{
    int ret;
    cc_uint32_t num_posted;

    ret = cc_qp_post_rq(rnic_hndl, qp_id, rwr, 1, &num_posted);
    if (ret)
	am_error(ret, "%s: post recv", __func__);
}

static void post_small_send(cc_sq_wr_t *swr)
{
    int ret;
    cc_uint32_t num_posted;

    ret = cc_qp_post_sq(rnic_hndl, qp_id, swr, 1, &num_posted);
    if (ret)
	am_error(ret, "%s: qp post sq", __func__);
}

static void post_rdma_write(cc_sq_wr_t *swr)
{
    int ret;
    cc_uint32_t num_posted;

    ret = cc_qp_post_sq(rnic_hndl, qp_id, swr, 1, &num_posted);
    if (ret)
	am_error(ret, "%s: rdma send", __func__);
}

static void reap_cq(void)
{
    int ret;
    cc_wc_t wc;

    for (;;) {
	ret = cc_cq_poll(rnic_hndl, cq_hndl, &wc);
	if (ret == 0) {
	    if (wc.status == 0)
		break;
	    else if (wc.status == CCERR_INVALID_STAG)
		error("%s: work completion invalid stag", __func__);
	    else if (wc.status == CCERR_BASE_AND_BOUNDS_VIOLATION)
		error("%s: work completion base/bounds violation", __func__);
	    else if (wc.status == CCERR_ACCESS_VIOLATION)
		error("%s: work completion access violation", __func__);
	    else if (wc.status == CCERR_FLUSHED)
		error("%s: work completion flushed", __func__);
	    else
		error("%s: work completion bad status %d", __func__, wc.status);
	}
	if (ret == CCERR_CQ_EMPTY) {
	    if (blocking) {
		/* wait for signal from cq event handler */
		pthread_mutex_lock(&cqeh_lock);
		pthread_cond_wait(&cqeh_cond, &cqeh_lock);
		pthread_mutex_unlock(&cqeh_lock);
	    }
	    continue;
	}
	if (ret)
	    am_error(ret, "%s: cq poll", __func__);
    }
}

/*
 * Do a spray test.
 */
static void spray(void)
{
    int i, j;
    int skip = 10;  /* warm-up before measurement begins */
    const int window_size = 64;   /* how many messages in flight */
    double *v = NULL;
    cc_rq_wr_t rwr;
    cc_sq_wr_t swr, rdma_wr;
    cc_data_addr_t ssgl, rsgl, rdma_sgl;

    debug(1, __func__);

    /* disable for debugging */
    if (iters < skip)
	skip = 0;

    rsgl.to = uint64_from_ptr(small_recv_buf);
    rsgl.length = 4;
    rsgl.stag = recv_stag;
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.local_sgl.sge_list = &rsgl;
    rwr.local_sgl.sge_count = 1;

    /* must be valid stag and to even for 0-byte message;
     * won't send anything for 0-bytes either */
    ssgl.to = uint64_from_ptr(small_send_buf);
    ssgl.length = 4;
    ssgl.stag = send_stag;
    swr.wr_id = uint64_from_ptr(&swr);
    swr.wr_u.send.local_sgl.sge_list = &ssgl;
    swr.wr_u.send.local_sgl.sge_count = 1;
    swr.wr_type = CCWR_SEND;
    swr.signaled = 1;

    if (am_server) {
	rdma_sgl.to = uint64_from_ptr(buf0);
	rdma_sgl.length = bufsize;
	rdma_sgl.stag = my_sge.stag;
	rdma_wr.wr_u.rdma_write.remote_to = his_sge.to;
	rdma_wr.wr_u.rdma_write.remote_stag = his_sge.stag;
	rdma_wr.wr_u.rdma_write.read_fence = 0;
	rdma_wr.wr_id = uint64_from_ptr(&rdma_wr);
	rdma_wr.wr_u.rdma_write.local_sgl.sge_list = &rdma_sgl;
	rdma_wr.wr_u.rdma_write.local_sgl.sge_count = 1;
	rdma_wr.wr_type = CCWR_RDMA_WRITE;
	rdma_wr.signaled = 1;
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
	    rdma_sgl.to = uint64_from_ptr(buf0);  /* start with 0s */
	    for (j=0; j<window_size-1; j++)
		post_rdma_write(&rdma_wr);
	    rdma_sgl.to = uint64_from_ptr(buf1);  /* switch to 1s */
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
	sleep(1);  /* make sure he is preposted */
	/* ready to begin ping */
	debug(2, "%s: client send ready-to-begin ping", __func__);
	post_small_send(&swr);
	reap_cq();
	debug(2, "%s: client wait for last rdma", __func__);
	for (i=0; i<iters+skip; i++) {
	    if (blocking) {
		/* wait for final message */
		reap_cq();
		post_small_recv(&rwr);
	    } else {
		/* actively poll for the last message */
		for (;;) {
		    if (*(volatile char *)(buf0 + bufsize-1) != 0)
			break;
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
