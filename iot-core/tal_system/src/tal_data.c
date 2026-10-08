/**
 * @file tal_data.c
 * @brief tal_data K-V 引擎：日志式布局（追加 + 墓碑 + seq + CRC + GC 压缩）+ AES
 *
 * 区域 = 环形日志；记录从低偏移追加，写满时把各 name 的最新记录复制到空闲尾部
 * （分配新 seq）再推进 head。掉电安全：已复制的新 seq 胜出、未复制的用旧记录、
 * 撕裂记录 CRC 失败被忽略。
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
static uint32_t s_head;
static uint32_t s_region_size;
static uint32_t s_erase_gran;
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
    s_erase_gran = region.erase_granularity;
    return OPRT_OK;
}

/* 全区域扫描：建索引、求 max_seq / tail / head */
/* 全区域扫描：分块读入内存找 magic；命中则按 CRC 验证——
 * 通过 = 记录（按记录长度前进）；不通过 = 数据内的假 magic（前进 1 字节）。
 * 回收擦除后的 0xFF 空洞因此可被跳过。 */
static OPERATE_RET __scan(void)
{
    TAL_DATA_OPS_T *ops = tal_data_get_ops();
    uint8_t *chunk = NULL;
    const uint32_t CHUNK = 512;
    uint32_t off = 0, min_seq = 0xFFFFFFFF, min_off = 0, max_seq = 0, max_end = 0;
    int have = 0;

    chunk = tal_malloc(CHUNK);
    if (NULL == chunk) {
        return OPRT_MALLOC_FAILED;
    }

    s_index_cnt = 0;
    while (off + 16 <= s_region_size) {
        uint32_t scan = 0, limit = (s_region_size - off < CHUNK) ? (s_region_size - off) : CHUNK;
        uint8_t state = 0;
        char name[TAL_DATA_NAME_MAX + 1] = {0};
        uint32_t seq = 0, data_off = 0, data_len = 0, rec_len = 0;
        uint32_t namelen, dlen, total;
        uint8_t *rec = NULL;

        if (OPRT_OK != ops->read(off, chunk, limit)) {
            break;
        }
        /* 在块内找 magic */
        while (scan + 2 <= limit && TAL_DATA_MAGIC != __rd16(chunk + scan)) {
            scan++;
        }
        if (scan + 2 > limit) {
            off += limit;                            /* 本块无 magic → 跳过整块 */
            continue;
        }
        off += scan;                                 /* 定位到候选记录起点 */

        /* 候选点：读定长头拿 namelen/datalen，再读整条验 CRC */
        uint8_t hdr[16];
        if (off + 16 > s_region_size || OPRT_OK != ops->read(off, hdr, 16)) {
            break;
        }
        namelen = hdr[7];
        if (namelen < 1 || namelen > TAL_DATA_NAME_MAX) {
            off++;                                   /* 假 magic → 前进 1 字节 */
            continue;
        }
        dlen = __rd32(hdr + 8 + namelen);
        total = __rec_len(namelen, dlen);
        if (total > TAL_DATA_PLAIN_MAX + 128 || off + total > s_region_size) {
            off++;
            continue;
        }
        rec = tal_malloc(total);
        if (NULL == rec) {
            tal_free(chunk);
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != ops->read(off, rec, total) ||
            OPRT_OK != __rec_decode(rec, total, off, &state, &seq, name, &data_off, &data_len, &rec_len)) {
            tal_free(rec);
            off++;                                   /* 假 magic / 撕裂记录 → 前进 1 字节 */
            continue;
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
             * 不变量：依赖"扫描顺序内 seq 单调"（后扫到的记录 seq 不大于先扫到的）；
             * GC 副本追加到更高偏移且分配新 seq，该不变量仍成立；若将来 GC 布局破坏它，
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
        if (seq < min_seq || 0 == have) {
            min_seq = seq;
            min_off = off;
        }
        have = 1;
        off += rec_len;                              /* 记录有效 → 按记录长度前进 */
    }

    tal_free(chunk);

    if (0 == have) {
        s_max_seq = 0;
        s_tail = 0;
        s_head = 0;
        return OPRT_OK;
    }
    s_max_seq = max_seq;
    s_tail = max_end;
    s_head = min_off;
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

static OPERATE_RET __compact(void);
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len);

/* ---- 空闲空间核算（环形：tail 顺时针到 head） ---- */
/* 空闲间隙（环形：tail 顺时针到 head）；0 表示"空"（head==tail） */
static uint32_t __free_gap(void)
{
    return (s_head - s_tail + s_region_size) % s_region_size;
}

/* 可用空闲：空区视为整区；否则保留 1 字节余量，使 tail 永不追上 head（消除满/空歧义） */
static uint32_t __usable_free(void)
{
    uint32_t gap = __free_gap();
    return (0 == gap) ? s_region_size : (gap - 1);
}

/* 占用区内的垃圾字节数（占用 - 有效） */
static uint32_t __garbage_len(void)
{
    uint32_t live = 0, occupied;
    for (uint32_t i = 0; i < s_index_cnt; i++) {
        live += s_index[i].rec_len;
    }
    occupied = (s_head == s_tail) ? 0 : (s_region_size - __free_gap());
    return (occupied > live) ? (occupied - live) : 0;
}

/* 追加一条记录：只写空闲间隙；跨区尾仅在"回卷后不越过 head"时允许 PAD 回卷 */
static OPERATE_RET __append_rec(const uint8_t *rec, uint32_t rec_len)
{
    TAL_DATA_OPS_T *ops = tal_data_get_ops();

    if (s_tail + rec_len > s_region_size) {
        uint32_t pad_len = s_region_size - s_tail;
        /* 回卷后从 0 起写，只有 [0, head) 是空闲；head==0 时无处可回卷 */
        if (0 == s_head || rec_len + 1 > s_head) {
            return OPRT_COM_ERROR;
        }
        if (pad_len >= 16) {
            uint8_t *pad = tal_malloc(pad_len);
            if (NULL == pad) {
                return OPRT_MALLOC_FAILED;
            }
            memset(pad, 0xFF, pad_len);
            __rec_encode(pad, TAL_DATA_ST_PAD, ++s_max_seq, "pad", NULL, 0);
            if (OPRT_OK != ops->write(s_tail, pad, pad_len)) {
                tal_free(pad);
                s_max_seq--;
                return OPRT_COM_ERROR;
            }
            tal_free(pad);
        }
        s_tail = 0;
    }
    if (rec_len + 1 > __usable_free()) {
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK != ops->write(s_tail, rec, rec_len)) {
        return OPRT_COM_ERROR;
    }
    s_tail += rec_len;
    return OPRT_OK;
}

/* 追加：主动阈值压缩（等写不下再压就晚了——那时 free < live，副本无处放） */
static OPERATE_RET __append(const uint8_t *rec, uint32_t rec_len)
{
    if (__usable_free() < s_region_size / 2 && __garbage_len() > 0) {
        (void)__compact();
    }
    if (OPRT_OK == __append_rec(rec, rec_len)) {
        return OPRT_OK;
    }
    if (OPRT_OK != __compact()) {
        return OPRT_COM_ERROR;                       /* 压缩也无法腾出空间 → 区域满 */
    }
    return __append_rec(rec, rec_len);
}

/* GC：把各 name 最新记录复制到空闲间隙（新 seq），推进 head 并擦除回收区。
 * 副本起点对齐到擦除粒度，保证回收擦除不触碰副本。 */
static OPERATE_RET __compact(void)
{
    uint32_t live_len = 0, old_tail, old_head, copy_start;
    TAL_DATA_ITEM_T old_index[TAL_DATA_INDEX_MAX];
    uint32_t old_cnt;

    for (uint32_t i = 0; i < s_index_cnt; i++) {
        live_len += s_index[i].rec_len;
    }
    if (live_len + 16 > __usable_free()) {
        return OPRT_COM_ERROR;                       /* 活数据装不进空闲间隙（已知限制：活数据 > 半区） */
    }
    if (0 == live_len) {
        s_head = s_tail;                             /* 无有效数据：整区可回收 */
        return OPRT_OK;
    }

    memcpy(old_index, s_index, sizeof(old_index));
    old_cnt = s_index_cnt;
    old_tail = s_tail;
    old_head = s_head;

    copy_start = (old_tail + s_erase_gran - 1) / s_erase_gran * s_erase_gran;
    if (copy_start + live_len + 16 > s_region_size) {
        return OPRT_COM_ERROR;                       /* 对齐后放不下 */
    }
    s_tail = copy_start;

    for (uint32_t i = 0; i < old_cnt; i++) {
        TAL_DATA_ITEM_T item = old_index[i];
        uint8_t *ct = NULL, *new_rec = NULL;
        uint32_t new_len;
        OPERATE_RET ret;

        ct = tal_malloc(item.data_len);
        if (NULL == ct) {
            return OPRT_MALLOC_FAILED;
        }
        if (OPRT_OK != tal_data_get_ops()->read(item.data_off, ct, item.data_len)) {
            tal_free(ct);
            return OPRT_COM_ERROR;
        }
        new_len = __rec_len((uint32_t)strlen(item.name), item.data_len);
        new_rec = tal_malloc(new_len);
        if (NULL == new_rec) {
            tal_free(ct);
            return OPRT_MALLOC_FAILED;
        }
        __rec_encode(new_rec, TAL_DATA_ST_VALID, ++s_max_seq, item.name, ct, item.data_len);
        ret = __append_rec(new_rec, new_len);
        if (OPRT_OK == ret) {
            /* 用新的 s_tail 反推副本位置（回卷安全） */
            s_index[i].rec_off = (s_tail >= new_len) ? (s_tail - new_len) : (s_region_size + s_tail - new_len);
            s_index[i].rec_len = new_len;
            s_index[i].data_off = s_index[i].rec_off + 12 + (uint32_t)strlen(item.name);
            s_index[i].seq = s_max_seq;
        }
        tal_free(new_rec);
        tal_free(ct);
        if (OPRT_OK != ret) {
            return ret;                              /* 部分完成的副本 seq 更大 → 重启后按 seq 收敛，安全 */
        }
    }

    s_head = old_tail;                               /* 旧记录区变空闲 */
    /* 回收擦除：上界 = copy_start（已对齐），严格不触碰副本 */
    {
        uint32_t era_beg = (old_head / s_erase_gran) * s_erase_gran;
        if (copy_start > era_beg) {
            if (OPRT_OK != tal_data_get_ops()->erase(era_beg, copy_start - era_beg)) {
                PR_WARN("tal_data: reclaim erase fail (%u,%u)", era_beg, copy_start - era_beg);
            }
        }
    }
    return OPRT_OK;
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
