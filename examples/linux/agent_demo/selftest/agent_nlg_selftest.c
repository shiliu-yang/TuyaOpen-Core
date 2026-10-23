/**
 * @file agent_nlg_selftest.c
 * @brief agent_demo downlink consumption selftest: STOP with non-empty final content must be printed
 *
 * Semantics of the text stream events (ai_skill.c): the final segment (eof=1)
 * of a multi-segment NLG stream is carried by AI_USER_EVT_TEXT_STREAM_STOP as
 * that packet's content (may be non-empty); in the single-frame case STOP is an
 * explicit empty packet {NULL,0,0}. A consumer that does not read event->data
 * in the STOP branch loses the last piece of text.
 *
 * The selftest builds events through the public API (ai_user_event_notify,
 * synchronous) and captures the stdout output of the downlink callback (in
 * agent_audio.c) for assertions — independent of the cloud's packetization and
 * stably reproducible. It also checks that ASR_OK and ASR_EMPTY post the
 * uplink verification semaphore and that no event makes the wait time out.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "core_api.h"
#include "ai_user_event.h"
#include "agent_audio.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

/* Capture stdout output during fn() into out (returns the byte count) */
static int capture_stdout(void (*fn)(void *), void *arg, char *out, size_t out_sz)
{
    int     fds[2];
    int     saved;
    ssize_t n;

    if (pipe(fds) != 0) {
        return -1;
    }

    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[1]);

    fn(arg);
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    n = read(fds[0], out, out_sz - 1);
    close(fds[0]);
    if (n < 0) {
        n = 0;
    }
    out[n] = '\0';

    return (int)n;
}

static void __emit_stop_with_text(void *arg)
{
    ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, arg);
}

static void __emit_stop_empty(void *arg)
{
    AI_NOTIFY_TEXT_T empty = {.data = NULL, .datalen = 0, .timeindex = 0};

    (void)arg;
    ai_user_event_notify(AI_USER_EVT_TEXT_STREAM_STOP, &empty);
}

static void __emit_asr_ok(void *arg)
{
    AI_NOTIFY_TEXT_T t = {.data = "recognized text", .datalen = (uint16_t)strlen("recognized text"), .timeindex = 0};

    (void)arg;
    ai_user_event_notify(AI_USER_EVT_ASR_OK, &t);
}

static void __emit_asr_empty(void *arg)
{
    (void)arg;
    ai_user_event_notify(AI_USER_EVT_ASR_EMPTY, NULL);
}

int main(void)
{
    char out[512];

    core_time_service_init();
    core_log_init(CORE_LOG_LEVEL_WARN, 1024, NULL); /* lower log noise to avoid interfering with capture */
    agent_downlink_init();

    /* 1) STOP with a non-empty final segment (multi-segment stream) -> the content must appear in the output */
    AI_NOTIFY_TEXT_T tail = {.data = "final segment text", .datalen = (uint16_t)strlen("final segment text"), .timeindex = 0};
    capture_stdout(__emit_stop_with_text, &tail, out, sizeof(out));
    CHECK(strstr(out, "final segment text") != NULL, "STOP with non-empty content is printed");

    /* 2) STOP with an empty packet (single-frame / empty final segment) -> nothing should be printed */
    capture_stdout(__emit_stop_empty, NULL, out, sizeof(out));
    CHECK(strstr(out, "final segment text") == NULL, "empty STOP prints no content");

    /* 3) Uplink verification semaphore: both ASR_OK and ASR_EMPTY must post (an
     *    empty recognition means the link works but has no content, not an
     *    upload failure); without an ASR event wait must time out (otherwise
     *    "upload done" would be a false positive). This decouples the downlink
     *    callback -> uplink verification coupling from the cloud connection so
     *    it can be judged offline. */
    CHECK(agent_audio_uplink_verify_init() == OPRT_OK, "uplink verify init");
    capture_stdout(__emit_asr_ok, NULL, out, sizeof(out));
    CHECK(agent_audio_uplink_verify_wait(2000) == OPRT_OK, "ASR_OK posts uplink verify");

    CHECK(agent_audio_uplink_verify_init() == OPRT_OK, "uplink verify re-init");
    capture_stdout(__emit_asr_empty, NULL, out, sizeof(out));
    CHECK(agent_audio_uplink_verify_wait(2000) == OPRT_OK, "ASR_EMPTY posts uplink verify");

    /* Reverse case: no ASR event -> the wait must fail (use a short timeout, do not drag 15s into the selftest) */
    CHECK(agent_audio_uplink_verify_init() == OPRT_OK, "uplink verify init (no-asr case)");
    CHECK(agent_audio_uplink_verify_wait(300) != OPRT_OK, "no ASR event -> wait times out");

    if (s_fail) {
        printf("AGENT NLG SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT NLG SELFTEST PASSED\n");
    return 0;
}
