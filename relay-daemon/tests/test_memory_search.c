#include "Unity/unity.h"
#include "memory_search.h"
#include "mocks.h"

/* ── Test: Create with defaults ────────────────────────────────────── */
static void test_memory_search_create_defaults(void)
{
    const char *cfg_text =
        "workspace_path = /home/user/relay\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: Disabled returns NULL ───────────────────────────────────── */
static void test_memory_search_create_disabled(void)
{
    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_enabled = 0\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NULL(ms);

    config_free(cfg);
}

/* ── Test: No http client → fail-open (return 0) ───────────────────── */
static void test_memory_search_no_http_fails_open(void)
{
    mock_proc_reset();
    mock_http_reset();

    /* Pass NULL for http — should fail-open, never call proc */
    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "tell me about Jamie", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: No results returns 0 ────────────────────────────────────── */
static void test_memory_search_query_no_results(void)
{
    mock_proc_reset();
    mock_proc_set_output("No results found.");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "something irrelevant", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: Timeout returns 0 (fail-open) ──────────────────────────── */
static void test_memory_search_query_timeout(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR_TIMEOUT;

    const char *cfg_text =
        "workspace_path = /home/user/relay\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "test query", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: Proc failure returns 0 (fail-open) ─────────────────────── */
static void test_memory_search_query_proc_failure(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR;

    const char *cfg_text =
        "workspace_path = /home/user/relay\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "test query", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: Short message skipped ───────────────────────────────────── */
static void test_memory_search_query_short_message(void)
{
    mock_proc_reset();

    const char *cfg_text =
        "workspace_path = /home/user/relay\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "ok", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);
    /* Proc should not have been called */
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: NULL safety ─────────────────────────────────────────────── */
static void test_memory_search_null_safety(void)
{
    char context[128];

    /* NULL searcher */
    int n = memory_search_query(NULL, "test", context, sizeof(context));
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    /* NULL query */
    mock_proc_reset();
    const char *cfg_text = "workspace_path = /home/user/relay\n";
    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, NULL, cfg);

    n = memory_search_query(ms, NULL, context, sizeof(context));
    TEST_ASSERT_EQUAL_INT(0, n);

    /* NULL context buffer */
    n = memory_search_query(ms, "test", NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, n);

    /* Free NULL is safe */
    memory_search_free(NULL);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Test: Update config changes top_k ─────────────────────────────── */
static void test_memory_search_update_config(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response(
        "[1] test.md (lines 1-3) | Score: 0.9\n"
        "--------\n"
        "Some memory.\n");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765\n"
        "memory_search_top_k = 3\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "test query", context, sizeof(context));
    TEST_ASSERT_EQUAL_INT(1, n);

    /* Update top_k to 5 */
    const char *new_cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765\n"
        "memory_search_top_k = 5\n";

    config_t *new_cfg = config_load_string(new_cfg_text);
    memory_search_update_config(ms, new_cfg);

    /* Verify the HTTP body now contains "top_k":5 */
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response(
        "[1] test.md (lines 1-3) | Score: 0.9\n"
        "--------\n"
        "Some memory.\n");

    memory_search_query(ms, "test query two", context, sizeof(context));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"top_k\":5"));

    memory_search_free(ms);
    config_free(cfg);
    config_free(new_cfg);
}

/* ── Test: Threshold change propagated in request body ─────────────── */
static void test_memory_search_threshold_in_body(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response(
        "[1] test.md (lines 1-3) | Score: 0.9\n"
        "--------\n"
        "Memory content.\n");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765\n"
        "memory_search_threshold = 0.75\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    memory_search_query(ms, "test query", context, sizeof(context));

    /* Request body must include the threshold as min_score */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "0.75"));

    memory_search_free(ms);
    config_free(cfg);
}

/* ── HTTP memory search tests ──────────────────────────────────────── */

/* Test: when memory_search_url is configured, http->post is called instead
 * of proc->spawn. */
static void test_memory_search_uses_http_when_url_configured(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response(
        "[1] 2025-01-15.md (lines 12-18) | Score: 0.823\n"
        "--------\n"
        "John's son Jamie likes Roblox.\n");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765/search\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "tell me about Jamie", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_NOT_NULL(strstr(context, "Jamie likes Roblox"));
    /* HTTP was called, proc was not */
    TEST_ASSERT_EQUAL_STRING("http://localhost:8765/search", g_mock_http_last_url);
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());

    memory_search_free(ms);
    config_free(cfg);
}

/* Test: HTTP POST body contains the query text. */
static void test_memory_search_http_request_body_contains_query(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response(
        "[1] test.md (lines 1-3) | Score: 0.9\n"
        "--------\n"
        "Some memory.\n");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765/search\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    memory_search_query(ms, "what does John enjoy", context, sizeof(context));

    /* POST body must include the query string */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "what does John enjoy"));

    memory_search_free(ms);
    config_free(cfg);
}

/* Test: HTTP failure is fail-open (returns 0, empty context). */
static void test_memory_search_http_error_fail_open(void)
{
    mock_proc_reset();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765/search\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "test query", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    memory_search_free(ms);
    config_free(cfg);
}

/* Test: no URL configured → fail-open (HTTP not called, returns 0). */
static void test_memory_search_no_url_fails_open(void)
{
    mock_proc_reset();
    mock_http_reset();

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = \n";  /* empty URL */

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "test query", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);
    /* Neither proc nor HTTP should have been called */
    TEST_ASSERT_EQUAL_STRING("", g_mock_http_last_url);
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());

    memory_search_free(ms);
    config_free(cfg);
}

/* Test: HTTP "No results found" response returns 0. */
static void test_memory_search_http_no_results(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_http_set_response("No results found.");

    const char *cfg_text =
        "workspace_path = /home/user/relay\n"
        "memory_search_url = http://localhost:8765/search\n";

    config_t *cfg = config_load_string(cfg_text);
    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(ms);

    char context[RELAY_MAX_MEMORY_CONTEXT];
    int n = memory_search_query(ms, "something very obscure", context, sizeof(context));

    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", context);

    memory_search_free(ms);
    config_free(cfg);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_memory_search_suite(void)
{
    RUN_TEST(test_memory_search_create_defaults);
    RUN_TEST(test_memory_search_create_disabled);
    RUN_TEST(test_memory_search_no_http_fails_open);
    RUN_TEST(test_memory_search_query_no_results);
    RUN_TEST(test_memory_search_query_timeout);
    RUN_TEST(test_memory_search_query_proc_failure);
    RUN_TEST(test_memory_search_query_short_message);
    RUN_TEST(test_memory_search_null_safety);
    RUN_TEST(test_memory_search_update_config);
    RUN_TEST(test_memory_search_threshold_in_body);
    RUN_TEST(test_memory_search_uses_http_when_url_configured);
    RUN_TEST(test_memory_search_http_request_body_contains_query);
    RUN_TEST(test_memory_search_http_error_fail_open);
    RUN_TEST(test_memory_search_no_url_fails_open);
    RUN_TEST(test_memory_search_http_no_results);
}
