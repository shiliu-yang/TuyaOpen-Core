/**
 * @file tal_kv.c
 * @brief tal_kv platform dispatch: forwards the four interfaces to the port's
 *        KV implementation (tal_kv_ops_platform).
 *
 * iot-core holds no KV logic -- storage medium, record format, encryption and
 * serialization all live in the port (see the tal_kv_ops.h contract).
 * Dispatch goes through TAL_KV_EXEC_OP, which checks the card and member
 * before calling them and returns the given default otherwise.
 *
 * tal_kv_get() clears *value to NULL and *length to 0 before dispatching, so
 * callers observe that contract even when the operation is unavailable.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv.h"
#include "tal_kv_ops.h"
#include "tal_log.h"

/* Platform ops card (defined in the port's port_kv.c), dispatched by core
 * through the getter */
extern TAL_KV_OPS_T tal_kv_ops_platform;

TAL_KV_OPS_T *tal_kv_get_ops(void)
{
    return &tal_kv_ops_platform;
}

#define TAL_KV_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        TAL_KV_OPS_T *ops = tal_kv_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL KV operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

int tal_kv_init(tal_kv_cfg_t *kv_cfg)
{
    TAL_KV_EXEC_OP(init, OPRT_COM_ERROR, kv_cfg);
}

int tal_kv_set(const char *key, const uint8_t *value, size_t length)
{
    TAL_KV_EXEC_OP(set, OPRT_COM_ERROR, key, value, length);
}

int tal_kv_get(const char *key, uint8_t **value, size_t *length)
{
    /* Contract: on failure *value is NULL and *length is 0 (also when the ops
     * card is unavailable) */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }
    TAL_KV_EXEC_OP(get, OPRT_COM_ERROR, key, value, length);
}

int tal_kv_del(const char *key)
{
    TAL_KV_EXEC_OP(del, OPRT_COM_ERROR, key);
}
