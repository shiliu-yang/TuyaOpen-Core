/**
 * @file agent_chain_selftest.c
 * @brief agent_demo offline selftest: sample loading -> per-frame read -> boundaries
 *
 * Does not connect to the cloud or need credentials; it verifies the uplink
 * audio source read contract and the frame splitting of the bundled sample.
 * The upload orchestration agent_audio_upload_once depends on the AI client
 * context and is covered by online runtime testing.
 *
 * Criteria (bundled sample assets/voice_16k.pcm: 16kHz/16bit/mono, 2.592s):
 *   total bytes = 82944; split into 80ms frames (2560B) = 33 frames
 *   (32 full frames + a 1024B last frame). 
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "play_local_in.h"

#include <stdio.h>

#define FRAME_BYTES 2560         /* 80ms @16kHz/16bit/mono */
#define EXPECTED_TOTAL_BYTES 82944 /* 2.592s × 16000 × 2B */
#define EXPECTED_FRAMES 33         /* 32×2560 + 1024 */
#define EXPECTED_LAST_FRAME 1024

/* Sample path: same source as the app (tuya_main.c's AGENT_AUDIO_SRC_PATH).
 * CMake injects AGENT_DEMO_AUDIO_FILE into this target (see CMakeLists.txt); an
 * absolute path configured there is the sample under test, and when empty the
 * bundled sample in the build directory is used (run from build/). */
#ifndef AGENT_DEMO_AUDIO_FILE
#define AGENT_DEMO_AUDIO_FILE ""
#endif
#define SELFTEST_AUDIO_SRC ((AGENT_DEMO_AUDIO_FILE)[0] ? (AGENT_DEMO_AUDIO_FILE) : "voice_16k.pcm")

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(void)
{
    uint8_t  buf[FRAME_BYTES];
    uint32_t total  = 0;
    uint32_t frames = 0;
    uint32_t last   = 0;
    INT_T    n;

    /* 0) Sample pre-check: copied to the build directory by CMake, run from
     *    build/; running elsewhere gives a clear hint (the p2p_demo "run from
     *    build/" convention) so later assertions do not fail en masse and hide
     *    the root cause. */
    if (play_local_in_open(SELFTEST_AUDIO_SRC) != OPRT_OK) {
        printf("FAIL: audio sample '%s' not found: run from build/ \n", SELFTEST_AUDIO_SRC);
        return 1;
    }
    play_local_in_close();

    /* 1) Open the bundled sample */
    CHECK(play_local_in_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "open bundled pcm");

    /* 2) Read in 80ms frames, counting frames/total bytes/last frame length */
    for (;;) {
        n = play_local_in_read(buf, FRAME_BYTES);
        CHECK(n >= 0, "read returns no error");
        if (n <= 0) {
            break;
        }
        CHECK(n <= FRAME_BYTES, "frame not larger than 80ms");
        total += (uint32_t)n;
        frames++;
        last = (uint32_t)n;
    }

    CHECK(total == EXPECTED_TOTAL_BYTES, "total bytes");
    CHECK(frames == EXPECTED_FRAMES, "frame count");
    CHECK(last == EXPECTED_LAST_FRAME, "last frame bytes");

    /* 3) Reading after EOF still returns 0 */
    CHECK(play_local_in_read(buf, FRAME_BYTES) == 0, "eof stays eof");

    /* 4) Can reopen after close */
    CHECK(play_local_in_close() == OPRT_OK, "close");
    CHECK(play_local_in_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "reopen");
    CHECK(play_local_in_close() == OPRT_OK, "close again");

    /* 5) Bad path is rejected and does not affect later use */
    CHECK(play_local_in_open("/nonexistent/agent_demo_selftest.pcm") != OPRT_OK, "bad path rejected");
    CHECK(play_local_in_open(SELFTEST_AUDIO_SRC) == OPRT_OK, "usable after bad path");
    CHECK(play_local_in_close() == OPRT_OK, "final close");

    /* 6) I/O errors must be distinguished from EOF (contract: >0 bytes / 0 EOF / <0 error):
     *    fopen on a directory path succeeds but fread must fail (ferror) — if ferror
     *    were swallowed as 0 (EOF), the caller would treat a disk read error as
     *    "finished" and report success with 0 bytes (a silent false positive). */
    CHECK(play_local_in_open(".") == OPRT_OK, "open dir path (fopen succeeds)");
    CHECK(play_local_in_read(buf, FRAME_BYTES) < 0, "read on dir returns error, not EOF");
    CHECK(play_local_in_close() == OPRT_OK, "close after dir probe");

    if (s_fail) {
        printf("AGENT CHAIN SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT CHAIN SELFTEST PASSED\n");
    return 0;
}
