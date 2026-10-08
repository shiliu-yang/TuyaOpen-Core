/**
 * @file tal_kv_ops.h
 * @brief tal_kv 的平台 ops 契约：区域语义（一块数据区 + 区域读写擦）
 *
 * port 只需提供一块可读写擦的线性区域（flash 分区 / 区域文件 / EEPROM），
 * K-V 布局与加解密由 TAL 统一实现。移植新平台 = 实现 4 个区域原语。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#ifndef __TAL_KV_OPS_H__
#define __TAL_KV_OPS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t size;               /* 区域总字节数 */
    uint32_t erase_granularity;  /* 擦除粒度（flash=4096；文件实现可同用 4096） */
} TAL_KV_REGION_T;

typedef struct {
    /* 准备区域并回填 region 信息（建区域文件 / 挂 flash 分区） */
    OPERATE_RET (*init)(TAL_KV_REGION_T *region);
    OPERATE_RET (*read)(uint32_t offset, uint8_t *buf, uint32_t len);
    OPERATE_RET (*write)(uint32_t offset, const uint8_t *buf, uint32_t len);
    /* 擦除 [offset, offset+len)，对齐到 erase_granularity */
    OPERATE_RET (*erase)(uint32_t offset, uint32_t len);
} TAL_KV_OPS_T;

TAL_KV_OPS_T *tal_kv_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
