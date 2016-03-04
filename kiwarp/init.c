/*
 * kiwarp main file and user interface handling
 *
 * $Id: init.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "util.h"
#include "user.h"
#include "rdmap.h"
#include "priv.h"
#include "iwsk.h"

static LIST_HEAD(user_context_list);

/*
 * Always succeed.  One connection per thread group max.
 */
static int iwarp_open(struct inode *inode, struct file *file)
{
	struct user_context *uc;
	int ret = 0;

	iwarp_debug("%s: --------------------------", __func__);
	iwarp_debug("%s: add user %d", __func__, current->tgid);
	list_for_each_entry(uc, &user_context_list, list) {
		if (current->tgid == uc->tgid) {
		    ret = -EBUSY;
		    goto out;
		}
	}

	uc = kmalloc(sizeof(*uc), GFP_KERNEL);
	if (!uc) {
		ret = -ENOMEM;
		goto out;
	}
	uc->tgid = current->tgid;

	INIT_LIST_HEAD(&uc->cq_list);
	uc->cq_list_next_handle = 1;

	/* open iwarp stack */
	ret = rdmap_open();
	if (ret < 0)
		goto out_free_uc;

	/* initialize stag related structs */
	uc->mm = kmalloc(sizeof(*(uc->mm)), GFP_KERNEL);
	if (!uc->mm) {
		ret = -ENOMEM;
		goto out_rdmap_close;
	}
	ret = mem_initialize(uc->mm);
	if (ret < 0)
		goto out_free_mm;

	/* hash of fds for this user */
	uc->fdhash = kmalloc(sizeof(*uc->fdhash), GFP_KERNEL);
	if (!uc->fdhash) {
		ret = -ENOMEM;
		goto out_mem_release;
	}
	ret = ht_create(4, uc->fdhash);
	if (ret < 0)
		goto out_free_fdhash;

	/* success */
	list_add(&uc->list, &user_context_list);
	file->private_data = uc;
	goto out;

out_free_fdhash:
	kfree(uc->fdhash);
out_mem_release:
	mem_release(uc->mm);
out_free_mm:
	kfree(uc->mm);
out_rdmap_close:
	rdmap_close();
out_free_uc:
	kfree(uc);

out:
	return ret;
}

/*
 * Close the connection, deleting all data structures first.
 */
static int iwarp_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	cq_t *cq, *cqnext;
	struct user_context *uc = file->private_data;

	iwarp_debug("%s: remove user %d", __func__, current->tgid);
	list_del(&uc->list);

	/* destroy hash table, calling this function for each one to
	 * delete the iwsk structure too */
	ret = ht_destroy_callback(uc->fdhash, rdmap_release_sock_res);
	kfree(uc->fdhash);

	/* release stag DS */
	ret = mem_release(uc->mm);
	kfree(uc->mm);

	/* destroy all CQs */
	list_for_each_entry_safe(cq, cqnext, &uc->cq_list, list)
		if (cq_destroy(uc, cq) < 0)
			iwarp_info("%s: destroy cq handle %d failed",
				   __func__, (int) cq->handle);

	rdmap_close(); /* close iwarp stack */
	kfree(uc);
	return ret;
}

/*
 * No read commands yet
 */
ssize_t iwarp_read(struct file *file, char __user *buf, size_t len,
		   loff_t *ppos)
{
	if (ppos != &file->f_pos)
	       return -ESPIPE;  /* pread not supported */

	if (len == 0)
	       return 0;

	return -EINVAL;
}

/*
 * Read the command type, then read the contents and call the appropriate
 * iwarp function with its "natural" arguments.  This is basically a
 * demultiplexor using a write interface from userspace. The kernel
 * translates from fd -> struct file *.
 */
static ssize_t iwarp_write(struct file *file, const char __user *ubuf,
		           size_t count, loff_t *ppos)
{
	struct user_context *uc = file->private_data;
	uint32_t cmd;
	int ret = 0;

	if (count < sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, ubuf, sizeof(cmd)))
		return -EFAULT;

	iwarp_debug("%s: cmd %d", __func__, cmd);
	switch (cmd) {
	    case IWARP_REGISTER_SOCK: {
		struct user_register_sock ureg;
		cq_t *scq, *rcq;
		if (count != sizeof(ureg))
			return -EINVAL;
		if (copy_from_user(&ureg, ubuf, sizeof(ureg)))
			return -EFAULT;
		scq = cq_lookup(uc, ureg.scq_handle);
		if (!scq)
			return -EINVAL;
		rcq = cq_lookup(uc, ureg.rcq_handle);
		if (!rcq)
			return -EINVAL;
		ret = rdmap_register_sock(uc, ureg.fd, scq, rcq);
		break;
	    }
	    case IWARP_SET_SOCK_ATTRS: {
		struct user_sock_attrs attrs;
		if (count != sizeof(attrs))
			return -EINVAL;
		if (copy_from_user(&attrs, ubuf, count))
			return -EFAULT;
		ret = rdmap_set_sock_attrs(uc, attrs.fd, attrs.use_crc,
					   attrs.use_mrkr);
		break;
	    }
	    case IWARP_INIT_STARTUP: {
		struct user_init_startup uis;
		if (count != sizeof(uis))
			return -EINVAL;
		if (copy_from_user(&uis, ubuf, count))
			return -EFAULT;
		ret = rdmap_init_startup(uc, uis.fd, uis.is_initiator,
					 uis.pd_in, uis.len_in, uis.pd_out,
					 uis.len_out);
		break;
	    }
	    case IWARP_DEREGISTER_SOCK: {
		struct user_deregister_sock uds;
		if (count != sizeof(uds))
			return -EINVAL;
		if (copy_from_user(&uds, ubuf, sizeof(uds)))
			return -EFAULT;
		ret = rdmap_deregister_sock(uc, uds.fd);
		break;
	    }
	    case IWARP_POLL: {
		struct user_poll upoll;
		cq_t *cq;
		if (count != sizeof(upoll))
			return -EINVAL;
		if (copy_from_user(&upoll, ubuf, sizeof(upoll)))
			return -EFAULT;
		cq = cq_lookup(uc, upoll.cq_handle);
		if (!cq)
			return -EINVAL;
		ret = rdmap_poll(uc, cq, upoll.wc);
		break;
	    }
	    case IWARP_POLL_BLOCK: {
		struct user_poll_block upoll;
		cq_t *cq;
		if (count != sizeof(upoll))
			return -EINVAL;
		if (copy_from_user(&upoll, ubuf, sizeof(upoll)))
			return -EFAULT;
		cq = cq_lookup(uc, upoll.cq_handle);
		if (!cq)
			return -EINVAL;
		ret = rdmap_poll_block(uc, cq, upoll.fd, upoll.wc);
		break;
	    }
	    case IWARP_CQ_CREATE: {
	    	struct user_cq_create ucc;
		cq_t *cq;
		if (count != sizeof(ucc))
			return -EINVAL;
		if (copy_from_user(&ucc, ubuf, sizeof(ucc)))
			return -EFAULT;
		cq = cq_create(uc, ucc.depth);
		if (!cq)
			return -ENOMEM;
		//~ iwarp_info("%s: cq handle is %llu", __func__, cq->handle);
		if (copy_to_user(ucc.cq_handle, &cq->handle,
				 sizeof(cq->handle)))
			return -EFAULT;
		ret = 0;
		break;
	    }
	    case IWARP_CQ_DESTROY: {
		struct user_cq_destroy ucd;
		cq_t *cq;
		if (count != sizeof(ucd))
			return -EINVAL;
		if (copy_from_user(&ucd, ubuf, sizeof(ucd)))
			return -EFAULT;
		cq = cq_lookup(uc, ucd.cq_handle);
		if (!cq)
			return -EINVAL;
		/*iwarp_info("%s: destroying cq with handle= %llu", __func__,
		cq->handle);*/
		ret = cq_destroy(uc, cq);
		break;
	    }
	    case IWARP_MEM_REG: {
		struct user_mem_reg ureg;
		mem_desc_t mem_desc;
		if (count != sizeof(ureg))
			return -EINVAL;
		if (copy_from_user(&ureg, ubuf, sizeof(ureg)))
			return -EFAULT;
		mem_desc = mem_register(ureg.address, ureg.len, uc->mm);
		if (!mem_desc)
			return -EINVAL;
		//~ iwarp_info("mem_desc is %lx\n", mem_desc);
		if (copy_to_user(ureg.mem_desc, &mem_desc, sizeof(mem_desc)))
			return -EFAULT;
		ret = 0;
		break;
	    }
	    case IWARP_MEM_DEREG: {
		struct user_mem_dereg umd;
		if (count != sizeof(umd))
			return -EINVAL;
		if (copy_from_user(&umd, ubuf, sizeof(umd)))
			return -EFAULT;
		ret = mem_deregister(umd.md, uc->mm);
		break;
	    }
	    case IWARP_STAG_CREATE: {
		struct user_stag_create usc;
		stag_t stag;
		if (count != sizeof(usc))
			return -EINVAL;
		if (copy_from_user(&usc, ubuf, sizeof(usc)))
			return -EFAULT;
		//~ iwarp_info("Got mem desc %lx from user\n", usc.md);
		stag = mem_stag_create(usc.md, usc.start, usc.len, usc.rw,
				       usc.prot_domain, uc->mm);
		//~ iwarp_info("STag passing back to user is %d\n", stag);
		if (stag < 0)
			return -EINVAL;
		if (copy_to_user(usc.stag, &stag, sizeof(stag)))
			return -EFAULT;
		ret = 0;
		break;
	    }
	    case IWARP_STAG_DESTROY: {
		struct user_stag_destroy usd;
		if (count != sizeof(usd))
			return -EINVAL;
		if (copy_from_user(&usd, ubuf, sizeof(usd)))
			return -EFAULT;
		ret = mem_stag_destroy(usd.stag, uc->mm);
	    	break;
	    }
	    case IWARP_SEND: {
		struct user_send us;
		if (count != sizeof(us))
			return -EINVAL;
		if (copy_from_user(&us, ubuf, sizeof(us)))
			return -EFAULT;
		ret = rdmap_send(uc, us.fd, us.id, us.buf, us.len,
		                 us.local_stag);
		break;
	    }
	    case IWARP_POST_RECV: {
		struct user_post_recv upr;

		if (count != sizeof(upr))
			return -EINVAL;
		if (copy_from_user(&upr, ubuf, sizeof(upr)))
			return -EFAULT;
		ret = rdmap_post_recv(uc, upr.fd, upr.id, upr.buf, upr.len,
		                      upr.local_stag);
		break;
	    }
	    case IWARP_RDMA_WRITE: {
		struct user_rdma_write urw;
		if (count != sizeof(urw))
			return -EINVAL;
		if (copy_from_user(&urw, ubuf, sizeof(urw)))
			return -EFAULT;
		ret = rdmap_rdma_write(uc, urw.fd, urw.id, urw.buf, urw.len,
				       urw.local_stag, urw.sink_stag,
				       urw.sink_to);
		break;
	    }
	    case IWARP_RDMA_READ: {
		struct user_rdma_read urr;
		if (count != sizeof(urr))
			return -EINVAL;
		if (copy_from_user(&urr, ubuf, sizeof(urr)))
			return -EFAULT;
		ret = rdmap_rdma_read(uc, urr.fd, urr.id, urr.sink_stag,
				      urr.sink_to, urr.len, urr.src_stag,
				      urr.src_to);
		break;
	    }
	    case IWARP_ENCOURAGE: {
		ret = rdmap_encourage(uc, NULL);
		break;
	    }
	    default:
		ret = -EINVAL;
	}

	/* write worked okay */
	if (ret == 0)
		ret = count;
	iwarp_debug("write ret %d", ret);
	return ret;
}

static struct file_operations iwarp_fops = {
	.owner = THIS_MODULE,
	.open    = iwarp_open,
	.release = iwarp_release,
	.read    = iwarp_read,
	.write   = iwarp_write,
};

static const char *modname = "kiwarp";
static int major;

static int iwarp_init(void)
{
	int ret;

	ret = register_chrdev(0, modname, &iwarp_fops);
	if (ret < 0) {
	        printk(KERN_ERR "%s: register_chrdev\n", __func__);
		return ret;
	}
	major = ret;

	iwarp_info("module loaded");
	return 0;
}

static void iwarp_exit(void)
{
	int ret;

	ret = unregister_chrdev(major, modname);
	if (ret < 0)
		printk(KERN_ERR "%s: unregister_chrdev %d failed: %d\n",
		    __func__, major, ret);

	iwarp_info("module unloaded");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OSC iwarp team");
MODULE_DESCRIPTION("Kernel-resident iwarp software stack");

module_init(iwarp_init);
module_exit(iwarp_exit);

