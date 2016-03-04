/*
* Header for functions to stub between the kernel and software versions
*
*$Id: stubs.h 666 2007-08-03 15:12:59Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*/

#ifndef stub_h
#define stub_h

iwarp_status_t v_RNIC_open(int index, iwarp_rnic_t *rnic);

iwarp_status_t v_rnic_close(iwarp_rnic_t *rnic_ptr);

iwarp_status_t v_mem_init(void);

iwarp_status_t v_rdmap_init(void);

iwarp_status_t v_mem_fini(void);

iwarp_status_t v_rdmap_fin(void);

iwarp_status_t v_mem_register(iwarp_rnic_t *rnic_ptr, void *buffer, uint32_t length, iwarp_mem_desc_t *mem_region);

iwarp_status_t v_mem_stag_create(iwarp_rnic_t *rnic_ptr, iwarp_mem_desc_t *mem_region,
    void *start, uint32_t length, iwarp_access_control_t access_flags, iwarp_prot_id pd, iwarp_stag_index_t *stag_index);

iwarp_status_t v_mem_stag_destroy(iwarp_rnic_t *rnic_ptr, iwarp_stag_index_t stag_index);

iwarp_status_t v_mem_deregister(iwarp_rnic_t *rnic_ptr, iwarp_mem_desc_t mem_region);

iwarp_status_t v_rnic_advance(iwarp_rnic_t *rnic_ptr);

iwarp_status_t v_create_cq(iwarp_rnic_t *rnic_ptr, int *num_evts, iwarp_cq_handle_t *cq_hndl);

iwarp_status_t v_destroy_cq(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl);

iwarp_status_t v_poll_cq(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl, int retrys, int time_out,  iwarp_work_completion_t *wc);
iwarp_status_t v_poll_block_qp(iwarp_rnic_t *rnic_ptr,
                               iwarp_cq_handle_t cq_hndl,
			       iwarp_qp_handle_t qp_id,
			       iwarp_work_completion_t *wc);

iwarp_status_t v_rdmap_register_connection(iwarp_rnic_t *rnic_ptr, iwarp_qp_handle_t qp_id, const char private_data[],
				           char *remote_private_data, int rpd, iwarp_host_t type);

iwarp_status_t v_throw_away_cqe(iwarp_rnic_t *rnic_ptr, iwarp_cq_handle_t cq_hndl);

iwarp_status_t v_rdmap_post_recv(iwarp_rnic_t *rnic_ptr, int socket_fd, void *buffer, uint32_t length, iwarp_wr_id_t wr_id, iwarp_stag_index_t local_stag);

iwarp_status_t v_rdmap_post_send(iwarp_rnic_t *rnic_ptr, int socket_fd, void *buffer, uint32_t length, iwarp_wr_id_t wr_id, iwarp_stag_index_t local_stag);

iwarp_status_t v_rdmap_rdma_write(iwarp_rnic_t *rnic_ptr, int socket_fd, iwarp_stag_index_t remote_stag, uint64_t to,  void *buffer, uint32_t len, iwarp_wr_id_t wr_id, iwarp_stag_index_t local_stag);

iwarp_status_t v_rdmap_rdma_read(iwarp_rnic_t *rnic_ptr, int socket_fd, iwarp_stag_index_t local_stag, uint64_t to, uint32_t len, iwarp_stag_index_t remote_stag, uint64_t remote_to, iwarp_wr_id_t wr_id);

iwarp_status_t v_rdmap_deregister_sock(iwarp_rnic_t *rnic_ptr, int socket_fd);

#endif
