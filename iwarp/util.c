/*
 * util.c - general utilities
 *
 * $Id: util.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "util.h"

const char *progname = "(pre-main)";


int rdma_write_count = 0;  /*This is the RDMA Write count that will be exported - user apps can use this DIRECTLY*/

/*
 * Set the program name, first statement of code usually.
 */
void
set_progname(int argc ATTR_UNUSED, char *argv[])
{
    const char *cp;

    for (cp=progname=argv[0]; *cp; cp++)
	if (*cp == '/')
	    progname = cp+1;
}

/*
 * Debug message.
 */
void
info(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ".\n");
}

/*
 * Warning, non-fatal.
 */
void
warning(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Warning: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ".\n");
}

/*
 * printerr, non-fatal
 */
void
printerr(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: Error:", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ".\n");
}

/*
 * Error, fatal.
 */
void
error(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ".\n");
    exit(1);
}

/*
 * Error, fatal, with the errno message.
 */
void
error_errno(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", strerror(errno));
    exit(1);
}

/*
 * Error, fatal, with the negative errno value given as first argument.
 */
void
error_ret(int ret, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: Error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", strerror(-ret));
    exit(1);
}

/*
 * Put a string in permanent memory.  strdup with error-checking
 * malloc.
 */
char *
strsave(const char *s)
{
    char *t;

    t = Malloc((strlen(s)+1)*sizeof(char));
    strcpy(t, s);
    return t;
}

/*
 * Error-checking malloc.
 */
void *
Malloc(unsigned int n)
{
    void *x;

    if (n == 0)
	error("Malloc called on zero bytes");
    x = malloc((unsigned int) n);
    if (!x)
	error("%s: couldn't get %d bytes", __func__, n);
    return x;
}

/*
 * Gcc >= "2.96" seem to have this nice addition.  2.95.2 does not.
 */
#ifdef __GNUC__
#  if __GNUC__ >= 3
#    define USE_FORMAT_SIZE_T 1
#  endif
#  if __GNUC__ == 2 && __GNUC_MINOR__ >= 96
#    define USE_FORMAT_SIZE_T 1
#  endif
#endif

#ifndef USE_FORMAT_SIZE_T
#  define USE_FORMAT_SIZE_T 0
#endif

/*
 * Loop over reading until everything arrives.  Error if not.
 */
void
read_full(int fd, void *buf, size_t num)
{
    int cc, offset = 0;
    int total = num;

    while (num > 0) {
	cc = read(fd, (char *)buf + offset, num);
	if (cc < 0) {
	    if (USE_FORMAT_SIZE_T)
		error_errno("%s: read %zu bytes", __func__, num);
	    else
		error_errno("%s: read %lu bytes", __func__,
		  (long unsigned int) num);
	}
	if (cc == 0 && total != 0)
	    error("%s: EOF, only %d of %d bytes", __func__, offset, total);
	num -= cc;
	offset += cc;
    }
}

/*
 * Keep looping until all bytes have been accepted by the kernel.  Return
 * count if all okay, else -1 with errno set.
 */
int
write_full(int fd, const void *buf, size_t count)
{
    int cc, ptr = 0;
    int total = count;

    while (count > 0) {
	cc = write(fd, (const char *)buf + ptr, count);
	if (cc < 0)
	    return cc;
	count -= cc;
	ptr += cc;
    }
    return total;
}

/* Loop over read vector till everything arrives */
void
readv_full(int fd, struct iovec *vec, int vec_sz, ssize_t len)
{
	int cc, vi = 0;

	while (len > 0) {
		cc = readv(fd, &vec[vi], vec_sz - vi);
		if (cc < 0)
			error_errno("%s:%d cc = (%d)", __FILE__, __LINE__, cc);
		len -= cc;
		if (len > 0) {
			while (vi < vec_sz && vec[vi].iov_len < (size_t)cc) {
				cc -= vec[vi].iov_len;
				vi++;
			}
			iw_assert(vi < vec_sz, "vi (%d) >= vec_sz (%d)", vi, vec_sz);
			read_full(fd, (uint8_t *)vec[vi].iov_base + cc,
					  vec[vi].iov_len - cc);
			len -= (vec[vi].iov_len - cc);
			vi++;
		}
	}
	iw_assert(len == 0, "%s:%d len(%d) != 0", __FILE__, __LINE__, len);
}

/*
 * Return 0 for successful, or -1 if error; only writes entire array.
 */
int
writev_full(int fd, struct iovec *vec, int vec_sz, size_t len)
{
	int cc, vi = 0, ret = 0;

	while (len > 0) {
		cc = writev(fd, &vec[vi], vec_sz - vi);
		if (cc < 0)
			return cc;
		len -= cc;
		if (len > 0) {
			while (vi < vec_sz && (int)vec[vi].iov_len < cc) {
				cc -= vec[vi].iov_len;
				vi++;
			}
			iw_assert(vi < vec_sz, "vi (%d) >= vec_sz (%d)", vi, vec_sz);
			ret = write_full(fd, (const uint8_t *)vec[vi].iov_base + cc,
							 vec[vi].iov_len - cc);
			if (ret < 0) {
				/* TODO: if non-blocking check EAGAIN & loop */
				break;
			}
			len -= vec[vi].iov_len - cc;
			vi++;
		}
	}

	return ret;
}
