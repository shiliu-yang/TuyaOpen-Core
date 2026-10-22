/**
 * @file core_network_platform.h
 * @brief Platform socket API for core_network.c (LINUX/POSIX).
 *
 * core_network.c includes this header and calls the socket API directly. This
 * header is the only platform seam for the network layer: it pulls in the
 * platform socket stack and provides the TUYA_FD_SET_T -> fd_set bridge.
 *
 * Layering: this header is part of the port's contract layer. It may include
 * tuya_cloud_types.h and platform headers only; it must not include any core
 * header.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETWORK_PLATFORM_H__
#define __CORE_NETWORK_PLATFORM_H__

#include "tuya_cloud_types.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

/* TUYA_FD_MAX_COUNT is 1024 on LINUX, so TUYA_FD_SET_T and glibc's fd_set are
 * both 128 bytes. Keep the cast below guarded by a compile-time size check. */
#define CORE_NET_NATIVE_FD_SET(fds) ((fd_set *)(fds))
typedef char core_net_fd_set_size_check_t[(sizeof(TUYA_FD_SET_T) >= sizeof(fd_set)) ? 1 : -1];

#endif /* __CORE_NETWORK_PLATFORM_H__ */
