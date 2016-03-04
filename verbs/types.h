/*
* Header file for Verbs layer which defines all data types
*
*$Id: types.h 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*Lots of influence from ccil api code (Ammasso)
*
*
*/


/*Notes
Remove primitive data types and substitute iwarp_ specific typedef'd types for them

*/
#ifndef TYPES_INC
#define TYPES_INC
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define DEFAULT_STREAM 0
#define IWARP_INFINITY -1

typedef void* iwarp_context_t;
typedef void* iwarp_tagged_t;
typedef int iwarp_bool_t;
typedef int iwarp_stag_key_t;
typedef uint64_t iwarp_wr_id_t;
typedef void* iwarp_local_mem_addr_t;
typedef mem_desc_t iwarp_mem_desc_t;
typedef stag_t iwarp_stag_index_t;

/*iwarp verbs library status codes*/
#define ERRNO_ENTRY(name,init) name init,
typedef enum {
#include "errno.h"
} iwarp_status_t;
#undef ERRNO_ENTRY

typedef enum {
    IWARP_WR_TYPE_SEND,
    IWARP_WR_TYPE_RECV,
    IWARP_WR_TYPE_RDMA_WRITE,
    IWARP_WR_TYPE_RDMA_READ
} iwarp_wr_work_t;

typedef enum {
    IWARP_PASSIVE_SERVER = 0,
    IWARP_ACTIVE_CLIENT = 1
} iwarp_host_t;


typedef enum {
    SIGNALED,
    UNSIGNALED,
} iwarp_wr_cq_t;


typedef enum {
    PAGE_MODE = 0,
    BLOCK_MODE  /*NOT supporting BLOCK MODE currently*/
} iwarp_pblmode_t;

typedef enum {
    VA_ADDR_T = 0,
    BLOCK_ADDR_T
}iwarp_addr_t;

typedef enum {
    IWARP_WR_SUCCESS,
    IWARP_WR_FAILURE,
}iwarp_wr_status_t;


typedef enum {
    REMOTE_READ = STAG_R,
    REMOTE_WRITE = STAG_W,
    //~ LOCAL_READ = 4 | REMOTE_READ,
    //~ LOCAL_WRITE = 8 | REMOTE_WRITE,
    //~ BIND_MEM_WINDOW = 16
}iwarp_access_control_t;



#ifdef KERNEL_IWARP
    typedef uint64_t iwarp_cq_handle_t;
#else
    typedef cq_t *iwarp_cq_handle_t;   /*cq_t is exported from ../cq.h  - The parent directories cq code */
#endif


/**********************/
/*Protection Domain*/
/**********************/
typedef int iwarp_prot_id;

typedef struct {  /*The protection domain */
    iwarp_prot_id id;
    int available;  /*available for allocation not necessarily in use*/
    int in_use;     /*how many resources actually being used */
}iwarp_prot_domain_t;

/**********/
/*QP Stuff*/
/**********/
typedef int iwarp_qp_handle_t;

typedef struct { /*All of the QP's properties*/
    iwarp_cq_handle_t sq_cq;
    iwarp_cq_handle_t rq_cq;
    uint32_t sq_depth;
    uint32_t rq_depth;
    iwarp_bool_t rdma_r_enable;
    iwarp_bool_t rdma_w_enable;
    iwarp_bool_t bind_mem_window_enable;
    uint32_t send_sgl_max;
    uint32_t rdma_w_sgl_max;
    uint32_t recv_sgl_max;
    uint32_t ord;
    uint32_t ird;
    iwarp_prot_id prot_d_id;
    iwarp_bool_t zero_stag_enable;
    iwarp_bool_t disable_mpa_markers;
    iwarp_bool_t disable_mpa_crc;

}iwarp_qp_attrs_t;

typedef struct { /*The QP Structure its self - each QP has its own socket*/
    int socket_fd;
    iwarp_bool_t available;
    iwarp_qp_attrs_t *attributes;
    iwarp_bool_t connected;
    int pre_connection_posts;
}iwarp_qp_t;







/*Connection Stuff*/
typedef int iwarp_port_t;


/*******************************/
/*Work Request Processing Stuff*/
/*******************************/
/* scatter-gather entry: memory descriptor for a send or a recv */
typedef struct IWARP_SGE_T{
    uint32_t length;
    iwarp_stag_index_t stag;
    uint64_t to;
    //~ struct IWARP_SGE_T *next_sge; /*pointer to next SGE*/
}iwarp_sge_t;


/* scatter-gather list */
typedef struct {
    iwarp_sge_t sge[MAX_SGE];
    uint32_t sge_count;  /* keep this field */
}iwarp_sgl_t;




typedef struct {
    iwarp_wr_id_t wr_id;
    iwarp_sgl_t *sgl;
    iwarp_sgl_t *remote_sgl;
    iwarp_wr_work_t wr_type;
    iwarp_wr_cq_t cq_type;
    iwarp_local_mem_addr_t local_addr;
    //~ iwarp_stag_index_t remote_stag;
    //~ iwarp_stag_index_t local_stag;
    //~ iwarp_to_t to;
}iwarp_wr_t;

typedef struct {
    iwarp_wr_t queue[MAX_WRQ];
    int size;
}iwarp_wr_q_t;



/**************/
/*RNIC Stuff*/
/**************/
typedef uint64_t iwarp_rnic_handle_t;  /*The RNIC handle we pass around*/

typedef struct { /*The actual RNIC structure*/
    iwarp_qp_t qp_index[MAX_QP];
    iwarp_prot_domain_t pd_index[MAX_PROT_DOMAIN];
    iwarp_wr_q_t recv_q;  /* queue to hold posts before connection up */
    int fd; /*just a simple old file descriptor to keep track of what our RNIC is open on,*/
} iwarp_rnic_t;


typedef struct { /*The RNIC's properties*/
    char *vendor_name;
    int version;
    int max_qp;
    int max_wrq;
    int max_srq;
    char local_hostname[HOST_MAX];
    char official_hostname[HOST_MAX];
    int address_type;
    int length;
    char *address;
    int fd;


}iwarp_rnic_query_attrs_t;

/***************************/
/*Completion Queue Stuff*/
/***************************/
//~ typedef struct {

    //~ struct iwarp_cq_t *next;
//~ }iwarp_cq_t;

typedef struct {
    iwarp_wr_id_t wr_id;
    iwarp_qp_handle_t qp_hndl;
    iwarp_wr_work_t wr_type;
    iwarp_wr_status_t status;
    uint32_t bytes_recvd;
    iwarp_bool_t stag_invalidate;
    iwarp_stag_index_t stag;
}iwarp_work_completion_t;

#endif

