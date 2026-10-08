/**
 * @file tal_data.c
 * @brief tal_data K-V 引擎：线性追加日志（追加 + 墓碑 + seq + CRC）+ AES
 *
 * 区域 = 纯线性日志：write/erase 只在尾部追加，从不回卷、不压缩、不擦除；
 * 区域满时返回 OPRT_COM_ERROR（绝不覆盖已有记录）——flash 不做回收，
 * 由平台开足够大的存储区域。掉电安全：重扫按 seq 取大，撕裂记录 CRC 失败被忽略。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_data.h"
#include "tal_data_ops.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_security.h"

#include <string.h>

/* 平台 ops 卡（port 的 tkl_data.c 定义），core 侧经 getter 分发 */
extern TAL_DATA_OPS_T tal_data_ops_platform;

TAL_DATA_OPS_T *tal_data_get_ops(void)
{
    return &tal_data_ops_platform;
}

#define TAL_DATA_MAGIC       0xDA7A
#define TAL_DATA_ST_VALID    0x01
#define TAL_DATA_ST_TOMB     0x00
#define TAL_DATA_ST_PAD      0x02
#define TAL_DATA_NAME_MAX    63
#define TAL_DATA_INDEX_MAX   64
/* 单条明文上限 TAL_DATA_PLAIN_MAX 由 tal_data.h 提供 */

typedef struct {
    char     name[TAL_DATA_NAME_MAX + 1];
    uint32_t rec_off;
    uint32_t rec_len;
    uint32_t data_off;
    uint32_t data_len;
    uint32_t seq;
} TAL_DATA_ITEM_T;

static TAL_DATA_ITEM_T s_index[TAL_DATA_INDEX_MAX];
static uint32_t s_index_cnt;
static uint32_t s_max_seq;
static uint32_t s_tail;
static uint32_t s_region_size;
static uint8_t  s_key[16];
static uint8_t  s_iv[16];
static int      s_inited;

/* ---- 小工具：小端读写 ---- */
static uint16_t __rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t __rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void __wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void __wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- CRC32（多项式 0xEDB88320，逐位实现，避免引入查表依赖） ---- */
static uint32_t __crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

/* ---- name 合法性 ---- */
static OPERATE_RET __name_check(const char *name)
{
    size_t len;
    if (NULL == name) {
        return OPRT_INVALID_PARM;
    }
    len = strlen(name);
    if (0 == len || len > TAL_DATA_NAME_MAX) {
        return OPRT_INVALID_PARM;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        int ok = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                  c == '_' || c == '-');
        if (!ok) {
            return OPRT_INVALID_PARM;
        }
    }
    return OPRT_OK;
}

/* ---- 记录编解码 ---- */
static uint32_t __rec_len(uint32_t namelen, uint32_t datalen)
{
    uint32_t raw = 16 + namelen + datalen;          /* magic2+state1+seq4+namelen1+datalen4+crc4 */
    return (raw + 3) & ~3u;                          /* 4 字节对齐 */
}

static void __rec_encode(uint8_t *rec, uint8_t state, uint32_t seq, const char *name, const uint8_t *data,
                         uint32_t datalen)
{
    uint32_t namelen = (uint32_t)strlen(name);
    uint32_t body = 12 + namelen + datalen;          /* 不含 crc 的部分 */
    uint32_t total = __rec_len(namelen, datalen);

    memset(rec, 0, total);
    __wr16(rec + 0, TAL_DATA_MAGIC);
    rec[2] = state;
    __wr32(rec + 3, seq);
    rec[7] = (uint8_t)namelen;
    memcpy(rec + 8, name, namelen);
    __wr32(rec + 8 + namelen, datalen);
    if (datalen > 0 && NULL != data) {
        memcpy(rec + 12 + namelen, data, datalen);
    }
    __wr32(rec + body, __crc32(rec, body));
}

/* 解析一条记录；成功返回 OPRT_OK 并回填字段（data 不拷贝，只回填偏移） */
static OPERATE_RET __rec_decode(const uint8_t *rec, uint32_t buf_len, uint32_t rec_off, uint8_t *state,
                                uint32_t *seq, char name[TAL_DATA_NAME_MAX + 1], uint32_t *data_off,
                                uint32_t *data_len, uint32_t *rec_len)
{
    uint32_t namelen, dlen, body, total;

    if (buf_len < 16 || TAL_DATA_MAGIC != __rd16(rec)) {
        return OPRT_NOT_FOUND;
    }
    namelen = rec[7];
    if (namelen < 1 || namelen > TAL_DATA_NAME_MAX) {
        return OPRT_NOT_FOUND;
    }
    if (buf_len < 12 + namelen + 4) {
        return OPRT_NOT_FOUND;
    }
    dlen = __rd32(rec + 8 + namelen);
    if (dlen > TAL_DATA_PLAIN_MAX + 16) {
        return OPRT_NOT_FOUND;
    }
    body = 12 + namelen + dlen;
    total = __rec_len(namelen, dlen);
    if (buf_len < total) {
        return OPRT_NOT_FOUND;
    }
    if (__crc32(rec, body) != __rd32(rec + body)) {
        return OPRT_NOT_FOUND;                       /* 撕裂/损坏 → 视为不存在 */
    }

    memcpy(name, rec + 8, namelen);
    name[namelen] = '\0';
    *state = rec[2];
    *seq = __rd32(rec + 3);
    *data_off = rec_off + 12 + namelen;
    *data_len = dlen;
    *rec_len = total;
    return OPRT_OK;
}

static int __index_find(const char *name)
{
    for (uint32_t i = 0; i < s_index_cnt; i++) {
        if (0 == strcmp(s_index[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

/* 把 (name, seq) 写入索引：已存在且 seq 更大则更新，否则追加 */
static void __index_put(const TAL_DATA_ITEM_T *item)
{
    int idx = __index_find(item->name);
    if (idx >= 0) {
        if (item->seq > s_index[idx].seq) {
            s_index[idx] = *item;
        }
        return;
    }
    if (s_index_cnt < TAL_DATA_INDEX_MAX) {
        s_index[s_index_cnt++] = *item;
    } else {
        PR_WARN("tal_data: index full (%d), drop %s", TAL_DATA_INDEX_MAX, item->name);
    }
}

static OPERATE_RET __ops_init(void)
{
    TAL_DATA_REGION_T region = {0};
    TAL_DATA_OPS_T *ops = tal_data_get_ops();

    if (NULL == ops || NULL == ops->init) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != ops->init(&region)) {
        PR_ERR("tal_data: region init fail");
        return OPRT_COM_ERROR;
    }
    if (region.size < 16 * 1024 || 0 == region.erase_granularity) {
        PR_ERR("tal_data: region too small");
        return OPRT_COM_ERROR;
    }
    s_region_size = region.size;
    return OPRT_OK;
}

/* 顺序扫描：区域为线性日志（记录从 0 连续追加，从不回卷/回收），
 * 遇第一条不可解析记录即日志尾；建索引并求 max_seq / tail。 */
static OPERATE_RET __scan(void)
{
    TAL_DATA_OPS_T *ops = tal_data_get_ops();
    uint8_t *buf = NULL;
    uint32_t off = 0, max_seq = 0, max_end = 0;
    int have = 0;

    buf = tal_malloc(64);
    if (NULL == buf) {
        return OPRT_MALLOC_FAILED;
    }

    s_index_cnt = 0;
    while (off + 16 <= s_region_size) {
        uint8_t state = 0;
        char name[TAL_DATA_NAME_MAX + 1] = {0};
        uint32_t seq = 0, data_off = 0, data_len = 0, rec_len = 0;

        /* 先读 8 字节定长头，拿到 namelen 后读整条 */
        if (OPRT_OK != ops->read(off, buf, 8)) {
            break;
        }
        if (TAL_DATA_MAGIC != __rd16(buf)) {
            break;                                    /* 到空闲区（0xFF）为止 */
        }
        uint32_t namelen = buf[7];
        if (namelen < 1 || namelen > TAL_DATA_NAME_MAX) {
            break;
        }
        uint32_t need = 12 + namelen + 4;             /* 到 datalen 结尾，先读这部分 */
        if (off + need > s_region_size) {
            break;
        }
        uint8_t *hdr = tal_malloc(need);
        if (NULL == hdr) {
            tal_free(buf);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, hdr, need)) {
            tal_free(hdr);
            break;
        }
        uint32_t dlen = __rd32(hdr + 8 + namelen);
        uint32_t total = __rec_len(namelen, dlen);
        tal_free(hdr);

        if (off + total > s_region_size || total > TAL_DATA_PLAIN_MAX + 128) {
            break;
        }
        uint8_t *rec = tal_malloc(total);
        if (NULL == rec) {
            tal_free(buf);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, rec, total) ||
            OPRT_OK != __rec_decode(rec, total, off, &state, &seq, name, &data_off, &data_len, &rec_len)) {
            tal_free(rec);
            break;                                    /* 撕裂/损坏 → 视为日志尾 */
        }
        tal_free(rec);

        if (TAL_DATA_ST_VALID == state) {
            TAL_DATA_ITEM_T item;
            memset(&item, 0, sizeof(item));
            strncpy(item.name, name, TAL_DATA_NAME_MAX);
            item.rec_off = off;
            item.rec_len = rec_len;
            item.data_off = data_off;
            item.data_len = data_len;
            item.seq = seq;
            __index_put(&item);
        } else if (TAL_DATA_ST_TOMB == state) {
            /* 墓碑参与 seq 比较：seq 更大（或相等）的墓碑移除同名索引项；
             * 否则 erase 会在重启后被旧 VALID 记录"复活"。
             * 依赖扫描顺序内 seq 单调（后扫到的记录 seq 不大于先扫到的）；线性追加
             * （记录按 seq 递增顺序落盘）天然满足该不变量。若将来引入回卷/搬移，
             * 需改为扫描末尾按 name 归并。 */
            int idx = __index_find(name);
            if (idx >= 0 && seq >= s_index[idx].seq) {
                for (uint32_t i = (uint32_t)idx; i + 1 < s_index_cnt; i++) {
                    s_index[i] = s_index[i + 1];
                }
                s_index_cnt--;
            }
        }
        if (seq > max_seq || 0 == have) {
            max_seq = seq;
            max_end = off + rec_len;
        }
        have = 1;
        off += rec_len;
    }

    tal_free(buf);

    if (0 == have) {
        s_max_seq = 0;
        s_tail = 0;
        return OPRT_OK;
    }
    s_max_seq = max_seq;
    s_tail = max_end;
    return OPRT_OK;
}
OPERATE_RET tal_data_init(const char *seed, const char *key)
{
    uint8_t digest[32] = {0};

    if (NULL == seed || NULL == key) {
        return OPRT_INVALID_PARM;
    }
    if (s_inited) {
        return OPRT_OK;
    }
    if (OPRT_OK != __ops_init()) {
        return OPRT_COM_ERROR;
    }
    /* key/IV 派生：沿用现有 tal_kv 方案（seed/key 各做一次 SHA256 取前 16 字节） */
    if (OPRT_OK != tal_sha256_ret((const uint8_t *)seed, strlen(seed), digest, 0)) {
        return OPRT_COM_ERROR;
    }
    memcpy(s_iv, digest, 16);
    if (OPRT_OK != tal_sha256_ret((const uint8_t *)key, strlen(key), digest, 0)) {
        return OPRT_COM_ERROR;
    }
    memcpy(s_key, digest, 16);

    if (OPRT_OK != __scan()) {
        return OPRT_COM_ERROR;
    }
    s_inited = 1;
    return OPRT_OK;
}

/* 追加一条记录：线性追加，区域满即报错（flash 不做回收，绝不覆盖已有记录） */
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len)
{
    TAL_DATA_OPS_T *ops = tal_data_get_ops();

    if (s_tail + rec_len > s_region_size) {
        return OPRT_COM_ERROR;                       /* 区域满：绝不覆盖已有记录 */
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}

static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    return __append_rec(rec, rec_len);
}
OPERATE_RET tal_data_write(const char *name, const uint8_t *buf, size_t len)
{
    uint8_t *ct = NULL, *rec = NULL;
    uint8_t iv_local[16];
    uint32_t ct_len = 0, rec_len;
    OPERATE_RET ret;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name) || NULL == buf || 0 == len || len > TAL_DATA_PLAIN_MAX) {
        return OPRT_INVALID_PARM;
    }
    /* 索引满：先拒绝，避免写入一条无法被索引（也就无法读回）的记录 */
    if (__index_find(name) < 0 && s_index_cnt >= TAL_DATA_INDEX_MAX) {
        PR_ERR("tal_data: index full (%d), reject write %s", TAL_DATA_INDEX_MAX, name);
        return OPRT_COM_ERROR;
    }
    memcpy(iv_local, s_iv, 16);                      /* mbedtls CBC 会改写传入的 iv，必须用副本 */
    if (OPRT_OK != tal_aes128_cbc_encode((uint8_t *)buf, (uint32_t)len, s_key, iv_local, &ct, &ct_len)) {
        return OPRT_COM_ERROR;
    }
    rec_len = __rec_len((uint32_t)strlen(name), ct_len);
    rec = tal_malloc(rec_len);
    if (NULL == rec) {
        tal_aes_free_data(ct);
        return OPRT_MALLOC_FAILED;
    }
    __rec_encode(rec, TAL_DATA_ST_VALID, ++s_max_seq, name, ct, ct_len);
    ret = __append(rec, rec_len);
    if (OPRT_OK == ret) {
        TAL_DATA_ITEM_T item;
        int idx;
        memset(&item, 0, sizeof(item));
        strncpy(item.name, name, TAL_DATA_NAME_MAX);
        item.rec_off = s_tail - rec_len;
        item.rec_len = rec_len;
        item.data_off = item.rec_off + 12 + (uint32_t)strlen(name);
        item.data_len = ct_len;
        item.seq = s_max_seq;
        idx = __index_find(name);
        if (idx >= 0) {
            s_index[idx] = item;
        } else if (s_index_cnt < TAL_DATA_INDEX_MAX) {
            s_index[s_index_cnt++] = item;
        }
    } else {
        s_max_seq--;                                 /* 回滚 seq */
    }
    tal_free(rec);
    tal_aes_free_data(ct);
    return ret;
}

OPERATE_RET tal_data_read(const char *name, uint8_t *buf, size_t buf_size, size_t *out_len)
{
    TAL_DATA_OPS_T *ops = tal_data_get_ops();
    int idx;
    uint8_t *rec = NULL, *ct = NULL, *pt = NULL;
    uint8_t state = 0;
    uint8_t iv_local[16];
    char rname[TAL_DATA_NAME_MAX + 1];
    uint32_t seq = 0, doff = 0, dlen = 0, rlen = 0, data_in_rec;
    uint32_t pt_len = 0;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name) || NULL == out_len) {
        return OPRT_INVALID_PARM;
    }
    idx = __index_find(name);
    if (idx < 0) {
        return OPRT_NOT_FOUND;
    }
    /* 读整条记录并复核 CRC：扫描后数据可能被破坏（撕裂写/位翻转） */
    rec = tal_malloc(s_index[idx].rec_len);
    if (NULL == rec) {
        return OPRT_MALLOC_FAILED;
    }
    if (OPRT_OK != ops->read(s_index[idx].rec_off, rec, s_index[idx].rec_len) ||
        OPRT_OK != __rec_decode(rec, s_index[idx].rec_len, s_index[idx].rec_off, &state, &seq, rname, &doff, &dlen,
                                &rlen) ||
        0 != strcmp(rname, name) ||                  /* 索引项悬垂/错位：记录内容与索引不符 */
        TAL_DATA_ST_VALID != state) {
        tal_free(rec);
        return OPRT_NOT_FOUND;
    }
    ct = tal_malloc(dlen);
    if (NULL == ct) {
        tal_free(rec);
        return OPRT_MALLOC_FAILED;
    }
    data_in_rec = s_index[idx].data_off - s_index[idx].rec_off;
    memcpy(ct, rec + data_in_rec, dlen);
    tal_free(rec);

    memcpy(iv_local, s_iv, 16);                      /* mbedtls CBC 会改写传入的 iv，必须用副本 */
    if (OPRT_OK != tal_aes128_cbc_decode(ct, dlen, s_key, iv_local, &pt, &pt_len)) {
        tal_free(ct);
        return OPRT_COM_ERROR;
    }
    tal_free(ct);

    /* decode 回填的是密文长度，明文仍带 PKCS7 填充 → 求真实长度 */
    {
        int32_t actual = tal_aes_get_actual_length(pt, pt_len);
        if (actual < 0) {
            tal_aes_free_data(pt);
            return OPRT_COM_ERROR;
        }
        pt_len = (uint32_t)actual;
    }

    *out_len = pt_len;
    if (NULL == buf) {
        tal_aes_free_data(pt);
        return OPRT_OK;                              /* 探测尺寸 */
    }
    if (pt_len > buf_size) {
        tal_aes_free_data(pt);
        return OPRT_INVALID_PARM;                    /* 已回填真实长度 */
    }
    memcpy(buf, pt, pt_len);
    tal_aes_free_data(pt);
    return OPRT_OK;
}

OPERATE_RET tal_data_erase(const char *name)
{
    uint8_t *rec = NULL;
    uint32_t rec_len;
    int idx;
    OPERATE_RET ret;

    if (!s_inited) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != __name_check(name)) {
        return OPRT_INVALID_PARM;
    }
    idx = __index_find(name);
    if (idx < 0) {
        return OPRT_OK;                              /* 幂等 */
    }
    rec_len = __rec_len((uint32_t)strlen(name), 0);
    rec = tal_malloc(rec_len);
    if (NULL == rec) {
        return OPRT_MALLOC_FAILED;
    }
    __rec_encode(rec, TAL_DATA_ST_TOMB, ++s_max_seq, name, NULL, 0);
    ret = __append(rec, rec_len);
    if (OPRT_OK == ret) {
        for (uint32_t i = (uint32_t)idx; i + 1 < s_index_cnt; i++) {
            s_index[i] = s_index[i + 1];             /* 从索引移除 */
        }
        s_index_cnt--;
    } else {
        s_max_seq--;
    }
    tal_free(rec);
    return ret;
}
