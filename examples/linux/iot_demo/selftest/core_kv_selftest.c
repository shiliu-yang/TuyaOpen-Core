/**
 * @file core_kv_selftest.c
 * @brief Black-box selftest of the core_kv public API (init / set / get / del)
 *
 * This file uses only the 4 public interfaces of core_kv.h and does not touch
 * implementation details such as the ops card, region primitives, record
 * format, or encryption: the assertions rely only on the semantics promised
 * by the header -- overwrite, binary safety (values containing 0x00 are
 * returned verbatim with their length), get returns heap memory with a
 * trailing '\0' (released by the caller with core_free), outputs are set to
 * NULL/0 on failure, a missing key returns OPRT_NOT_FOUND, and del is
 * idempotent (missing keys are also OK). The test should therefore hold for
 * any platform implementation (Linux littlefs+AES, NVS-style chip KV).
 * Implementation-specific behavior such as storage medium, per-entry limit,
 * name charset/length, and regions/serialization is out of scope.
 *
 * Each case deletes its own keys with core_kv_del, so the test can be run
 * repeatedly (it does not assume clean storage).
 *
 * Run: cd build && ./core_kv_selftest
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "core_kv.h"
#include "core_memory.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

/* seed/key are 16 characters each (core_kv_cfg_t.seed/key are char[17]; the port decides their use) */
static core_kv_cfg_t s_kv_cfg = {
    .seed = "0123456789abcdef",
    .key  = "fedcba9876543210",
};

/* Read-back check: get succeeds + heap pointer non-NULL + length matches + content matches + trailing '\0'; released after the check */
static int check_roundtrip(const char *key, const uint8_t *expect, size_t expect_len)
{
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == core_kv_get(key, &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(out_len == expect_len);
    if (expect_len > 0) {
        CHECK(0 == memcmp(expect, rd, expect_len));
    }
    CHECK(0 == rd[out_len]);                        /* includes a trailing '\0' on success, so it can be used as a C string */
    core_free(rd);
    return 0;
}

/* 1. Basic set/get/del: raw byte round-trip + string semantics + binary safety + invisible after delete */
static int test_basic_rw(void)
{
    uint8_t wr[64];
    uint8_t bin[16];
    uint8_t *rd = NULL;
    size_t out_len = 0;
    const char *str_val = "hello.kv";

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    /* 64B incrementing-byte round-trip (length + content byte-for-byte identical) */
    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(i + 1);
    }
    CHECK(OPRT_OK == core_kv_set("t1.raw", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t1.raw", wr, sizeof(wr)));

    /* String semantics: store a C string (without the trailing '\0') and read it back directly as a string */
    CHECK(OPRT_OK == core_kv_set("t1.str", (const uint8_t *)str_val, strlen(str_val)));
    CHECK(OPRT_OK == core_kv_get("t1.str", &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(out_len == strlen(str_val));
    CHECK(0 == strcmp((char *)rd, str_val));
    CHECK(0 == rd[out_len]);
    core_free(rd);
    rd = NULL;

    /* Binary safety: values containing 0x00 are returned verbatim with their length */
    memset(bin, 0, sizeof(bin));
    bin[0] = 0x11;
    bin[8] = 0x22;
    CHECK(OPRT_OK == core_kv_set("t1.bin", bin, sizeof(bin)));
    CHECK(0 == check_roundtrip("t1.bin", bin, sizeof(bin)));

    /* The key is invisible after del */
    CHECK(OPRT_OK == core_kv_del("t1.raw"));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == core_kv_get("t1.raw", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);

    /* Clean up all keys used by this case */
    CHECK(OPRT_OK == core_kv_del("t1.raw"));
    CHECK(OPRT_OK == core_kv_del("t1.str"));
    CHECK(OPRT_OK == core_kv_del("t1.bin"));
    return 0;
}

/* 2. Overwrite: a later write to the same key replaces the earlier one (variable length), another key is unaffected */
static int test_overwrite(void)
{
    uint8_t v1[64], v2[32], other[48];

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    memset(v1, 0x11, sizeof(v1));
    memset(v2, 0x22, sizeof(v2));
    memset(other, 0x33, sizeof(other));

    CHECK(OPRT_OK == core_kv_set("t2.kv", v1, sizeof(v1)));
    CHECK(OPRT_OK == core_kv_set("t2.other", other, sizeof(other)));

    /* Second write (shorter as well) -> the read-back must be the later value */
    CHECK(OPRT_OK == core_kv_set("t2.kv", v2, sizeof(v2)));
    CHECK(0 == check_roundtrip("t2.kv", v2, sizeof(v2)));

    /* The other key is unaffected */
    CHECK(0 == check_roundtrip("t2.other", other, sizeof(other)));

    CHECK(OPRT_OK == core_kv_del("t2.kv"));
    CHECK(OPRT_OK == core_kv_del("t2.other"));
    return 0;
}

/* 3. Missing key: get returns NOT_FOUND and sets outputs to NULL/0; del is idempotent and returns OPRT_OK */
static int test_missing_key(void)
{
    uint8_t *rd;
    size_t out_len;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    /* Delete first for idempotency so "missing" does not depend on clean storage */
    CHECK(OPRT_OK == core_kv_del("t3.none"));

    /* get: NOT_FOUND + outputs set to NULL/0 (verified with preset sentinel values) */
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == core_kv_get("t3.none", &rd, &out_len));
    CHECK(NULL == rd);
    CHECK(0 == out_len);

    /* del idempotence: repeated calls on a missing key all return OPRT_OK */
    CHECK(OPRT_OK == core_kv_del("t3.none"));
    CHECK(OPRT_OK == core_kv_del("t3.none"));

    /* A key that existed and was deleted can be deleted again (idempotent), and get still returns NOT_FOUND */
    CHECK(OPRT_OK == core_kv_set("t3.kv", (const uint8_t *)"x", 1));
    CHECK(OPRT_OK == core_kv_del("t3.kv"));
    CHECK(OPRT_OK == core_kv_del("t3.kv"));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == core_kv_get("t3.kv", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);
    return 0;
}

/* 4a. Empty string: write length 1 (only the trailing '\0') -> read back length==1 and first byte 0 */
static int test_empty_value(void)
{
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    CHECK(OPRT_OK == core_kv_set("t4.empty", (const uint8_t *)"", 1));
    CHECK(OPRT_OK == core_kv_get("t4.empty", &rd, &out_len));
    CHECK(NULL != rd);
    CHECK(1 == out_len);
    CHECK(0 == rd[0]);
    CHECK(0 == rd[1]);                              /* the trailing '\0' is appended by get */
    core_free(rd);
    rd = NULL;

    CHECK(OPRT_OK == core_kv_del("t4.empty"));
    return 0;
}

/* 4b. Invalid length 0: the contract (core_kv_set in core_kv.h) states that length
 *     must be > 0 and that 0 returns OPRT_INVALID_PARM, so assert the exact error
 *     code plus no corrupted state left behind */
static int test_zero_length(void)
{
    const uint8_t dummy = 0x5A;
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));
    CHECK(OPRT_OK == core_kv_del("t4.zero"));        /* idempotent cleanup, does not depend on clean storage */

    CHECK(OPRT_INVALID_PARM == core_kv_set("t4.zero", &dummy, 0));

    /* An invalid write must not persist: the read-back should be NOT_FOUND with outputs zeroed */
    CHECK(OPRT_NOT_FOUND == core_kv_get("t4.zero", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);

    /* The key must still be writable normally (no corrupted state left behind) */
    CHECK(OPRT_OK == core_kv_del("t4.zero"));
    CHECK(OPRT_OK == core_kv_set("t4.zero", (const uint8_t *)"ok", 2));
    CHECK(0 == check_roundtrip("t4.zero", (const uint8_t *)"ok", 2));
    CHECK(OPRT_OK == core_kv_del("t4.zero"));
    return 0;
}

/* 4c. Large value: 4096-byte round-trip (does not assert the per-entry platform limit; only verifies the size the current implementation can store) */
static int test_large_value(void)
{
    const size_t big_len = 4096;
    uint8_t *big = NULL;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    big = (uint8_t *)core_malloc(big_len);
    CHECK(NULL != big);
    for (size_t i = 0; i < big_len; i++) {
        big[i] = (uint8_t)(i * 7 + 3);              /* fill pattern containing 0x00, also verifies binary safety */
    }

    CHECK(OPRT_OK == core_kv_set("t5.big", big, big_len));
    CHECK(0 == check_roundtrip("t5.big", big, big_len));

    CHECK(OPRT_OK == core_kv_del("t5.big"));
    core_free(big);
    return 0;
}

/* 5. get memory semantics: every success returns independent heap memory (the
 *    pointers from two reads must differ -- both blocks belong to the caller, and
 *    the same address would mean overlapping allocations), identical content,
 *    trailing '\0', released with core_free */
static int test_get_memory(void)
{
    uint8_t wr[32];
    uint8_t *rd1 = NULL, *rd2 = NULL;
    size_t len1 = 0, len2 = 0;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(0xA0 + i);
    }
    CHECK(OPRT_OK == core_kv_set("t6.kv", wr, sizeof(wr)));

    /* Read twice in a row; both results must be valid (the first is not released before the second read) */
    CHECK(OPRT_OK == core_kv_get("t6.kv", &rd1, &len1));
    CHECK(OPRT_OK == core_kv_get("t6.kv", &rd2, &len2));
    CHECK(NULL != rd1 && NULL != rd2);
    CHECK(rd1 != rd2);                              /* two independent heap blocks */
    CHECK(len1 == sizeof(wr) && len2 == sizeof(wr));
    CHECK(0 == memcmp(rd1, wr, sizeof(wr)));
    CHECK(0 == memcmp(rd2, wr, sizeof(wr)));
    CHECK(0 == rd1[len1] && 0 == rd2[len2]);        /* both can safely be used as C strings */
    core_free(rd1);
    core_free(rd2);
    rd1 = NULL;
    rd2 = NULL;

    CHECK(OPRT_OK == core_kv_del("t6.kv"));
    return 0;
}

/* 6. Repeated init: two consecutive core_kv_init calls both succeed, then set/get works normally */
static int test_reinit(void)
{
    uint8_t wr[48];

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));
    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    memset(wr, 0x6C, sizeof(wr));
    CHECK(OPRT_OK == core_kv_set("t7.kv", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t7.kv", wr, sizeof(wr)));
    CHECK(OPRT_OK == core_kv_del("t7.kv"));
    return 0;
}

/* 7. Persistence: write -> init again -> content unchanged; after del, init again must not resurrect it */
static int test_persistence(void)
{
    uint8_t wr[128];
    uint8_t *rd = NULL;
    size_t out_len = 0;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    for (size_t i = 0; i < sizeof(wr); i++) {
        wr[i] = (uint8_t)(0xF0 ^ i);
    }
    CHECK(OPRT_OK == core_kv_set("t8.kv", wr, sizeof(wr)));

    /* Content is unchanged after reopening (init again) */
    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));
    CHECK(0 == check_roundtrip("t8.kv", wr, sizeof(wr)));

    /* Deletion is persistent: still NOT_FOUND after reopening */
    CHECK(OPRT_OK == core_kv_del("t8.kv"));
    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    CHECK(OPRT_NOT_FOUND == core_kv_get("t8.kv", &rd, &out_len));
    CHECK(NULL == rd && 0 == out_len);
    return 0;
}

/* 8. Multithreaded concurrency: 4 threads each write their own key (50 overwrites
 *    of 512B each); after join, every key reads back = that thread's last write;
 *    the sentinel key set before concurrency is unaffected.
 *    Thread functions return void*, so the CHECK macro's return -1 does not apply
 *    -> use the fail_at flag to carry failures back. */
#define CONC_THREADS 4
#define CONC_ITERS   50
#define CONC_VAL_LEN 512

typedef struct {
    int id;
    int fail_at;                                    /* index of the first failed write in the thread, -1 = all succeeded */
} conc_arg_t;

static void *conc_worker(void *arg)
{
    conc_arg_t *a = (conc_arg_t *)arg;
    uint8_t val[CONC_VAL_LEN];
    char name[16];

    snprintf(name, sizeof(name), "t9.conc.%d", a->id);
    for (int it = 0; it < CONC_ITERS; it++) {
        memset(val, (uint8_t)(a->id * 64 + it), sizeof(val));
        if (OPRT_OK != core_kv_set(name, val, sizeof(val))) {
            a->fail_at = it;
            return NULL;
        }
    }
    return NULL;
}

static int test_concurrent(void)
{
    pthread_t th[CONC_THREADS];
    conc_arg_t args[CONC_THREADS];
    uint8_t sentinel[128], expect[CONC_VAL_LEN];
    char name[16];

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    /* Sentinel key written before concurrency: its content must be unchanged afterwards */
    memset(sentinel, 0x9C, sizeof(sentinel));
    CHECK(OPRT_OK == core_kv_set("t9.sentinel", sentinel, sizeof(sentinel)));

    for (int i = 0; i < CONC_THREADS; i++) {
        args[i].id = i;
        args[i].fail_at = -1;
        CHECK(0 == pthread_create(&th[i], NULL, conc_worker, &args[i]));
    }
    for (int i = 0; i < CONC_THREADS; i++) {
        CHECK(0 == pthread_join(th[i], NULL));
        CHECK(-1 == args[i].fail_at);               /* concurrent writes must not report errors */
    }

    /* Every key must read back, with content = that thread's last write */
    for (int i = 0; i < CONC_THREADS; i++) {
        snprintf(name, sizeof(name), "t9.conc.%d", i);
        memset(expect, (uint8_t)(i * 64 + (CONC_ITERS - 1)), sizeof(expect));
        CHECK(0 == check_roundtrip(name, expect, CONC_VAL_LEN));
    }

    /* The sentinel is unaffected by concurrency */
    CHECK(0 == check_roundtrip("t9.sentinel", sentinel, sizeof(sentinel)));

    /* Clean up the concurrent keys + sentinel */
    for (int i = 0; i < CONC_THREADS; i++) {
        snprintf(name, sizeof(name), "t9.conc.%d", i);
        CHECK(OPRT_OK == core_kv_del(name));
    }
    CHECK(OPRT_OK == core_kv_del("t9.sentinel"));
    return 0;
}

/* 10. Wrong key: after re-init with a different seed/key set, old data must not
 *     decrypt (not OK, and outputs set to NULL/0); after restoring the original
 *     cfg and re-initializing, the data is intact (verifies that the port
 *     refreshes key derivation from the supplied config on every init). The case
 *     depends on the encryption implementation -- the Linux port has AES-128-CBC;
 *     platforms without encryption (NVS direct mapping) may adjust or drop it. */
static int test_wrong_key(void)
{
    static const uint8_t wr[32] = "wrong-key-payload-0123456789abc";
    core_kv_cfg_t other_cfg = {
        .seed = "aaaaaaaaaaaaaaaa",
        .key  = "bbbbbbbbbbbbbbbb",
    };
    uint8_t *rd = NULL;
    size_t out_len = 0;
    int ret;

    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));

    CHECK(OPRT_OK == core_kv_set("t10.wrong", wr, sizeof(wr)));
    CHECK(0 == check_roundtrip("t10.wrong", wr, sizeof(wr)));

    /* Switch to another seed/key set: the old ciphertext cannot be decrypted -> must error, with outputs set to NULL/0 (verified with preset sentinels) */
    CHECK(OPRT_OK == core_kv_init(&other_cfg));
    rd = (uint8_t *)0x1;
    out_len = 0x5A5A;
    ret = core_kv_get("t10.wrong", &rd, &out_len);
    CHECK(OPRT_OK != ret);
    CHECK(NULL == rd && 0 == out_len);

    /* Restore the original cfg: the data must be intact (init is idempotent but refreshes key derivation) */
    CHECK(OPRT_OK == core_kv_init(&s_kv_cfg));
    CHECK(0 == check_roundtrip("t10.wrong", wr, sizeof(wr)));

    CHECK(OPRT_OK == core_kv_del("t10.wrong"));
    return 0;
}

int main(void)
{
    if (0 != test_basic_rw()) {
        return 1;
    }
    if (0 != test_overwrite()) {
        return 1;
    }
    if (0 != test_missing_key()) {
        return 1;
    }
    if (0 != test_empty_value()) {
        return 1;
    }
    if (0 != test_zero_length()) {
        return 1;
    }
    if (0 != test_large_value()) {
        return 1;
    }
    if (0 != test_get_memory()) {
        return 1;
    }
    if (0 != test_reinit()) {
        return 1;
    }
    if (0 != test_persistence()) {
        return 1;
    }
    if (0 != test_concurrent()) {
        return 1;
    }
    if (0 != test_wrong_key()) {
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
