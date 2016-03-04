/*
 * Memory handling header.
 *
 * $Id: test_stub.c 666 2007-08-03 15:12:59Z dennis $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include "ddp.h"
#include "mpa.h"
#include "common.h"
#include "util.h"
#include "rdmap.h"
#include "iwsk.h"
#include "test_stub.h"

static bool_t is_server = FALSE;
static char *server = NULL;
static socket_t server_fd = -1;
static socket_t sk = -1;
static const uint16_t PORT = 5728;
static const uint16_t TIMEOUT = 5;
static const uint16_t BACKLOG = 100;
static char myhostname[1024];

static void ATTR_NORETURN usage(const char *funcname);
static void get_nodeaddress(const char *node, struct sockaddr_in *skin);
static void open_server_sock(struct sockaddr_in *skin);
static void accept_connection(struct sockaddr_in *skin);
static void connect_to_server(struct sockaddr_in *skin);

static void ATTR_NORETURN
usage(const char *funcname)
{
	fprintf(stderr, "%s: Usage: %s [-server 1] [test specific local options] "
			"<server>\n", funcname, progname);
	exit(1);
}

void
parse_options(int argc, char *argv[])
{
	set_progname(argc, argv);
	while(++argv, --argc > 0){
		const char *cp;
		int i = 0;
		if (**argv == '-'){
			switch((*argv)[1]){
				case 'l':
				case 'n':
					++argv, --argc;
					break;
				case 's':
					cp = &((*argv)[2]);
					for (i=1; *cp && *cp == "server"[i]; cp++, i++);
					if(*cp)
						usage(__func__);
					if(++argv, --argc <= 0) usage("server");
					is_server = TRUE;
					break;
				default:
					usage(__func__);
			}
		} else {
			if (server)
				usage(__func__);
			server = *argv;
		}
	}
	if (!server)
		usage(__func__);
}

inline bool_t
get_isserver(void)
{
	return is_server;
}

static void
get_nodeaddress(const char *node, struct sockaddr_in *skin)
{
	struct hostent *hp = NULL;
	debug(2, "%s: in %s for %s", myhostname, __func__, node);
	socklen_t sin_len = sizeof(*skin);

	hp = gethostbyname(node);
	if(!hp)
		error("host \"%s\" not resolvable",node);
	memset(skin, 0, sin_len);
	skin->sin_family = hp->h_addrtype;
	memcpy(&(skin->sin_addr), hp->h_addr_list[0], hp->h_length);
	skin->sin_port = htons(PORT);
}

static void
open_server_sock(struct sockaddr_in *skin)
{
	int flags;

	debug(2, "%s: in %s", myhostname, __func__);
	server_fd = socket(PF_INET, SOCK_STREAM, 0);
	if(server_fd < 0)
		error_errno("socket");

	flags = 1;
	if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flags,
				  sizeof(flags)) < 0)
		error_errno("setsockopt reuseaddr ");
	if(bind(server_fd, (struct sockaddr *)skin, sizeof(*skin)) < 0)
		error_errno("bind");
	if(listen(server_fd, BACKLOG) < 0)
		error_errno("listen");

	flags = fcntl(server_fd, F_GETFL); /* get socket flags */
	if(flags < 0)
		error_errno("%s: get listen socket flags", __func__);
	/*if(fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0)
		error_errno("%s: error during setting listen socket nonblocking",
					__func__);*/

	debug(2, "bound to addr");
}


static void
accept_connection(struct sockaddr_in *skin)
{
	struct timeval tv, now, delta;

	open_server_sock(skin);

	sk = -1;
	gettimeofday(&tv, 0);
	while (sk < 0) {
		sk = accept(server_fd, 0, 0);
		if(sk< 0){
			if(errno == EAGAIN || errno == EINTR){
				usleep(100);
				gettimeofday(&now, 0);
				timersub(&now, &tv, &delta);
				if(delta.tv_sec >= TIMEOUT)
				    error(
				    "%s: %s: nobody connected before timeout",
					      myhostname, __func__);
				continue;
			}
			else
				error_errno("accept");
		}
	}

	close(server_fd);
}

static void
connect_to_server(struct sockaddr_in *skin)
{
	struct timeval tv, now, delta;

	struct hostent *he = NULL;

	he = gethostbyaddr((char *)&(skin->sin_addr.s_addr),
					   sizeof(skin->sin_addr.s_addr), AF_INET);

	sk = socket(PF_INET, SOCK_STREAM, 0);
	gettimeofday(&tv, 0);
	while (TRUE) {
		if(connect(sk, (struct sockaddr *)skin, sizeof(*skin))== 0)
			break;
		if(errno == ECONNREFUSED || errno == EINTR){
			usleep(100);
			gettimeofday(&now, 0);
			timersub(&now, &tv, &delta);
			if(delta.tv_sec >= 3*TIMEOUT){
				error("%s: failed to connect to server %s\n",
					  myhostname, he->h_name);
			}
		}
		else
			error_errno("connect");
	}
}

socket_t
init_connection(bool_t is_server)
{
	struct sockaddr_in skin;

	if (gethostname(myhostname, sizeof(myhostname)) < 0)
		error_errno("gethostname");

	get_nodeaddress(server, &skin);

	if (is_server) {
		accept_connection(&skin);
	} else {
		connect_to_server(&skin);
	}
	return sk;
}


