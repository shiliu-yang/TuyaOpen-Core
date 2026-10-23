/**
 * @file demo_vcam_selftest.c
 * @brief demo_vcam standalone selftest: after collecting 80 frames, verify the
 *        index statistics, frame lengths, timestamps, and I-frame distribution.
 *
 * The build produces a standalone executable vcam_selftest (linked against the
 * same library group as p2p_demo). Usage: ./build/vcam_selftest [video path]
 * (defaults to demo_video.264 next to the executable; CMake copies
 * assets/demo_video.264 to the build directory at configure time).
 *
 * Two tiers of checks:
 *   - **Bundled video (no argument) uses absolute checks**: the AU count, IDR
 *     count, first-frame length, and I-frame indices are pinned to the real
 *     values of demo_video.264 (from ffprobe -count_packets and an NAL
 *     histogram: 540 AUs, 9 IDRs every 60 frames, first AU 54923B). These
 *     assertions necessarily fail if the splitting logic is broken (e.g. SPS/PPS
 *     split into separate frames, or a P frame mistaken for an I frame).
 *   - A custom file (with an argument) only gets loose lower-bound checks
 *     (count/increasing/non-empty) because the real values are unknown.
 *
 * Exit code: 0 = PASSED, 1 = check or open failure.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "demo_vcam.h"
#include "core_api.h"
#include "core_api_ops.h"

/* Frames collected by the selftest: about 5.3 seconds at 15fps; the bundled video has GOP=60, so 80 frames should contain 2 IDRs (indices 0/60) */
#define VCAM_SELFTEST_FRAMES 80
#define VCAM_SELFTEST_FPS    15
#define VCAM_SELFTEST_WAIT_MS 10000 /* upper bound for frame collection; timeout is a failure */

/* Absolute ground truth for the bundled demo_video.264 (ffprobe/NAL histogram; update if the video changes).
 * The absolute tier is selected when "file size == bundled video size" (see main); any other video falls back to the loose tier */
#define VCAM_SELFTEST_EXPECT_AU       540u      /* total AU count */
#define VCAM_SELFTEST_EXPECT_IDR      9u        /* IDR AU count (GOP=60) */
#define VCAM_SELFTEST_EXPECT_FIRST_AU 54923u    /* first AU (SPS+PPS+SEI+IDR) byte count */
#define VCAM_SELFTEST_EXPECT_MAX_AU   (64u * 1024u) /* per-AU limit (within the 256KB P2P frame slot) */
#define VCAM_SELFTEST_BUNDLED_SIZE    4465130u      /* bundled video byte count (basis for the absolute tier) */

/* Video source path: falls back to "demo_video.264" when not injected (same as tuya_ipc_demo.c) */
#ifndef P2P_DEMO_H264_FILE
#define P2P_DEMO_H264_FILE "demo_video.264"
#endif

/* The callback runs on the vcam thread: the count is volatile; the frame
 * details are written by the callback thread and read by the main thread after
 * close (which waits for the thread to exit) */
static volatile uint32_t s_cb_count = 0;
static uint32_t          s_frame_len[VCAM_SELFTEST_FRAMES];
static uint64_t          s_frame_ts[VCAM_SELFTEST_FRAMES];
static BOOL_T            s_frame_is_i[VCAM_SELFTEST_FRAMES];
static uint32_t          s_frame_i_num = 0;

/**
 * @brief Frame callback: record length/timestamp/I-frame flag of the first 80 frames
 */
static void __selftest_frame_cb(const DEMO_VCAM_FRAME_T *frame)
{
    uint32_t n = s_cb_count;

    if (n < VCAM_SELFTEST_FRAMES) {
        s_frame_len[n]  = frame->len;
        s_frame_ts[n]   = frame->ts_ms;
        s_frame_is_i[n] = frame->is_i_frame;
        if (frame->is_i_frame) {
            s_frame_i_num++;
        }
    }
    s_cb_count = n + 1;
}

/**
 * @brief Log output callback (same as iot_demo: goes to stdout through the port port_log_output)
 */
static void __selftest_log_output_cb(const char *str)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief Default video path: prefer the executable directory (demo_video.264 in
 *        the build directory, copied from assets/ at configure time), then fall
 *        back to the current directory
 *
 * This also lets `./build/vcam_selftest` run directly (from the example directory).
 *
 * @return video path (static buffer, no need to free)
 */
static const char *__selftest_default_path(void)
{
    static char exe_dir_path[1024];
    char        link[512];
    ssize_t     n   = 0;
    char       *slash = NULL;

    /* P2P_DEMO_H264_FILE can be overridden by Kconfig/-D (CMake injects it into this target) */
    if (0 == access(P2P_DEMO_H264_FILE, R_OK)) {
        return P2P_DEMO_H264_FILE;
    }
    n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n <= 0) {
        return P2P_DEMO_H264_FILE;
    }
    link[n] = '\0';
    slash   = strrchr(link, '/');
    if (NULL == slash) {
        return P2P_DEMO_H264_FILE;
    }
    slash[1] = '\0';
    snprintf(exe_dir_path, sizeof(exe_dir_path), "%s%s", link, P2P_DEMO_H264_FILE);
    if (0 == access(exe_dir_path, R_OK)) {
        return exe_dir_path;
    }

    return P2P_DEMO_H264_FILE;
}

/**
 * @brief File size (used to decide whether the bundled video is under test; returns 0 when unavailable)
 */
static uint32_t __selftest_file_size(const char *path)
{
    struct stat st;

    if (path == NULL || 0 != stat(path, &st)) {
        return 0;
    }
    return (uint32_t)st.st_size;
}

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : __selftest_default_path();
    /* The bundled video uses the absolute tier (decided by file size, including
     * a path overridden via -D); a custom file has unknown ground truth, so only
     * loose lower bounds are checked */
    BOOL_T      bundled   = (__selftest_file_size(path) == VCAM_SELFTEST_BUNDLED_SIZE) ? TRUE : FALSE;
    OPERATE_RET rt        = OPRT_OK;
    uint32_t    i         = 0;
    uint32_t    waited_ms = 0;
    uint32_t    fail      = 0;
    uint32_t    au_num = 0, idr_num = 0, max_au = 0;
    BOOL_T      first_is_i = FALSE;

    core_log_init(CORE_LOG_LEVEL_DEBUG, 1024, __selftest_log_output_cb);

    rt = demo_vcam_open(path, VCAM_SELFTEST_FPS, __selftest_frame_cb);
    if (OPRT_OK != rt) {
        printf("VCAM SELFTEST FAILED: open '%s' ret %d\n", path, rt);
        return 1;
    }
    printf("vcam opened (%s), mode=%s, waiting %u frames...\n", path, bundled ? "absolute" : "loose",
           VCAM_SELFTEST_FRAMES);

    /* Check 0: index statistics (AU count / IDR count / max AU / whether the first frame is an IDR) */
    if (OPRT_OK != demo_vcam_get_stats(&au_num, &idr_num, &max_au, &first_is_i)) {
        printf("VCAM SELFTEST FAILED: get_stats failed\n");
        demo_vcam_close();
        return 1;
    }
    printf("[check] index: %u AU, %u IDR, max AU %u B, first %s IDR\n", au_num, idr_num, max_au,
           first_is_i ? "is" : "not");
    if (bundled) {
        if (au_num != VCAM_SELFTEST_EXPECT_AU) {
            printf("VCAM SELFTEST FAILED: AU count %u != %u (AU splitting logic broken?)\n", au_num,
                   (uint32_t)VCAM_SELFTEST_EXPECT_AU);
            fail++;
        }
        if (idr_num != VCAM_SELFTEST_EXPECT_IDR) {
            printf("VCAM SELFTEST FAILED: IDR count %u != %u (IDR detection broken?)\n", idr_num,
                   (uint32_t)VCAM_SELFTEST_EXPECT_IDR);
            fail++;
        }
        if (max_au > VCAM_SELFTEST_EXPECT_MAX_AU) {
            printf("VCAM SELFTEST FAILED: max AU %u > %u\n", max_au, (uint32_t)VCAM_SELFTEST_EXPECT_MAX_AU);
            fail++;
        }
        if (!first_is_i) {
            printf("VCAM SELFTEST FAILED: first AU is not IDR\n");
            fail++;
        }
    } else if (au_num == 0 || idr_num == 0) {
        printf("VCAM SELFTEST FAILED: empty index (AU=%u IDR=%u)\n", au_num, idr_num);
        fail++;
    }

    /* The main thread polls until the callback has collected enough frames (the callback runs on the vcam thread) */
    while (s_cb_count < VCAM_SELFTEST_FRAMES && waited_ms < VCAM_SELFTEST_WAIT_MS) {
        core_system_sleep(5);
        waited_ms += 5;
    }
    if (s_cb_count < VCAM_SELFTEST_FRAMES) {
        printf("VCAM SELFTEST FAILED: only %u frames in %ums\n", s_cb_count, waited_ms);
        demo_vcam_close();
        return 1;
    }

    demo_vcam_close();

    /* Check 1: callback frame count */
    printf("[check] callback frames: %u (expect >= %u)\n", s_cb_count, VCAM_SELFTEST_FRAMES);
    /* Check 2: timestamps start at 0 and increase strictly (in steps of 1000/fps) */
    if (0 != s_frame_ts[0]) {
        printf("VCAM SELFTEST FAILED: first ts_ms should start at 0\n");
        fail++;
    }
    for (i = 1; i < VCAM_SELFTEST_FRAMES; i++) {
        if (s_frame_ts[i] <= s_frame_ts[i - 1]) {
            printf("VCAM SELFTEST FAILED: ts not increasing at %u (%llu <= %llu)\n", i,
                   (unsigned long long)s_frame_ts[i], (unsigned long long)s_frame_ts[i - 1]);
            fail++;
            break;
        }
    }
    printf("[check] ts_ms: first %llu, last %llu, step %llu\n", (unsigned long long)s_frame_ts[0],
           (unsigned long long)s_frame_ts[VCAM_SELFTEST_FRAMES - 1],
           (unsigned long long)(s_frame_ts[1] - s_frame_ts[0]));
    /* Check 3: every frame len > 0 (also collects min/max frame length) */
    {
        uint32_t len_min = s_frame_len[0];
        uint32_t len_max = s_frame_len[0];
        for (i = 0; i < VCAM_SELFTEST_FRAMES; i++) {
            if (0 == s_frame_len[i]) {
                printf("VCAM SELFTEST FAILED: frame %u len == 0\n", i);
                fail++;
                break;
            }
            if (s_frame_len[i] < len_min) {
                len_min = s_frame_len[i];
            }
            if (s_frame_len[i] > len_max) {
                len_max = s_frame_len[i];
            }
        }
        printf("[check] frame len: min %u, max %u\n", len_min, len_max);
        if (bundled && s_frame_len[0] != VCAM_SELFTEST_EXPECT_FIRST_AU) {
            printf("VCAM SELFTEST FAILED: first emitted frame len %u != %u (AU boundary broken?)\n", s_frame_len[0],
                   (uint32_t)VCAM_SELFTEST_EXPECT_FIRST_AU);
            fail++;
        }
    }
    /* Check 4: I-frame distribution. For the bundled video GOP=60 -> the first
     * 80 frames must contain IDRs exactly at indices 0 and 60 (a P frame marked
     * as I, or a missed IDR, both fail here); a custom file only requires at
     * least one */
    printf("[check] I-frame in %u frames: %u\n", VCAM_SELFTEST_FRAMES, s_frame_i_num);
    if (bundled) {
        if (!s_frame_is_i[0] || s_frame_i_num != 2) {
            printf("VCAM SELFTEST FAILED: expect exactly 2 IDR (idx 0/60), got %u\n", s_frame_i_num);
            fail++;
        } else {
            for (i = 1; i < VCAM_SELFTEST_FRAMES; i++) {
                if (s_frame_is_i[i] && i != 60) {
                    printf("VCAM SELFTEST FAILED: unexpected IDR at frame %u (P frame misclassified?)\n", i);
                    fail++;
                    break;
                }
            }
        }
    } else if (s_frame_i_num < 1) {
        printf("VCAM SELFTEST FAILED: no IDR in %u frames\n", VCAM_SELFTEST_FRAMES);
        fail++;
    }

    if (fail) {
        printf("VCAM SELFTEST FAILED: %u check(s) failed\n", fail);
        return 1;
    }

    printf("VCAM SELFTEST PASSED\n");
    return 0;
}
