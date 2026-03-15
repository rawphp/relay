#include "Unity/unity.h"
#include "claude.h"
#include "llm_provider.h"
#include "mocks.h"

/* Stub for proc_set_current_workspace_path — defined in main.c in production.
 * Captures the last value passed so tests can assert on it. */
void proc_set_current_workspace_path(const char *path)
{
    if (path) {
        snprintf(g_mock_proc_last_workspace_path,
                 sizeof(g_mock_proc_last_workspace_path), "%s", path);
    } else {
        g_mock_proc_last_workspace_path[0] = '\0';
    }
}

/* ── Test: Parse valid JSON response ────────────────────────────────── */
static void test_claude_parse_valid_response(void)
{
    const char *json =
        "{\"type\":\"result\","
        "\"session_id\":\"abc-123-def\","
        "\"result\":\"Hello from Claude!\","
        "\"duration_ms\":1234,"
        "\"total_cost_usd\":0.002}";

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response(json, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("abc-123-def", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("Hello from Claude!", resp.result);
    TEST_ASSERT_EQUAL_INT(1234, resp.duration_ms);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);
}

/* ── Test: Parse response with missing session_id ───────────────────── */
static void test_claude_parse_no_session(void)
{
    const char *json =
        "{\"type\":\"result\","
        "\"result\":\"Hello!\"}";

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response(json, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("Hello!", resp.result);
}

/* ── Test: Parse error response ─────────────────────────────────────── */
static void test_claude_parse_error_response(void)
{
    const char *json =
        "{\"type\":\"error\","
        "\"error\":\"Rate limit exceeded\"}";

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response(json, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);
}

/* ── Test: Parse malformed JSON ─────────────────────────────────────── */
static void test_claude_parse_malformed(void)
{
    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response("{invalid json{{", &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

/* ── Test: Parse NULL input ─────────────────────────────────────────── */
static void test_claude_parse_null(void)
{
    claude_response_t resp;
    int rc = claude_parse_response(NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

/* ── Test: Send message via mock proc ───────────────────────────────── */
static void test_claude_send_message(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"new-sess-456\","
        "\"result\":\"I can help with that.\","
        "\"duration_ms\":500}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("new-sess-456", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("I can help with that.", resp.result);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Send with resume session ─────────────────────────────────── */
static void test_claude_send_with_resume(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"existing-sess\","
        "\"result\":\"Continuing conversation.\","
        "\"duration_ms\":300}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 120\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "continue", "existing-sess", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("Continuing conversation.", resp.result);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Process spawn failure ────────────────────────────────────── */
static void test_claude_send_proc_failure(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, NULL, &resp);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Update config changes timeout ───────────────────────────── */
static void test_claude_update_config_changes_timeout(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* Send with original timeout (60) */
    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));
    claude_send(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(60, g_mock_proc_last_timeout);

    /* Update config with new timeout */
    const char *new_cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 300\n"
        "workspace_path = /home/user/workspace\n";

    config_t *new_cfg = config_load_string(new_cfg_text);
    claude_update_config(cl, new_cfg);

    /* Send again — should use new timeout (300) */
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    memset(&resp, 0, sizeof(resp));
    claude_send(cl, "test2", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(300, g_mock_proc_last_timeout);

    claude_free(cl);
    config_free(cfg);
    config_free(new_cfg);
}

/* ── Test: Update config with NULL args is safe ────────────────────── */
static void test_claude_update_config_null_safe(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* NULL config should be safe no-op */
    claude_update_config(cl, NULL);

    /* NULL claude should be safe no-op */
    claude_update_config(NULL, cfg);

    /* Original timeout should be preserved */
    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));
    claude_send(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(60, g_mock_proc_last_timeout);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Retry on transient error (success on 2nd attempt) ─────────*/
static void test_claude_retry_success_on_second_attempt(void)
{
    mock_proc_reset();

    /* First call fails with timeout */
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* Second call should succeed */
    mock_proc_set_output_after_n_calls(1,
        "{\"type\":\"result\","
        "\"session_id\":\"retry-sess\","
        "\"result\":\"Success after retry\","
        "\"duration_ms\":200}");

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_with_retry(cl, "test retry", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("Success after retry", resp.result);
    TEST_ASSERT_EQUAL_INT(2, mock_proc_call_count()); /* 1 failure + 1 success */

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Retry exhausted after max attempts ────────────────────────*/
static void test_claude_retry_exhausted(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Should fail after 3 retries */
    int rc = claude_send_with_retry(cl, "test fail", NULL, NULL, &resp);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(3, mock_proc_call_count());

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: No retry on parse error (permanent failure) ────────────── */
static void test_claude_no_retry_on_parse_error(void)
{
    mock_proc_reset();
    /* Set non-streaming output to malformed JSON.
     * claude_send uses spawn (non-streaming), so g_mock_proc_output is what
     * gets written to the output buffer. Malformed JSON → RELAY_ERR_PARSE.
     * This must NOT be retried (permanent parse failure, not transient crash). */
    mock_proc_set_output("{invalid json");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Should fail immediately without retry (parse errors are permanent) */
    int rc = claude_send_with_retry(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
    TEST_ASSERT_EQUAL_INT(1, mock_proc_call_count()); /* No retry on parse error */

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Exponential backoff between retries ────────────────────── */
static void test_claude_retry_backoff(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 100\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Record start time, send with retry, check backoff delays */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    claude_send_with_retry(cl, "test", NULL, NULL, &resp);

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* 3 attempts with 100ms, 200ms backoff = ~300ms minimum */
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    TEST_ASSERT_GREATER_THAN(250, elapsed_ms); /* Allow some margin */

    claude_free(cl);
    config_free(cfg);
}

/* ── Tests: claude_parse_response with stream-json error format ──────── */

static void test_claude_parse_stream_json_error_subtype(void)
{
    /* stream-json emits type:"result",subtype:"error" for errors */
    const char *json =
        "{\"type\":\"result\","
        "\"subtype\":\"error\","
        "\"result\":\"API rate limit exceeded\","
        "\"session_id\":\"\","
        "\"duration_ms\":100}";

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response(json, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);
    TEST_ASSERT_EQUAL_STRING("API rate limit exceeded", resp.result);
}

static void test_claude_parse_stream_json_success_subtype(void)
{
    /* stream-json success: type:"result",subtype:"success" */
    const char *json =
        "{\"type\":\"result\","
        "\"subtype\":\"success\","
        "\"session_id\":\"stream-sess-789\","
        "\"result\":\"Streamed response text.\","
        "\"duration_ms\":2500}";

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_parse_response(json, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);
    TEST_ASSERT_EQUAL_STRING("stream-sess-789", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("Streamed response text.", resp.result);
    TEST_ASSERT_EQUAL_INT(2500, resp.duration_ms);
}

/* ── Tests: claude_send_streaming ───────────────────────────────────── */

/* Accumulator for token callback testing */
typedef struct {
    char text[RELAY_MAX_RESPONSE];
    int call_count;
} token_acc_t;

static int token_accumulator(const char *text, size_t len, void *userdata)
{
    token_acc_t *acc = userdata;
    if (acc->text[0] == '\0') {
        snprintf(acc->text, sizeof(acc->text), "%.*s", (int)len, text);
    } else {
        size_t existing = strlen(acc->text);
        if (existing + len < sizeof(acc->text) - 1) {
            memcpy(acc->text + existing, text, len);
            acc->text[existing + len] = '\0';
        }
    }
    acc->call_count++;
    return 0; /* continue */
}

static void test_claude_send_streaming_delivers_tokens(void)
{
    mock_proc_reset();
    memset(g_mock_proc_stream_tokens, 0, sizeof(g_mock_proc_stream_tokens));
    memset(g_mock_proc_stream_result, 0, sizeof(g_mock_proc_stream_result));

    mock_proc_set_stream_output(
        "Hello, world!",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"stream-sess-abc\","
        "\"result\":\"Hello, world!\","
        "\"duration_ms\":1500}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming(cl, "say hello", NULL,
                                   NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* Token callback was called */
    TEST_ASSERT_GREATER_THAN(0, acc.call_count);
    TEST_ASSERT_EQUAL_STRING("Hello, world!", acc.text);
    /* Response populated from result event */
    TEST_ASSERT_EQUAL_STRING("stream-sess-abc", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("Hello, world!", resp.result);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_streaming_fallback_when_no_spawn_streaming(void)
{
    /* When spawn_streaming is NULL, should fall back to blocking claude_send */
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"fallback-sess\","
        "\"result\":\"Fallback response.\","
        "\"duration_ms\":500}");

    /* Create a proc with spawn_streaming = NULL */
    relay_proc_t fallback_proc = {
        .spawn = mock_proc_spawn,
        .spawn_streaming = NULL
    };

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&fallback_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming(cl, "test fallback", NULL,
                                   NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("Fallback response.", resp.result);
    TEST_ASSERT_EQUAL_STRING("fallback-sess", resp.session_id);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_streaming_proc_failure(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming(cl, "test", NULL,
                                   NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
    /* No tokens delivered on failure */
    TEST_ASSERT_EQUAL_INT(0, acc.call_count);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_streaming_uses_resume_session(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output(
        "Continuing...",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"existing-sess\","
        "\"result\":\"Continuing...\","
        "\"duration_ms\":800}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming(cl, "continue", "existing-sess",
                                   NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("existing-sess", resp.session_id);

    /* Verify --resume was in args */
    int found_resume = 0;
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        if (strcmp(g_mock_proc_last_args[i], "--resume") == 0) {
            found_resume = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found_resume);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_streaming_uses_stream_json_format(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output(
        "test",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"s\",\"result\":\"test\",\"duration_ms\":100}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));
    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send_streaming(cl, "test", NULL, NULL, token_accumulator, &acc, &resp);

    /* Verify --output-format stream-json was in args */
    int found_stream_json = 0;
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        if (strcmp(g_mock_proc_last_args[i], "stream-json") == 0) {
            found_stream_json = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found_stream_json);

    /* Verify --include-partial-messages was in args */
    int found_partial = 0;
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        if (strcmp(g_mock_proc_last_args[i], "--include-partial-messages") == 0) {
            found_partial = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found_partial);

    claude_free(cl);
    config_free(cfg);
}

/* ── Tests: claude_send_streaming_with_retry ────────────────────────── */

static void test_claude_streaming_retry_success_on_second_attempt(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;
    mock_proc_set_stream_output_after_n_calls(
        1,
        "Retry worked!",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"retry-stream-sess\","
        "\"result\":\"Retry worked!\","
        "\"duration_ms\":500}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming_with_retry(cl, "test retry", NULL,
                                              NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(2, mock_proc_call_count()); /* failed once, succeeded once */
    TEST_ASSERT_EQUAL_STRING("Retry worked!", acc.text);
    TEST_ASSERT_EQUAL_STRING("retry-stream-sess", resp.session_id);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_streaming_retry_exhausted(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming_with_retry(cl, "test fail", NULL,
                                              NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(3, mock_proc_call_count()); /* exhausted all 3 retries */
    TEST_ASSERT_EQUAL_INT(0, acc.call_count);          /* no tokens delivered */

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_streaming_no_retry_after_tokens_delivered(void)
{
    mock_proc_reset();
    /* Deliver tokens then fail — should not retry because tokens already sent */
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;
    g_mock_proc_stream_tokens_before_fail = 1;
    snprintf(g_mock_proc_stream_tokens, sizeof(g_mock_proc_stream_tokens),
             "partial output...");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming_with_retry(cl, "test no retry", NULL,
                                              NULL, token_accumulator, &acc, &resp);
    /* After Fix 3: tokens delivered → treat as success, no retry, no error */
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);
    TEST_ASSERT_EQUAL_INT(1, mock_proc_call_count()); /* no retry after tokens */
    TEST_ASSERT_GREATER_THAN(0, acc.call_count);       /* tokens were delivered */

    claude_free(cl);
    config_free(cfg);
}

/* ── Tests: retry notification callback ────────────────────────────── */

static int g_retry_notify_attempt = 0;
static int g_retry_notify_max = 0;
static int g_retry_notify_count = 0;

static void test_retry_notify_cb(int attempt, int max_retries, void *userdata)
{
    (void)userdata;
    g_retry_notify_attempt = attempt;
    g_retry_notify_max = max_retries;
    g_retry_notify_count++;
}

static void test_claude_retry_notify_fires_on_zero_output(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);
    claude_set_retry_notify(cl, test_retry_notify_cb, NULL);

    g_retry_notify_count = 0;
    g_retry_notify_attempt = 0;
    g_retry_notify_max = 0;

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send_streaming_with_retry(cl, "test fail", NULL,
                                     NULL, token_accumulator, &acc, &resp);

    /* Callback should have fired for each retry (2 times: before attempt 2 and 3) */
    TEST_ASSERT_EQUAL_INT(2, g_retry_notify_count);
    TEST_ASSERT_EQUAL_INT(3, g_retry_notify_max);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_retry_notify_not_fired_when_tokens_sent(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;
    g_mock_proc_stream_tokens_before_fail = 1;
    snprintf(g_mock_proc_stream_tokens, sizeof(g_mock_proc_stream_tokens),
             "partial output...");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "claude_retry_backoff_ms = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);
    claude_set_retry_notify(cl, test_retry_notify_cb, NULL);

    g_retry_notify_count = 0;

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send_streaming_with_retry(cl, "test no retry", NULL,
                                     NULL, token_accumulator, &acc, &resp);

    /* Tokens were sent → no retry → no notification */
    TEST_ASSERT_EQUAL_INT(0, g_retry_notify_count);

    claude_free(cl);
    config_free(cfg);
}

/* ── Tests: claude_build_system_prompt ──────────────────────────────── */

static void test_system_prompt_contains_agent_name(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "henry"));
}

static void test_system_prompt_contains_user_name(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "john"));
}

static void test_system_prompt_references_soul_and_identity(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "SOUL.md"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "IDENTITY.md"));
}

static void test_system_prompt_references_user_md(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "USER.md"));
}

static void test_system_prompt_references_priorities(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "PRIORITIES.md"));
}

static void test_system_prompt_references_skills(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    TEST_ASSERT_NOT_NULL(strstr(buf, "SKILLS.md"));
}

static void test_system_prompt_contains_timezone(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    /* Prompt must contain "Current time:" with a timezone label */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Current time:"));
}

static void test_system_prompt_fits_in_buffer(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john", "", "");
    /* Must be non-empty and not overflow — snprintf guarantees NUL termination */
    TEST_ASSERT_TRUE(strlen(buf) > 0);
    TEST_ASSERT_TRUE(strlen(buf) < CLAUDE_SYSTEM_PROMPT_MAX);
}

static void test_claude_system_prompt_includes_workspace(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john",
                               "life", "/Users/john/life");
    TEST_ASSERT_NOT_NULL(strstr(buf, "life"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/Users/john/life"));
}

static void test_claude_system_prompt_path_only_fallback(void)
{
    char buf[CLAUDE_SYSTEM_PROMPT_MAX];
    /* workspace_name empty — should still include path without crashing */
    claude_build_system_prompt(buf, sizeof(buf), "henry", "john",
                               "", "/Users/john/code");
    TEST_ASSERT_NOT_NULL(strstr(buf, "/Users/john/code"));
}

/* ── Tests: spawn failure populates resp with error detail ──────────── */

static void test_claude_send_timeout_sets_is_error(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_TIMEOUT, rc);
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_timeout_populates_result_detail(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send(cl, "hello", NULL, NULL, &resp);
    /* result must not be empty — gives operator something to log */
    TEST_ASSERT_TRUE(resp.result[0] != '\0');

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_generic_error_sets_is_error(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);
    TEST_ASSERT_TRUE(resp.result[0] != '\0');

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_send_streaming_timeout_sets_is_error(void)
{
    mock_proc_reset();
    g_mock_proc_stream_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);

    token_acc_t acc;
    memset(&acc, 0, sizeof(acc));
    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming(cl, "hello", NULL,
                                   NULL, token_accumulator, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_TIMEOUT, rc);
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);
    TEST_ASSERT_TRUE(resp.result[0] != '\0');

    claude_free(cl);
    config_free(cfg);
}

/* ── Tests: claude_in_flight — concurrent call counter ──────────────── */

static void test_claude_in_flight_zero_initially(void)
{
    mock_proc_reset();

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* No calls yet — counter must be zero */
    TEST_ASSERT_EQUAL_INT(0, claude_in_flight(cl));

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_in_flight_zero_after_successful_send(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));
    claude_send(cl, "hello", NULL, NULL, &resp);

    /* Counter must return to zero after call completes */
    TEST_ASSERT_EQUAL_INT(0, claude_in_flight(cl));

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_in_flight_zero_after_failed_send(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* Run several failing calls — counter must never leak */
    for (int i = 0; i < 3; i++) {
        claude_response_t resp;
        memset(&resp, 0, sizeof(resp));
        claude_send(cl, "hello", NULL, NULL, &resp);
    }

    TEST_ASSERT_EQUAL_INT(0, claude_in_flight(cl));

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: No retry when process killed by signal ───────────────────── */
static void test_claude_no_retry_on_signal_kill(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_SIGNAL;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Signal kills are not retriable — interrupted by a newer message */
    int rc = claude_send_with_retry(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_SIGNAL, rc);
    TEST_ASSERT_EQUAL_INT(1, mock_proc_call_count()); /* No retry on signal */
    TEST_ASSERT_EQUAL_INT(1, resp.is_error);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: retry_count=0 returns RELAY_ERR, not uninitialized (REQ-089) */
static void test_claude_retry_count_zero_returns_error(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_with_retry(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count()); /* loop never executes */

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_streaming_retry_count_zero_returns_error(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 0\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_streaming_with_retry(cl, "test", NULL, NULL,
                                               NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: claude_send calls proc_set_current_workspace_path ──────────*/
static void test_claude_send_sets_workspace_path(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"s\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, "/Users/john/life", &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* The resolved workspace path must have been registered via TLS */
    TEST_ASSERT_EQUAL_STRING("/Users/john/life", g_mock_proc_last_workspace_path);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: llm_provider_send_workspace threads workspace_path to spawn ──*/
/* Verifies the bug fix: step-3 fallback must pass workspace_path through
 * to claude_send so proc_set_current_workspace_path is called with the
 * caller-supplied path, not the stale cl->workspace ("." by default). */
static void test_llm_provider_send_workspace_threads_path_to_spawn(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"s\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    /* No workspace_path in config → cl->workspace = "." */
    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Pass an explicit workspace_path — must reach proc_set_current_workspace_path */
    int rc = llm_provider_send_workspace(llm, "describe this photo",
                                          NULL, "/home/relay", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("/home/relay", g_mock_proc_last_workspace_path);

    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Test: Default timeout is 600s when not specified in config ──────── */
static void test_claude_default_timeout_is_600(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"s\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    /* No claude_timeout in config — should use default */
    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));
    claude_send(cl, "test", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(600, g_mock_proc_last_timeout);

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Empty result_line is retriable (REQ-048) ────────────────── */
static void test_claude_empty_result_is_retriable(void)
{
    mock_proc_reset();
    /* No stream output set — mock returns RELAY_OK with empty result_line.
     * Before REQ-048 fix: returned RELAY_ERR_PARSE (no retry, count=1).
     * After fix: returns RELAY_ERR (retriable), retried retry_count times. */

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_with_retry(cl, "test", NULL, NULL, &resp);

    /* Should be retried (retriable error), not RELAY_ERR_PARSE */
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
    TEST_ASSERT_EQUAL_INT(3, mock_proc_call_count()); /* retried retry_count times */

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: Generic RELAY_ERR (e.g. DNS failure) is retried ─────────── */
/* DNS resolution failures map to RELAY_ERR via http_get in main.c.
 * This test proves RELAY_ERR is classified as retriable, so DNS
 * failures get automatic retries rather than failing immediately. */
static void test_claude_generic_error_is_retriable(void)
{
    mock_proc_reset();
    /* First call fails with RELAY_ERR (what DNS failures produce) */
    g_mock_proc_status = RELAY_ERR;

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "claude_retry_count = 3\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    /* Second call succeeds */
    mock_proc_set_output_after_n_calls(1,
        "{\"type\":\"result\","
        "\"session_id\":\"dns-retry\","
        "\"result\":\"Recovered after DNS failure\","
        "\"duration_ms\":100}");

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send_with_retry(cl, "test dns retry", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("Recovered after DNS failure", resp.result);
    TEST_ASSERT_EQUAL_INT(2, mock_proc_call_count()); /* 1 failure + 1 success */

    claude_free(cl);
    config_free(cfg);
}

/* ── Test: allowed_tools passed to args ─────────────────────────────── */
static void test_claude_allowed_tools_in_args(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n"
        "allowed_tools = Read,Write,Bash\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = claude_send(cl, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Verify --allowedTools was in args */
    int found_flag = 0;
    int found_value = 0;
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        if (strcmp(g_mock_proc_last_args[i], "--allowedTools") == 0) {
            found_flag = 1;
            if (g_mock_proc_last_args[i + 1] != NULL) {
                found_value = (strcmp(g_mock_proc_last_args[i + 1],
                                      "Read,Write,Bash") == 0);
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(found_flag);
    TEST_ASSERT_TRUE(found_value);

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_no_allowed_tools_when_unset(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\","
        "\"session_id\":\"sess\","
        "\"result\":\"ok\","
        "\"duration_ms\":100}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send(cl, "hello", NULL, NULL, &resp);

    /* Verify --allowedTools is NOT in args */
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0,
            strcmp(g_mock_proc_last_args[i], "--allowedTools"),
            "--allowedTools should not be present when config key is unset");
    }

    claude_free(cl);
    config_free(cfg);
}

static void test_claude_allowed_tools_in_streaming_args(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output("hello",
        "{\"type\":\"result\",\"session_id\":\"s\",\"result\":\"ok\",\"duration_ms\":50}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n"
        "allowed_tools = Read,Grep\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    claude_t *cl = claude_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(cl);

    claude_response_t resp;
    memset(&resp, 0, sizeof(resp));

    claude_send_streaming(cl, "test", NULL, NULL, NULL, NULL, &resp);

    /* Verify --allowedTools in streaming args */
    int found = 0;
    for (int i = 0; g_mock_proc_last_args[i] != NULL; i++) {
        if (strcmp(g_mock_proc_last_args[i], "--allowedTools") == 0) {
            found = 1;
            TEST_ASSERT_EQUAL_STRING("Read,Grep", g_mock_proc_last_args[i + 1]);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);

    claude_free(cl);
    config_free(cfg);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_claude_suite(void)
{
    RUN_TEST(test_claude_parse_valid_response);
    RUN_TEST(test_claude_parse_no_session);
    RUN_TEST(test_claude_parse_error_response);
    RUN_TEST(test_claude_parse_malformed);
    RUN_TEST(test_claude_parse_null);
    RUN_TEST(test_claude_send_message);
    RUN_TEST(test_claude_send_with_resume);
    RUN_TEST(test_claude_send_proc_failure);
    RUN_TEST(test_claude_update_config_changes_timeout);
    RUN_TEST(test_claude_update_config_null_safe);
    RUN_TEST(test_claude_retry_success_on_second_attempt);
    RUN_TEST(test_claude_retry_exhausted);
    RUN_TEST(test_claude_no_retry_on_parse_error);
    RUN_TEST(test_claude_no_retry_on_signal_kill);
    RUN_TEST(test_claude_retry_backoff);
    /* streaming tests */
    RUN_TEST(test_claude_parse_stream_json_error_subtype);
    RUN_TEST(test_claude_parse_stream_json_success_subtype);
    RUN_TEST(test_claude_send_streaming_delivers_tokens);
    RUN_TEST(test_claude_send_streaming_fallback_when_no_spawn_streaming);
    RUN_TEST(test_claude_send_streaming_proc_failure);
    RUN_TEST(test_claude_send_streaming_uses_resume_session);
    RUN_TEST(test_claude_send_streaming_uses_stream_json_format);
    RUN_TEST(test_claude_streaming_retry_success_on_second_attempt);
    RUN_TEST(test_claude_streaming_retry_exhausted);
    RUN_TEST(test_claude_streaming_no_retry_after_tokens_delivered);
    /* retry notification callback */
    RUN_TEST(test_claude_retry_notify_fires_on_zero_output);
    RUN_TEST(test_claude_retry_notify_not_fired_when_tokens_sent);
    /* system prompt builder */
    RUN_TEST(test_system_prompt_contains_agent_name);
    RUN_TEST(test_system_prompt_contains_user_name);
    RUN_TEST(test_system_prompt_references_soul_and_identity);
    RUN_TEST(test_system_prompt_references_user_md);
    RUN_TEST(test_system_prompt_references_priorities);
    RUN_TEST(test_system_prompt_references_skills);
    RUN_TEST(test_system_prompt_contains_timezone);
    RUN_TEST(test_system_prompt_fits_in_buffer);
    RUN_TEST(test_claude_system_prompt_includes_workspace);
    RUN_TEST(test_claude_system_prompt_path_only_fallback);
    /* spawn failure populates resp */
    RUN_TEST(test_claude_send_timeout_sets_is_error);
    RUN_TEST(test_claude_send_timeout_populates_result_detail);
    RUN_TEST(test_claude_send_generic_error_sets_is_error);
    RUN_TEST(test_claude_send_streaming_timeout_sets_is_error);
    /* workspace path TLS */
    RUN_TEST(test_claude_send_sets_workspace_path);
    RUN_TEST(test_llm_provider_send_workspace_threads_path_to_spawn);
    /* default timeout */
    RUN_TEST(test_claude_default_timeout_is_600);
    /* in-flight counter */
    RUN_TEST(test_claude_in_flight_zero_initially);
    RUN_TEST(test_claude_in_flight_zero_after_successful_send);
    RUN_TEST(test_claude_in_flight_zero_after_failed_send);
    /* empty result line is retriable (REQ-048) */
    RUN_TEST(test_claude_empty_result_is_retriable);
    /* generic RELAY_ERR (DNS failures) is retriable (REQ-063) */
    RUN_TEST(test_claude_generic_error_is_retriable);
    /* allowed tools restriction */
    RUN_TEST(test_claude_allowed_tools_in_args);
    RUN_TEST(test_claude_no_allowed_tools_when_unset);
    RUN_TEST(test_claude_allowed_tools_in_streaming_args);
    /* retry_count=0 returns RELAY_ERR (REQ-089) */
    RUN_TEST(test_claude_retry_count_zero_returns_error);
    RUN_TEST(test_claude_streaming_retry_count_zero_returns_error);
}
