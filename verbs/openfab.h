 /*
* OpenFabrics compativle API calls to our verbs API calls
*
*$Id: openfab.h 670 2007-08-23 18:51:00Z dennis $
*
*Copyright (C) 2005 OSC iWarp Team
*Distributed under the GNU Public License Version 2 or later (SEE LICENSE)
*
*
*
*/
#include "openfab/infiniband/verbs.h"
#include "openfab/rdma/rdma_cma.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include "util.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>

#define MAX_PRIV_DATA 255  /*(2^8 -1)*/


int check_open(struct ibv_context *context);

