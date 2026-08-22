/**
 *  Copyright (C) 2011-2012  Juho Vähä-Herttua
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#ifndef COMPAT_H
#define COMPAT_H

#if defined(WIN32) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <time.h>
#include <BaseTsd.h>

#if defined(_MSC_VER)
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif

#if _MSC_VER < 1900
#ifndef snprintf
#define snprintf _snprintf
#endif
#endif

#ifndef strtok_r
#define strtok_r strtok_s
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

static inline int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    timespec_get(tp, TIME_UTC);
    return 0;
}
#endif

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#endif

#include "sockets.h"
#include "threads.h"

#endif
