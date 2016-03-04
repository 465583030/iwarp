/*
 * Shared user/kernel structures for communication via chardev.
 *
 * $Id: user.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#ifndef __USER_H
#define __USER_H

#ifndef __KERNEL__
#define __user
#endif

enum user_command_type {
	IWARP_REGISTER_SOCK = 1,
	IWARP_SET_SOCK_ATTRS,
	IWARP_INIT_STARTUP,
	IWARP_DEREGISTER_SOCK,
	IWARP_POLL,
	IWARP_POLL_BLOCK,
	IWARP_CQ_CREATE,
	IWARP_CQ_DESTROY,
	IWARP_MEM_REG,
	IWARP_MEM_DEREG,
	IWARP_STAG_CREATE,
	IWARP_STAG_DESTROY,
	IWARP_SEND,
	IWARP_POST_RECV,
	IWARP_RDMA_WRITE,
	IWARP_RDMA_READ,
	IWARP_ENCOURAGE
};

struct user_register_sock {
	uint32_t cmd;  /* IWARP_REGISTER_SOCK */
	uint32_t fd;
	uint64_t scq_handle;
	uint64_t rcq_handle;
};

struct user_sock_attrs {
	uint32_t cmd;  /* IWARP_SET_SOCK_ATTRS */
	uint32_t fd;
	int use_crc;
	int use_mrkr;
};

struct user_init_startup {
	uint32_t cmd; /* IWARP_INIT_STARTUP */
	uint32_t fd;
	int is_initiator;
	char *pd_in; 	/* private data in */
	uint16_t len_in;/* len of inp pd */
	char *pd_out;	/* private data out */
	uint16_t len_out; /* len of out pd */
};

struct user_deregister_sock {
	uint32_t cmd;  /* IWARP_DEREGISTER_SOCK */
	uint32_t fd;
};

struct work_completion {
	uint64_t id;  /* returned opaque id */
	int32_t op;
	int32_t status;
	uint32_t msg_len;
};

struct user_poll {
	uint32_t cmd;  /* IWARP_POLL */
	uint64_t cq_handle;
	struct work_completion __user *wc;
};

struct user_poll_block {
	uint32_t cmd;  /* IWARP_POLL_BLOCK */
	uint32_t fd;
	uint64_t cq_handle;
	struct work_completion __user *wc;
};

struct user_cq_create {
	uint32_t cmd;  /* IWARP_CQ_CREATE */
	uint32_t depth;
	uint64_t *cq_handle;
};

struct user_cq_destroy {
	uint32_t cmd;  /* IWARP_CQ_DESTROY */
	uint64_t cq_handle;
};

struct user_mem_reg {
	uint32_t cmd; /*IWARP_MEM_REG*/
	void *address;
	size_t len;
	unsigned long *mem_desc;  /*TODO: make this say mem_desc_t get rid of unsigned long business, and an opaque index rather than a real kernel pointer */
};

struct user_mem_dereg {
	uint32_t cmd; /* IWARP_MEM_DEREG */
	unsigned long md;
};

struct user_stag_create {
	uint32_t cmd; /* IWARP_STAG_CREATE */
	unsigned long md;
	void *start;
	size_t len;
	int rw;
	int prot_domain;
	int32_t *stag; /* from kernel to user */
};

struct user_stag_destroy {
	uint32_t cmd; /* IWARP_STAG_DESTROY */
	int32_t stag;
};

struct user_send {
	uint32_t cmd; /* IWARP_SEND */
	uint32_t fd;
	uint64_t id;   /* opaque user identifier */
	void __user *buf;
	uint32_t len;
	int32_t local_stag;
};

struct user_post_recv {
	uint32_t cmd; /* IWARP_POST_RECV */
	uint32_t fd;
	uint64_t id;   /* opaque user identifier */
	void __user *buf;
	uint32_t len;
	int32_t local_stag;
};

struct user_rdma_write {
	uint32_t cmd; /* IWARP_RDMA_WRITE */
	uint32_t fd;
	uint64_t id;   /* opaque user identifier */
	void __user *buf;
	uint32_t len;
	int32_t local_stag;
	int32_t sink_stag;
	uint64_t sink_to;
};

struct user_rdma_read {
	uint32_t cmd; /* IWARP_RDMA_READ */
	uint32_t fd;
	uint64_t id;   /* opaque user identifier */
	int32_t sink_stag;
	uint32_t sink_to;
	uint32_t len;
	int32_t src_stag;
	int32_t src_to;
};

struct user_encourage {
	uint32_t cmd; /* IWARP_ENCOURAGE */
};

#endif  /* __USER_H */

