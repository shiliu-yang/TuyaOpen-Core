/**
 * @file tal_netif.h
 * @brief tal_netif platform dispatch: forwards the two interfaces to the
 *        port's netif implementation (tal_netif_ops_platform).
 *
 * core holds no network interface implementation -- interface walking,
 * ioctl retrieval and the polling thread all live in the port (see the
 * tal_netif_ops.h contract). Dispatch goes through TAL_NETIF_EXEC_OP, which
 * checks the card and member before calling them and returns the given
 * default otherwise.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_NETIF_H__
#define __TAL_NETIF_H__

#include "tuya_cloud_types.h"

/* Contract layer (supplied by the selected platform's port):
 * TAL_NETIF_LINK_STAT_E / TAL_NETIF_LINK_CHANGE_CB / TAL_NETIF_OPS_T are
 * defined in port/interface/tal_netif_ops.h -- core consumes the contract,
 * never defines it (layering: port does not depend on core). */
#include "tal_netif_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform ops of tal_netif
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_NETIF_OPS_T *tal_netif_get_ops(void);

/**
 * @brief  Get the local network interface IP (first non-loopback interface
 *         that is up)
 *
 * @param[out]  ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_netif_get_ip(NW_IP_S *ip);

/**
 * @brief  Register a link state change callback (starts an internal 3s
 *         polling thread; repeated calls are idempotent)
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_netif_set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb);

#ifdef __cplusplus
}
#endif

#endif // __TAL_NETIF_H__
