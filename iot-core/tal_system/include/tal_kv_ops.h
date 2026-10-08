/**
 * @file tal_kv_ops.h
 * @brief tal_kv 的平台 ops 契约：区域语义（一块数据区 + 区域读写擦）
 *
 * port 只需提供一块可读写擦的线性区域（flash 分区 / 区域文件 / EEPROM），
 * K-V 布局与加解密由 TAL 统一实现。移植新平台 = 实现 4 个区域原语。
 *
 * 区域状态契约：**port 必须保证首次使用前区域为擦除态（全 0xFF）**；引擎在
 * 扫描为空（无任何可解析记录）时会主动 erase(0, region_size) 一次作为兜底——
 * 防的是把引擎指向未擦除/曾被占用的区域（如老 littlefs 分区原地升级）：真 NOR
 * flash 只能 1→0，往非 0xFF 区域追加会静默写失败而内存索引无感，表现为每次
 * 重启都丢数据；此时无数据可失，归零是安全的。
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
    /* 擦除 [offset, offset+len)，对齐到 erase_granularity。
     * 契约（强制）：①region.size 必须是 erase_granularity 的整数倍（否则引擎的整区擦除
     * 会越界到相邻分区）②flash 实现必须调块擦除命令——**写 0xFF 对真 NOR flash 是 no-op**，
     * 照抄文件实现会得到"擦除成功但区域纹丝不动" */
    OPERATE_RET (*erase)(uint32_t offset, uint32_t len);
} TAL_KV_OPS_T;

TAL_KV_OPS_T *tal_kv_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
