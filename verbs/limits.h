/*
* Header file for Verbs layer which defines all limits and
*RNIC attributes
*
*$Id: limits.h 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*/

#define VENDOR_NAME "OSC iwarp"
#define VERSION 1
#define MAX_QP 10
#define MAX_WRQ 250
#define MAX_PROT_DOMAIN 10
#define HOST_MAX 256
//~ #define MAX_SQ_DEPTH 256
//~ #define MAX_RQ_DEPTH 256
#define MAX_SGE 1
#define MAX_S_SGL 10
#define MAX_RDMA_W_SGL 10  /*Ensure this is always at least as big as MAX_*_SGL*/
#define MAX_R_SGL 10
#define MAX_IRD 1
#define MAX_ORD 1
#define BIND_MEM_WINDOW_ENABLE 0
#define ENABLE_ZERO_STAG 0
#define ENABLE_CQE_HANDLER 0
#define MAX_CQ_DEPTH 1024

