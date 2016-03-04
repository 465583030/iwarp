/*
 * test stub that establishes connections
 *
 * $Id: test_stub.h 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See LICENSE)
 */

#include "iwsk.h"

void parse_options(int argc, char *argv[]);
inline bool_t get_isserver(void);
socket_t init_connection(bool_t is_server);

