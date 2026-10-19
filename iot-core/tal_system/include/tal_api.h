/**
 * @file tal_api.h
 * @brief TAL dispatch for semaphores, mutexes, queues and OTA, plus optional
 * external RAM allocation.
 *
 * Every tal_semaphore_*, tal_mutex_* and tal_queue_* function resolves the
 * platform ops card through tal_api_get_ops(), which returns
 * tal_api_ops_platform (defined by the port). TAL_EXEC_OP checks that the card
 * and the requested member are non-NULL, logs the missing operation and
 * returns a caller-supplied default on failure; TAL_EXEC_OP_VOID is the
 * variant for operations without a return value. The tal_* signatures match
 * the ones declared in the tal_semaphore.h, tal_mutex.h and tal_queue.h
 * headers.
 *
 * OTA is an optional capability delivered by runtime callback injection:
 * tal_ota_register_cbs() stores a TAL_OTA_CBS_T pointer, and the three
 * tal_ota_* functions dispatch through it with OPRT_NOT_SUPPORTED as the
 * default when no callback set is registered. The registrant keeps the
 * structure alive for the whole OTA flow.
 *
 * When ENABLE_EXT_RAM is enabled, tal_ex_ram_malloc() and tal_ex_ram_free()
 * dispatch to the ex_ram_* members of the same ops card and return NULL or do
 * nothing when those members are absent.
 *
 * @note This file is part of the Tuya IoT Development Platform and is intended
 * for use in Tuya-based applications. It is subject to the platform's license
 * and copyright terms.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_API_H__
#define __TAL_API_H__

#include "tuya_cloud_types.h"

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_ota.h"
#include "tal_queue.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tal_time_service.h"
#include "tal_network.h"
#include "tal_workqueue.h"
#include "tal_kv.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "tal_security.h"
#include "tal_sw_timer.h"
#include "tal_event.h"

#include "tal_api_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform ops of tal_api
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_API_OPS_T *tal_api_get_ops(void);

#ifdef __cplusplus
}
#endif /* __TAL_LOG_H__ */

#endif
