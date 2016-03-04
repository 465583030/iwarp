/*
 * Unidirectional spray bandwidth test.  OpenFabrics with RDMACM API version.
 *
 * $Id: uni-spray-bw-openib-sw.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005-6 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "../openfab/infiniband/verbs.h"
#include "../openfab/rdma/rdma_cma.h"

#include "util.h"
#include "verbs/perfmon.h"

static int iters = 30;
static int bufsize = 1024;
static const int window_size = 64;   /* how many messages in flight */

static unsigned short int port = 4210;
static struct sockaddr_in skin;  /* ip addr */
static int blocking = 0;
static const char *masterhost = NULL;
static char *buf0, *buf1, *bufmalloc;
static char small_send_buf[64], small_recv_buf[64];

static struct ibv_context *rnic_hndl;
static struct ibv_pd *prot_id;
static struct ibv_cq *cq_hndl;
static struct ibv_qp *qp_id;
static struct rdma_event_channel *rdmacm_channel;
static struct rdma_cm_id *cma_id;
static uint32_t send_stag, recv_stag, buf_stag;
static struct ibv_mr *send_mr, *recv_mr, *rdma_mr;

static int am_server;
static struct {
    uint32_t length;
    uint32_t stag;
    uint64_t to;
} my_sge, his_sge;

static void post_small_send(struct ibv_send_wr *swr);
static void prepost_recv(void);
static void reap_cq(void);
static void ib_init(void);
static void ib_build_qp(void);

#define uint64_from_ptr(p) ((uint64_t)(uintptr_t)(p))

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
 * Error, fatal, with explicitly-given positive error number (like EINVAL).
 */
static void error_xerrno(int errnum, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "Error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", strerror(errnum));
    exit(1);
}

static void connection_machine(int num_connects)
{
    int ret;
    struct rdma_conn_param conn_param = {
	.responder_resources = 1,
	.initiator_depth = 1,
	.retry_count = 5,
    };

    debug(2, "%s: num_connects %d", __func__, num_connects);
    while (num_connects != 0) {
	struct rdma_cm_event *event;

	/* it mallocs the event for us */
	ret = rdma_get_cm_event(rdmacm_channel, &event);
	if (ret) {
	    if (ret == -1)
		error_errno("rdma_get_cm_event");
	    else
		error_xerrno(-ret, "rdma_get_cm_event");
	}

	switch (event->event) {

	/*
	 * Active-side events
	 */
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	    debug(2, "%s: addr resolved", __func__);

	    /* grab handle first time a valid event happens */
	    if (rnic_hndl == NULL) {
		rnic_hndl = event->id->verbs;
		debug(4, "Back in conn machine resolved %s on %d", rnic_hndl->swinfo->masterhost, rnic_hndl->swinfo->port);
		ib_init();
	    }

	    ret = rdma_resolve_route(event->id, 30000);
	    if (ret)
		error_xerrno(-ret, "rdma_resolve_route");
	    break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
	    debug(2, "%s: route resolved", __func__);
	    ib_build_qp();
	    prepost_recv();

	    ret = rdma_connect(event->id, &conn_param);
	    if (ret)
		error_xerrno(-ret, "rdma_connect");
	    break;

	case RDMA_CM_EVENT_REJECTED:
	    error("%s: connection rejected", __func__);

	case RDMA_CM_EVENT_ADDR_ERROR:
	    error("%s: address error", __func__);

	/*
	 * Passive-side events
	 */
	case RDMA_CM_EVENT_CONNECT_REQUEST:
	    debug(2, "%s: connect request", __func__);
	    /* grab handle first time a valid event happens */
	    if (rnic_hndl == NULL) {
		rnic_hndl = event->id->verbs;
		ib_init();
	    }
	    /* build a QP, then accept the connection */
	    cma_id = event->id;  /* different from cma_listen_id */
	    debug(2, "cma->port_num is %d", cma_id->port_num);
	    ib_build_qp();
	    prepost_recv();

	    /* accept it */
	    ret = rdma_accept(event->id, &conn_param);
	    if (ret) {
		if (ret == -1)
		    error_errno("rdma_accept");
		else
		    error_xerrno(-ret, "rdma_accept");
	    }

	    break;

	/*
	 * Either active or passive
	 */
	case RDMA_CM_EVENT_ESTABLISHED:
	    debug(2, "%s: established", __func__);
	    --num_connects;
	    break;

	case RDMA_CM_EVENT_DISCONNECTED:
	    error("%s: disconnected", __func__);

	default:
	    error("%s: unknown event %d", __func__, event->event);

	}

	if (event->event != RDMA_CM_EVENT_REJECTED)
	    rdma_ack_cm_event(event);
    }

}

/*
 * Use the RDMA CM to glue everybody together, with a
 * QP between each pair of hosts.
 */
static void
connect_iwarp(void)
{
    struct hostent *hp;
    int ret;
    struct rdma_cm_id *cma_listen_id;
    struct ibv_sge sgl;
    struct ibv_send_wr swr;

    debug(1, __func__);

    rdmacm_channel = rdma_create_event_channel();
    if (!rdmacm_channel)
	error("%s: failed to create RDMA CM event channel", __func__);

    if (blocking)
	error("%s: fixme to do blocking", __func__);

    /* will be set as part of connection negotiation */
    rnic_hndl = NULL;

    hp = gethostbyname(masterhost);
    if (!hp)
	error("host \"%s\" not resolvable", masterhost);
    memset(&skin, 0, sizeof(skin));
    skin.sin_family = hp->h_addrtype;
    skin.sin_port = htons(port);
    debug(2, "host byte order port is %d in net short skin.sin_port = %d", port, skin.sin_port);
    if (am_server) {

	skin.sin_addr.s_addr = INADDR_ANY;

	ret = rdma_create_id(rdmacm_channel, &cma_listen_id, NULL, RDMA_PS_TCP);
	if (ret)
	    error_xerrno(-ret, "rdma_create_id");

	debug(2, "value is %d", cma_listen_id->port_num);
	debug(2, "cma_listen_id is pointing to %p", cma_listen_id);

	/* Gack, error values from these calls are hard to divine. */
	ret = rdma_bind_addr(cma_listen_id, (struct sockaddr *) &skin);
	if (ret) {
	    if (ret == -1)
		error_errno("rdma_bind_addr");
	    else
		error_xerrno(-ret, "rdma_bind_addr");
	}

	/* 0 means maximum backlog */
	ret = rdma_listen(cma_listen_id, 0);
	if (ret) {
	    if (ret == -1)
		error_errno("rdma_listen");
	    else
		error_xerrno(-ret, "rdma_listen");
	}

    } else {

	memcpy(&skin.sin_addr, hp->h_addr_list[0], hp->h_length);

	ret = rdma_create_id(rdmacm_channel, &cma_id, NULL, RDMA_PS_TCP);
	if (ret)
	    error_xerrno(-ret, "rdma_create_id");

	debug(2, "value is %d", cma_id->port_num);

	ret = rdma_resolve_addr(cma_id, NULL, (struct sockaddr *) &skin,
				30000);  /* 30 sec timeout */
	if (ret)
	    error_xerrno(-ret, "rdma_resolve_addr");
    }

    connection_machine(1);

    if (am_server) {
	rdma_destroy_id(cma_listen_id);  /* done listening */
	if (rnic_hndl == NULL)
	    error("%s: no one connected to master", __func__);
    } else {
	if (rnic_hndl == NULL)
	    error("%s: never connected to master", __func__);
    }
    debug(2, "connected!");


    /* now both have one posted receive on their respective QPs */

    /* pass data about the buf to him */
    debug(2, "%s: sending to %p len %d stag %d", __func__, buf0, bufsize,
      buf_stag);
    my_sge.to = uint64_from_ptr(buf0);
    my_sge.length = bufsize;
    my_sge.stag = buf_stag;
    memcpy(small_send_buf, &my_sge, sizeof(my_sge));
    sgl.addr = uint64_from_ptr(small_send_buf);
    sgl.length = sizeof(my_sge);
    sgl.lkey = send_stag;
    memset(&swr, 0, sizeof(swr));
    swr.wr_id = uint64_from_ptr(&swr);
    swr.sg_list = &sgl;
    swr.num_sge = 1;
    swr.opcode = IBV_WR_SEND;
    swr.send_flags = IBV_SEND_SIGNALED;

    /* server info -> client */
    if (am_server) {
	debug(1, "%s: server send info", __func__);
	post_small_send(&swr);
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
	post_small_send(&swr);
	reap_cq();
    }

    memcpy(&his_sge, small_recv_buf, sizeof(his_sge));
    debug(2, "%s: received to 0x%Lx len %d stag %d", __func__, his_sge.to,
      his_sge.length, his_sge.stag);

}

/*
 * Open the NIC.
 */
static void ib_init(void)
{
    int ret;
    struct ibv_device_attr device_attr;
    int cqe_num;

    debug(2, "%s", __func__);

    /* build a protection domain */
    prot_id = ibv_alloc_pd(rnic_hndl);
    if (!prot_id)
	error("%s: ibv_alloc_pd", __func__);

    /* see how many cq entries we are allowed to have */
    ret = ibv_query_device(rnic_hndl, &device_attr);
    if (ret < 0)
	error_errno("%s: ibv_query_device", __func__);

    debug(4, "%s: max %d completion queue entries", __func__,
          device_attr.max_cqe);
    cqe_num = 100;
    if (device_attr.max_cqe < cqe_num) {
	cqe_num = device_attr.max_cqe;
	warning("%s: hardly enough completion queue entries %d, hoping for %d",
	  __func__, device_attr.max_cqe, cqe_num);
    }

    /* build a CQ (ignore actual number returned) */
    debug(4, "%s: asking for %d completion queue entries", __func__, cqe_num);
    cq_hndl = ibv_create_cq(rnic_hndl, cqe_num, NULL, NULL, 0);
    if (!cq_hndl)
	error("%s: ibv_create_cq", __func__);
}

/*
 * Register mem and build the qp.
 */
static void ib_build_qp(void)
{
    int ret;
    struct ibv_qp_init_attr qp_init_attr;
    unsigned long pagesize = getpagesize();

    debug(1, __func__);

    /* register memory */
    bufmalloc = Malloc((bufsize + pagesize-1) * 2);
    buf0 = (char *)((uintptr_t)((bufmalloc + (pagesize-1))) & ~(pagesize-1));
    buf1 = (char *)((uintptr_t)((buf0 + bufsize + (pagesize-1)))
      & ~(pagesize-1));
    send_mr = ibv_reg_mr(prot_id, small_send_buf, sizeof(small_send_buf),
                        IBV_ACCESS_LOCAL_WRITE);
    if (!send_mr)
	error("%s: ibv_reg_mr send", __func__);
    send_stag = send_mr->lkey;

    debug(2, "registered send buffer now trying the rest");

    recv_mr = ibv_reg_mr(prot_id, small_recv_buf, sizeof(small_recv_buf),
                         IBV_ACCESS_LOCAL_WRITE);
    if (!recv_mr)
	error("%s: ibv_reg_mr recv", __func__);
    recv_stag = recv_mr->lkey;

    rdma_mr = ibv_reg_mr(prot_id, bufmalloc, (bufsize + pagesize-1) * 2,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!rdma_mr)
	error("%s: ibv_reg_mr rdma", __func__);
    buf_stag = rdma_mr->lkey;

    debug(2, "now ready to build qp");

    /* build qp */
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    /* wire both send and recv to the same CQ */
    qp_init_attr.send_cq         = cq_hndl;
    qp_init_attr.recv_cq         = cq_hndl;
    qp_init_attr.cap.max_send_wr = window_size + 1;  /* outstanding WQEs */
    qp_init_attr.cap.max_recv_wr = window_size + 1;
    qp_init_attr.cap.max_send_sge = 4;  /* scatter/gather entries */
    qp_init_attr.cap.max_recv_sge = 4;
    qp_init_attr.qp_type = IBV_QPT_RC;
    /* only generate completion queue entries if requested */
    qp_init_attr.sq_sig_all = 0;
    ret = rdma_create_qp(cma_id, prot_id, &qp_init_attr);
    if (ret)
	error_xerrno(-ret, "%s: rdma_create_qp", __func__);
    qp_id = cma_id->qp;

    debug(2, "QP built qp_id is %d", qp_id->sw_qp);
}


static void shutdown_iwarp(void)
{
    int ret;

    ret = ibv_dereg_mr(send_mr);
    if (ret)
    	error_xerrno(ret, "%s: ibv_dereg_mr send", __func__);
    ret = ibv_dereg_mr(recv_mr);
    if (ret)
    	error_xerrno(ret, "%s: ibv_dereg_mr recv", __func__);
    ret = ibv_dereg_mr(rdma_mr);
    if (ret)
    	error_xerrno(ret, "%s: ibv_dereg_mr rdma_wr", __func__);
    /* rdma_destroy_qp ignores return value */
    ret = ibv_destroy_qp(qp_id);
    if (ret)
    	error_xerrno(ret, "%s: ib_destroy_qp", __func__);
    ret = ibv_destroy_cq(cq_hndl);
    if (ret)
	error_xerrno(ret, "%s: ibv_destroy_cq");
    ret = ibv_dealloc_pd(prot_id);
    if (ret)
	error_xerrno(ret, "%s: ibv_dealloc_pd");
    rdma_destroy_event_channel(rdmacm_channel);
}

static void post_small_recv(struct ibv_recv_wr *rwr)
{
    struct ibv_recv_wr *bad_wr;
    int ret;

    ret = ibv_post_recv(qp_id, rwr, &bad_wr);
    if (ret)
	error_xerrno(ret, "%s: ibv_post_recv", __func__);
}

/* prepost receive before connection up */
static void prepost_recv(void)
{
    struct ibv_sge sgl;
    struct ibv_recv_wr rwr;

    sgl.addr = uint64_from_ptr(small_recv_buf);
    sgl.length = sizeof(small_recv_buf);
    sgl.lkey = recv_stag;

    memset(&rwr, 0, sizeof(rwr));
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.sg_list = &sgl;
    rwr.num_sge = 1;
    rwr.next = NULL;
    post_small_recv(&rwr);
}

static void post_small_send(struct ibv_send_wr *swr)
{
    struct ibv_send_wr *bad_wr;
    int ret;

    ret = ibv_post_send(qp_id, swr, &bad_wr);
    if (ret)
	error_xerrno(ret, "%s: ibv_post_send", __func__);
}

static void post_rdma_write(struct ibv_send_wr *swr)
{
    struct ibv_send_wr *bad_wr;
    int ret;

    ret = ibv_post_send(qp_id, swr, &bad_wr);
    if (ret)
	error_xerrno(ret, "%s: ibv_post_send", __func__);
}

//~ /*
 //~ * Return string form of work completion status field.
 //~ */
#define CASE(e)  case e: s = #e; break
static const char *openib_wc_status_string(int status)
{
    const char *s = "(UNKNOWN)";

    switch (status) {
	CASE(IBV_WC_SUCCESS);
	CASE(IBV_WC_LOC_LEN_ERR);
	CASE(IBV_WC_LOC_QP_OP_ERR);
	CASE(IBV_WC_LOC_EEC_OP_ERR);
	CASE(IBV_WC_LOC_PROT_ERR);
	CASE(IBV_WC_WR_FLUSH_ERR);
	CASE(IBV_WC_MW_BIND_ERR);
	CASE(IBV_WC_BAD_RESP_ERR);
	CASE(IBV_WC_LOC_ACCESS_ERR);
	CASE(IBV_WC_REM_INV_REQ_ERR);
	CASE(IBV_WC_REM_ACCESS_ERR);
	CASE(IBV_WC_REM_OP_ERR);
	CASE(IBV_WC_RETRY_EXC_ERR);
	CASE(IBV_WC_RNR_RETRY_EXC_ERR);
	CASE(IBV_WC_LOC_RDD_VIOL_ERR);
	CASE(IBV_WC_REM_INV_RD_REQ_ERR);
	CASE(IBV_WC_REM_ABORT_ERR);
	CASE(IBV_WC_INV_EECN_ERR);
	CASE(IBV_WC_INV_EEC_STATE_ERR);
	CASE(IBV_WC_FATAL_ERR);
	CASE(IBV_WC_GENERAL_ERR);
    }
    return s;
}

static void reap_cq(void)
{
    int ret;
    struct ibv_wc wc;

    wc.status = 0;
    wc.opcode = 0;

    for (;;) {
	errno = 0;
	ret = ibv_poll_cq(cq_hndl, 1, &wc);
	if (ret < 0) {
	    if (errno)
		error_errno("%s: ibv_poll_cq", __func__);
	    else
		error("%s: ibv_poll_cq failed", __func__);
	}
	if (ret > 0)
	    break;
    }

    if (wc.status != IBV_WC_SUCCESS)
	error("%s: entry id 0x%lx opcode %d status %s", __func__,
	      wc.wr_id, wc.opcode, openib_wc_status_string(wc.status));

    if (wc.opcode == IBV_WC_RDMA_WRITE)
	debug(4, "%s: rdma write complete", __func__);
    else if (wc.opcode == IBV_WC_SEND)
	debug(4, "%s: send to %d complete", __func__);
    else if (wc.opcode == IBV_WC_RECV)
	debug(4, "%s: recv data from %d complete", __func__);
    else
	error("%s: cq entry id 0x%lx opcode %d unexpected", __func__,
	      wc.wr_id, wc.opcode);
}

/*
 * Do a spray test.
 */
static void spray(void)
{
    int i, j;
    int skip = 10;  /* warm-up before measurement begins */
    double *v = NULL;
    struct ibv_recv_wr rwr;
    struct ibv_send_wr swr, rdma_wr;
    struct ibv_sge ssgl, rsgl, rdma_sgl;

    debug(1, __func__);

    /* disable for debugging */
    if (iters < skip)
	skip = 0;

    rsgl.addr = uint64_from_ptr(small_recv_buf);
    rsgl.length = 4;
    rsgl.lkey = recv_stag;
    memset(&rwr, 0, sizeof(rwr));
    rwr.wr_id = uint64_from_ptr(&rwr);
    rwr.sg_list = &rsgl;
    rwr.num_sge = 1;

    /* must be valid stag and to even for 0-byte message;
     * won't send anything for 0-bytes either */
    ssgl.addr = uint64_from_ptr(small_send_buf);
    ssgl.length = 4;
    ssgl.lkey = send_stag;
    memset(&swr, 0, sizeof(swr));
    swr.wr_id = uint64_from_ptr(&swr);
    swr.sg_list = &ssgl;
    swr.num_sge = 1;
    swr.opcode = IBV_WR_SEND;
    swr.send_flags = IBV_SEND_SIGNALED;

    if (am_server) {
	rdma_sgl.addr = uint64_from_ptr(buf0);
	rdma_sgl.length = bufsize;
	rdma_sgl.lkey = my_sge.stag;
	memset(&rdma_wr, 0, sizeof(rdma_wr));
	rdma_wr.wr_id = uint64_from_ptr(&rdma_wr);
	rdma_wr.sg_list = &rdma_sgl;
	rdma_wr.num_sge = 1;
	rdma_wr.wr.rdma.remote_addr = his_sge.to;
	rdma_wr.wr.rdma.rkey = his_sge.stag;
	rdma_wr.opcode = IBV_WR_RDMA_WRITE;
	rdma_wr.send_flags = IBV_SEND_SIGNALED;
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
	    rdma_sgl.addr = uint64_from_ptr(buf0);  /* start with 0s */
	    for (j=0; j<window_size-1; j++)
		post_rdma_write(&rdma_wr);
	    rdma_sgl.addr = uint64_from_ptr(buf1);  /* switch to 1s */
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
		    /*need to have an advance rnic verb in OF as well for SW iWARP*/
		    ibv_advance_sw_rnic(rnic_hndl);
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
	debug(2, "Sending QUIT Message");
	post_small_send(&swr);
    }
    else{
	debug(2, "Waiting for QUIT Message");
	post_small_recv(&rwr);
	debug(2, "Reaping CQ for FINAL time");
	reap_cq();  /*now quite right, for some reason finds the previous event, a send, not a recv*/
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

