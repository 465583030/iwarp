/*
 * util header
 *
 * $Id: util.h 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#ifndef __UTIL_H
#define __UTIL_H

#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/uio.h>
#include <stdarg.h>

#ifdef __KERNEL__
#	ifdef IWARP_DEBUG
#		define iwarp_debug(fmt, args...) \
		do {\
			printk(KERN_DEBUG "kiwarp: " fmt "\n", ##args);\
		} while (0)
#	else
#		define iwarp_debug(fmt, args...)
#	endif

#	define iwarp_info(fmt, args...) \
		do {\
			printk(KERN_INFO "kiwarp: " fmt "\n", ##args);\
		} while (0)
#endif

#ifdef __GNUC__
#  define ATTR_PRINTF   __attribute__ ((format(printf, 1, 2)))
#  define ATTR_PRINTF2  __attribute__ ((format(printf, 2, 3)))
#  define ATTR_NORETURN __attribute__ ((noreturn))
#  define ATTR_UNUSED   __attribute__ ((unused))
#  if __GNUC__ > 2
#  	 define ATTR_MALLOC   __attribute__ ((malloc))
#  else
#    define ATTR_MALLOC
#  endif
#else
#  define ATTR_PRINTF
#  define ATTR_PRINTF2
#  define ATTR_NORETURN
#  define ATTR_UNUSED
#  define ATTR_MALLOC
#endif


int kernel_recvmsg_full(struct socket *sock, struct msghdr *msg,
			struct kvec *vec, int vec_sz, ssize_t len, int flags);

int kernel_sendmsg_full(struct socket *sock, struct msghdr *msg,
			struct kvec *vec, int vec_sz, size_t len);

#endif /* __UTIL_H */
