/**
 * @file agent_http_selftest.c
 * @brief http_manager URL parsing boundary selftest: out-of-range ports must be rejected (no network needed)
 *
 * The criterion only looks at the return code of the send stage (a parser-level
 * rejection returns OPRT_INVALID_PARM); the connection result depends on the
 * environment (whether anything listens locally), so the test only asserts
 * "rejected/not rejected" and never asserts connectivity.
 *
 * Criterion strength boundary: the third group ("in-range ports are not
 * rejected") uses `!= OPRT_INVALID_PARM`, which only proves the parser did not
 * reject the value — it cannot prove the port value was parsed correctly,
 * because a failed connection also returns a value other than INVALID_PARM.
 * That group therefore guards against false rejection but does not catch a
 * wrong port value. Covering parse-value correctness would require an
 * observable assertion at the parser layer (e.g. exposing the parsed result to
 * tests).
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "core_api.h"
#include "http_manager.h"

#include <stdio.h>
#include <string.h>

static int s_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                                                     \
            s_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

/* Send a request to the url and return the send-stage result (OPRT_INVALID_PARM when the parser rejects it) */
static OPERATE_RET __send_probe(const char *url)
{
    S_HTTP_MANAGER *mgr = get_http_manager_instance();
    SESSION_ID      s   = mgr->create_http_session(url, FALSE);
    if (!s) {
        return OPRT_INVALID_PARM; /* session creation failed, also treated as rejected */
    }

    http_req_t req;
    memset(&req, 0, sizeof(req));
    req.type     = HTTP_GET;
    req.resource = url;
    req.version  = HTTP_VER_1_1;

    OPERATE_RET rt = mgr->send_http_request(s, &req, 0);
    mgr->destory_http_session(s);

    return rt;
}

int main(void)
{
    core_time_service_init();
    core_log_init(CORE_LOG_LEVEL_WARN, 1024, NULL);

    /* 1) Out-of-range ports (including uint16 wraparound samples) must be rejected by the parser */
    CHECK(__send_probe("http://127.0.0.1:73616/x") == OPRT_INVALID_PARM, "port 73616 (wraps to 8080) rejected");
    CHECK(__send_probe("http://127.0.0.1:65536/x") == OPRT_INVALID_PARM, "port 65536 (wraps to 0) rejected");
    CHECK(__send_probe("http://127.0.0.1:99999/x") == OPRT_INVALID_PARM, "port 99999 rejected");
    CHECK(__send_probe("http://127.0.0.1:4294967296/x") == OPRT_INVALID_PARM, "port 4294967296 rejected");

    /* 2) An explicit :0 is invalid (port 0 is reserved): separated from "no port uses the default" and "wraps to 0" */
    CHECK(__send_probe("http://127.0.0.1:0/x") == OPRT_INVALID_PARM, "explicit port 0 rejected");

    /* 3) In-range ports must not be rejected by the parser (connection success depends on the environment, not asserted) */
    CHECK(__send_probe("http://127.0.0.1:65535/x") != OPRT_INVALID_PARM, "port 65535 not rejected by parser");
    CHECK(__send_probe("http://127.0.0.1:8080/x") != OPRT_INVALID_PARM, "port 8080 not rejected by parser");
    CHECK(__send_probe("http://127.0.0.1/x") != OPRT_INVALID_PARM, "no-port (default) not rejected by parser");

    /* 4) An empty host is still rejected (regression) */
    CHECK(__send_probe("http:///x") == OPRT_INVALID_PARM, "empty host rejected");

    if (s_fail) {
        printf("AGENT HTTP SELFTEST FAILED: %d check(s)\n", s_fail);
        return 1;
    }

    printf("AGENT HTTP SELFTEST PASSED\n");
    return 0;
}
