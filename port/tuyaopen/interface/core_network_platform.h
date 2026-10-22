/**
 * @file core_network_platform.h
 * @brief Platform socket API for core_network.c.
 *
 * LINUX uses the POSIX socket API; ESP32 and lwIP-based MCU platforms use the
 * lwIP socket API. This header is the only platform seam of the network layer.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#ifndef __CORE_NETWORK_PLATFORM_H__
#define __CORE_NETWORK_PLATFORM_H__

#include "tuya_cloud_types.h"

#if defined(__linux__) && !defined(ESP_PLATFORM)

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

#else /* ESP32 / lwIP-based platforms */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#endif

#define CORE_NET_NATIVE_FD_SET(fds) ((fd_set *)(fds))
typedef char core_net_fd_set_size_check_t[(sizeof(TUYA_FD_SET_T) >= sizeof(fd_set)) ? 1 : -1];

#endif /* __CORE_NETWORK_PLATFORM_H__ */
