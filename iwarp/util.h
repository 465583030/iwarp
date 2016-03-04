/*
 * util.h - declarations of general utilities
 *
 * $Id: util.h 658 2006-10-19 19:58:05Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <stdlib.h>
#include <sys/uio.h>
#ifndef __util_h
#define __util_h

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
#  ifndef likely
#  define likely(x)   __builtin_expect((int)(x),1)
#  endif
#  ifndef unlikely
#  define unlikely(x) __builtin_expect((int)(x),0)
#  endif
#else
#  define ATTR_PRINTF
#  define ATTR_PRINTF2
#  define ATTR_NORETURN
#  define ATTR_UNUSED
#  define ATTR_MALLOC
#  ifndef likely
#  define likely(x)   (x)
#  endif
#  ifndef unlikely
#  define unlikely(x) (x)
#  endif
#endif

#if 0
#define iw_assert(cond, fmt, args...) \
	do { \
		if (unlikely(!(cond))) \
			error(fmt, ##args); \
	} while (0)
#else
#   define iw_assert(cond, fmt, ...) do { } while (0)
#endif

#if 0
#define DEBUG_LEVEL 10
#define debug(lvl,fmt,args...) \
    do { \
		if (lvl <= DEBUG_LEVEL) \
	    	info(fmt,##args); \
    } while (0)
#else
#  define debug(lvl,fmt,...) do { } while (0)
#endif

/*
 * printf converters to avoid warnings.  Use Ld or Lx/Lu in format.
 */
#if defined(__x86_64__)
/* in general, 64-bit architectures */
#  define Ld(x) ((long long int)(x))
#  define Lu(x) ((long long unsigned int)(x))
#else
#  define Ld(x) (x)
#  define Lu(x) (x)
#endif

/* set by set_progname */
extern const char *progname;

extern void set_progname(int argc, char *argv[]);
extern void info(const char *fmt, ...);
extern void warning(const char *fmt, ...) ATTR_PRINTF;
extern void printerr(const char *fmt, ...) ATTR_PRINTF;
extern void error(const char *fmt, ...) ATTR_PRINTF ATTR_NORETURN;
extern void error_errno(const char *fmt, ...) ATTR_PRINTF ATTR_NORETURN;
extern void error_ret(int ret, const char *fmt, ...) ATTR_PRINTF2 ATTR_NORETURN;
extern char *strsave(const char *s);
extern void *Malloc(unsigned int n) ATTR_MALLOC;
extern void read_full(int fd, void *buf, size_t count);
extern int write_full(int fd, const void *buf, size_t count);
extern void readv_full(int fd, struct iovec *vec, int vec_sz, ssize_t len);
extern int writev_full(int fd, struct iovec *vec, int vec_sz, size_t len);

#endif  /* __util_h */
