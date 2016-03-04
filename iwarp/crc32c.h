/*
 * Declare crc32c.c function.
 *
 * $Id: crc32c.h 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later.  (See LICENSE.)
 */
#ifndef __crc32c_h
#define __crc32c_h

#include <sys/uio.h>

uint32_t crc32c(const void *data, size_t length);
uint32_t crc32c_vec(const struct iovec *vec, int count);

#endif  /* __crc32c_h */
