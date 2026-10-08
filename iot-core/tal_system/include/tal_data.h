/**
 * @file tal_data.h
 * @brief K-V 数据存储：init / read / write / erase（上层只关心数据）
 *
 * 对上 4 个接口，不暴露存储细节；布局与加解密由 TAL 实现，port 只提供一块
 * 可读写擦的数据区（见 tal_data_ops.h）。name 限 [A-Za-z0-9._-]、长度 ≤63。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#ifndef __TAL_DATA_H__
#define __TAL_DATA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单条明文上限（宏可配） */
#define TAL_DATA_PLAIN_MAX (16 * 1024)

/* seed/key 各 16 字节（沿用现有 tal_kv 的密钥形态） */
OPERATE_RET tal_data_init(const char *seed, const char *key);

/* buf 由调用方提供；buf==NULL 时仅回填 *out_len（探测尺寸） */
OPERATE_RET tal_data_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len);

OPERATE_RET tal_data_write(const char *name, const uint8_t *buf, size_t len);

OPERATE_RET tal_data_erase(const char *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
