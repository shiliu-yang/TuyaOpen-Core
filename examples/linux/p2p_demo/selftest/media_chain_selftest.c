/**
 * @file media_chain_selftest.c
 * @brief Standalone media chain selftest (no cloud, no App): vcam -> validation/
 *        queue/ring buffer -> P2P frame-fetch callback.
 *
 * The build produces a standalone executable media_chain_selftest (linked
 * against the same library group as p2p_demo). Usage: run
 * ./media_chain_selftest from the build directory (the sample path is resolved
 * by the demo side: demo_video.264 under cwd or next to the executable).
 *
 * Flow (all through the demo's public contract functions):
 *   1. core runtime initialization (time service + log);
 *   2. tuya_ipc_demo_start(): create the frame pool / ring buffer; with no P2P
 *      session tuya_ipc_init_trans_av_info returns an error gracefully, and the
 *      logged error is expected;
 *   3. demo_on_live_video_start_callback(): open vcam, start pushing;
 *   4. loop demo_on_get_video_frame_callback() to collect 60 valid frames (or
 *      time out after 10s); verify: frame size in (0, 300KB], both
 *      eVideoIFrame/eVideoPBFrame occurred, and pts is non-decreasing;
 *   5. wait for an I frame to lock phase, request one with
 *      demo_on_request_i_frame_callback() and verify the next I frame arrives
 *      within 8 frames;
 *   6. demo_on_live_video_stop_callback() + tuya_ipc_demo_end(), print
 *      MEDIA CHAIN SELFTEST PASSED and return 0; any failed check returns 1.
 *
 * Exit code: 0 = PASSED, 1 = check failure / frame fetch timeout.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>

#include "core_api.h"
#include "core_api_ops.h"
#include "tuya_ipc_demo.h"

/* Frame collection target: about 4 seconds at 15fps; upper bound 10s */
#define SELFTEST_FRAMES         60
#define SELFTEST_WAIT_MS        10000
/* Upper bound for 5a phase lock (waiting for an I frame): must cover the worst
 * case of "waiting naturally for the next IDR". The video source is GOP=60@15fps
 * (one IDR about every 4 seconds); 6 seconds leaves margin -- do not rely on the
 * coincidental relation between the 60 frames collected in step 4 and GOP(60). */
#define SELFTEST_IFRAME_LOCK_MS 6000
/* Upper bound after 5b request I frame: when the request is honored the next I
 * frame is only 1~2 frames away (~130ms), so 2 seconds is plenty; if the request
 * is silently ignored, the frame-count assertion (>8 frames) fails before this
 * timeout. */
#define SELFTEST_IFRAME_WAIT_MS 2000
/* Maximum number of frames allowed between the request and the next I frame:
 * normally (vcam jumps to the next IDR AU) it is a few frames; when the request
 * is silently ignored, GOP=60 requires waiting for ~59 P frames -- the assertion
 * uses frame count rather than duration to avoid a false pass when the request
 * happens to land at the end of a GOP */
#define SELFTEST_IFRAME_MAX_FRAMES 8
/* Frame fetch buffer: the p2p_init media_frame buffer is 300KB; the selftest buffer matches that limit */
#define SELFTEST_FRAME_CAP      (300 * 1024)

/* The frame-fetch callback copies the frame into the buffer pointed to by data; 300KB cannot live on the stack */
static uint8_t s_frame_buf[SELFTEST_FRAME_CAP];

/**
 * @brief Log output callback (same as iot_demo/vcam selftest: goes to stdout through the port port_log_output)
 */
static void __selftest_log_output_cb(const char *str)
{
    CORE_API_OPS_T *ops = core_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

int main(void)
{
    MEDIA_FRAME frame;
    uint32_t    got       = 0;
    uint32_t    waited_ms = 0;
    uint32_t    i_num     = 0;
    uint32_t    p_num     = 0;
    uint32_t    len_min   = 0xFFFFFFFFu;
    uint32_t    len_max   = 0;
    uint64_t    pts_first = 0;
    uint64_t    pts_last  = 0;
    uint32_t    fail      = 0;
    int         rt        = 0;

    core_time_service_init();
    core_log_init(CORE_LOG_LEVEL_DEBUG, 1024, __selftest_log_output_cb);

    printf("media chain selftest: start\n");

    /* 2) Media chain initialization (no P2P session: the AV info report failure is expected) */
    tuya_ipc_demo_start();

    /* 3) Start live streaming: open vcam and start pushing */
    if (demo_on_live_video_start_callback() != 0) {
        printf("MEDIA CHAIN SELFTEST FAILED: live video start (vcam open) failed\n");
        tuya_ipc_demo_end();
        return 1;
    }

    memset(&frame, 0, sizeof(frame));
    frame.data = s_frame_buf;

    /* 4) Collect frames and validate each (0 = valid frame; -1 = no frame, the callback already slept 10ms) */
    while (got < SELFTEST_FRAMES && waited_ms < SELFTEST_WAIT_MS) {
        rt = demo_on_get_video_frame_callback(&frame);
        if (rt != 0) {
            waited_ms += 10;
            continue;
        }
        got++;

        if (frame.size == 0 || frame.size > SELFTEST_FRAME_CAP) {
            printf("MEDIA CHAIN SELFTEST FAILED: frame %u size %u out of (0, %u]\n", got, frame.size,
                   (uint32_t)SELFTEST_FRAME_CAP);
            fail++;
            break;
        }
        if (frame.type == eVideoIFrame) {
            i_num++;
        } else if (frame.type == eVideoPBFrame) {
            p_num++;
        } else {
            printf("MEDIA CHAIN SELFTEST FAILED: frame %u unexpected type %d\n", got, (int)frame.type);
            fail++;
            break;
        }
        if (frame.size < len_min) {
            len_min = frame.size;
        }
        if (frame.size > len_max) {
            len_max = frame.size;
        }
        if (got == 1) {
            pts_first = frame.pts;
        } else if (frame.pts < pts_last) {
            printf("MEDIA CHAIN SELFTEST FAILED: pts not monotonic at frame %u (%llu < %llu)\n", got,
                   (unsigned long long)frame.pts, (unsigned long long)pts_last);
            fail++;
            break;
        }
        pts_last = frame.pts;
    }

    if (fail == 0) {
        if (got < SELFTEST_FRAMES) {
            printf("MEDIA CHAIN SELFTEST FAILED: only %u frames in %ums\n", got, waited_ms);
            fail++;
        } else if (i_num == 0 || p_num == 0) {
            printf("MEDIA CHAIN SELFTEST FAILED: frame types incomplete (I=%u, P=%u)\n", i_num, p_num);
            fail++;
        } else {
            printf("[check] frames: %u (I=%u, P=%u) in %ums\n", got, i_num, p_num, waited_ms);
            printf("[check] frame size: min %u, max %u (cap %u)\n", len_min, len_max, (uint32_t)SELFTEST_FRAME_CAP);
            printf("[check] pts: %llu -> %llu (monotonic non-decreasing)\n", (unsigned long long)pts_first,
                   (unsigned long long)pts_last);
        }
    }

    /* 5) Request an I frame: first wait at a known phase (just after receiving
     *    an I frame), then send the request and count the frames received before
     *    the next I frame. vcam semantics: the next tick after a request jumps
     *    directly to the next IDR AU (the P frames in between are skipped) --
     *    normally only a few frames; if the request is silently ignored,
     *    GOP=60@15fps means ~59 P frames (~4s) until the next natural I frame. */
    if (fail == 0) {
        uint32_t iframe_waited = 0;
        uint32_t frames_after_req = 0;
        BOOL_T   got_i = FALSE;

        /* 5a) Wait for one I frame to lock the phase (budget see SELFTEST_IFRAME_LOCK_MS) */
        while (iframe_waited < SELFTEST_IFRAME_LOCK_MS) {
            rt = demo_on_get_video_frame_callback(&frame);
            if (rt != 0) {
                iframe_waited += 10;
                continue;
            }
            if (frame.type == eVideoIFrame) {
                break;
            }
        }
        if (iframe_waited >= SELFTEST_IFRAME_LOCK_MS) {
            printf("MEDIA CHAIN SELFTEST FAILED: no i-frame to lock phase (waited %ums)\n", iframe_waited);
            fail++;
        } else if (demo_on_request_i_frame_callback() != 0) {
            printf("MEDIA CHAIN SELFTEST FAILED: request i-frame rejected\n");
            fail++;
        } else {
            /* 5b) Count the frames received between the request and the next I frame */
            iframe_waited = 0;
            while (iframe_waited < SELFTEST_IFRAME_WAIT_MS) {
                rt = demo_on_get_video_frame_callback(&frame);
                if (rt != 0) {
                    iframe_waited += 10;
                    continue;
                }
                if (frame.type == eVideoIFrame) {
                    got_i = TRUE;
                    break;
                }
                frames_after_req++;
            }
            printf("[check] i-frame after request: %s (%u frame(s) in between, %ums)\n", got_i ? "yes" : "no",
                   frames_after_req, iframe_waited);
            if (!got_i) {
                printf("MEDIA CHAIN SELFTEST FAILED: no i-frame within %ums after request\n",
                       (uint32_t)SELFTEST_IFRAME_WAIT_MS);
                fail++;
            } else if (frames_after_req > SELFTEST_IFRAME_MAX_FRAMES) {
                printf("MEDIA CHAIN SELFTEST FAILED: %u frames before i-frame (> %u) — request likely ignored\n",
                       frames_after_req, (uint32_t)SELFTEST_IFRAME_MAX_FRAMES);
                fail++;
            }
        }
    }

    /* 6) Cleanup (stopping the stream closes vcam; end frees the frame pool) */
    demo_on_live_video_stop_callback();
    tuya_ipc_demo_end();

    if (fail) {
        printf("MEDIA CHAIN SELFTEST FAILED: %u check(s) failed\n", fail);
        return 1;
    }

    printf("MEDIA CHAIN SELFTEST PASSED\n");
    return 0;
}
