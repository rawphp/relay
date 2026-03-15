#include "Unity/unity.h"
#include "llm_provider.h"
#include "mocks.h"

static void test_llm_provider_defaults_to_claude(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"sess-1\",\"result\":\"ok\",\"duration_ms\":12}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);
    TEST_ASSERT_EQUAL_STRING("claude", llm_provider_name(llm));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_with_retry(llm, "hi", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("ok", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_selects_openai_codex(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-1\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"ok from codex\"}}\n");

    const char *cfg_text =
        "llm_provider = openai_codex\n"
        "openai_binary = /usr/local/bin/codex\n"
        "openai_timeout = 45\n"
        "openai_model = gpt-5-codex\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);
    TEST_ASSERT_EQUAL_STRING("openai_codex", llm_provider_name(llm));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_with_retry(llm, "hi", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("thread-1", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("ok from codex", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_accepts_openai_alias(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-alias\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"alias ok\"}}\n");

    const char *cfg_text =
        "llm_provider = openai\n"
        "openai_binary = /usr/local/bin/codex\n"
        "openai_timeout = 45\n"
        "openai_model = gpt-5-codex\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);
    TEST_ASSERT_EQUAL_STRING("openai_codex", llm_provider_name(llm));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_with_retry(llm, "hi", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("thread-alias", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("alias ok", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_selects_gemini(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"session_id\":\"gem-session\",\"response\":\"gemini ok\"}\n");

    const char *cfg_text =
        "llm_provider = gemini\n"
        "gemini_binary = /usr/local/bin/gemini\n"
        "gemini_model = gemini-2.5-flash\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);
    TEST_ASSERT_EQUAL_STRING("gemini", llm_provider_name(llm));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hi", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("gem-session", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("gemini ok", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_accepts_gemini_alias(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"session_id\":\"alias\",\"response\":\"gemini alias\"}\n");

    const char *cfg_text =
        "llm_provider = google_gemini\n"
        "gemini_binary = /usr/local/bin/gemini\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);
    TEST_ASSERT_EQUAL_STRING("gemini", llm_provider_name(llm));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("alias", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("gemini alias", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Tests: llm_provider_send_streaming ─────────────────────────────── */

/* Reuse token_acc_t pattern (defined inline here for isolation) */
typedef struct {
    char text[RELAY_MAX_RESPONSE];
    int call_count;
} llm_token_acc_t;

static int llm_token_accumulate(const char *text, size_t len, void *userdata)
{
    llm_token_acc_t *acc = userdata;
    size_t existing = strlen(acc->text);
    if (existing + len < sizeof(acc->text) - 1) {
        memcpy(acc->text + existing, text, len);
        acc->text[existing + len] = '\0';
    }
    acc->call_count++;
    return 0;
}

static void test_llm_provider_send_streaming_delivers_tokens(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output(
        "Streaming text here",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"llm-stream-sess\","
        "\"result\":\"Streaming text here\","
        "\"duration_ms\":1200}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_token_acc_t acc;
    memset(&acc, 0, sizeof(acc));

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = llm_provider_send_streaming(llm, "stream test", NULL,
                                         NULL,
                                         llm_token_accumulate, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_GREATER_THAN(0, acc.call_count);
    TEST_ASSERT_EQUAL_STRING("Streaming text here", acc.text);
    TEST_ASSERT_EQUAL_STRING("llm-stream-sess", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("Streaming text here", resp.result);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_send_streaming_null_safety(void)
{
    llm_token_acc_t acc;
    memset(&acc, 0, sizeof(acc));
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* NULL provider should return error */
    int rc = llm_provider_send_streaming(NULL, "test", NULL,
                                          NULL,
                                          llm_token_accumulate, &acc, &resp);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
}

static void test_llm_provider_send_streaming_uses_workspace_path(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output(
        "response",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"ws-stream-sess\","
        "\"result\":\"response\","
        "\"duration_ms\":100}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /default/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_token_acc_t acc;
    memset(&acc, 0, sizeof(acc));
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = llm_provider_send_streaming(llm, "hello", NULL,
                                         "/custom/workspace",
                                         llm_token_accumulate, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Verify the args passed to spawn_streaming contain --add-dir /custom/workspace */
    int found = 0;
    if (g_mock_proc_last_args) {
        for (int i = 0; g_mock_proc_last_args[i]; i++) {
            if (strcmp(g_mock_proc_last_args[i], "--add-dir") == 0 &&
                g_mock_proc_last_args[i + 1] &&
                strcmp(g_mock_proc_last_args[i + 1], "/custom/workspace") == 0) {
                found = 1;
                break;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_send_streaming_uses_default_when_no_workspace(void)
{
    mock_proc_reset();
    mock_proc_set_stream_output(
        "response",
        "{\"type\":\"result\",\"subtype\":\"success\","
        "\"session_id\":\"def-stream-sess\","
        "\"result\":\"response\","
        "\"duration_ms\":100}"
    );

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /default/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_token_acc_t acc;
    memset(&acc, 0, sizeof(acc));
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    /* NULL workspace_path — should use config default */
    int rc = llm_provider_send_streaming(llm, "hello", NULL,
                                         NULL,
                                         llm_token_accumulate, &acc, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    int found = 0;
    if (g_mock_proc_last_args) {
        for (int i = 0; g_mock_proc_last_args[i]; i++) {
            if (strcmp(g_mock_proc_last_args[i], "--add-dir") == 0 &&
                g_mock_proc_last_args[i + 1] &&
                strcmp(g_mock_proc_last_args[i + 1], "/default/workspace") == 0) {
                found = 1;
                break;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found);

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_in_flight_zero_initially(void)
{
    mock_proc_reset();

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    /* No calls yet — must be zero */
    TEST_ASSERT_EQUAL_INT(0, llm_provider_in_flight(llm));

    llm_provider_free(llm);
    config_free(cfg);
}

static void test_llm_provider_in_flight_zero_after_send(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"sess\",\"result\":\"ok\",\"duration_ms\":50}");

    const char *cfg_text =
        "claude_binary = /usr/local/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    llm_provider_send(llm, "hello", NULL, &resp);

    /* Counter must return to zero after send completes */
    TEST_ASSERT_EQUAL_INT(0, llm_provider_in_flight(llm));

    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Identity injection tests ────────────────────────────────────────── */

/* Test: when identity is set and files exist, their contents are prepended
 * to the message that the backend receives. */
static void test_llm_provider_identity_injected_in_message(void)
{
    mock_proc_reset();
    mock_fs_reset();

    mock_fs_set("/ws/SOUL.md",       "You are bold and direct.");
    mock_fs_set("/ws/IDENTITY.md",   "Your name is Henry.");
    mock_fs_set("/ws/USER.md",       "John is a developer.");
    mock_fs_set("/ws/PRIORITIES.md", "Focus on speed.");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_provider_set_identity(llm, &g_mock_fs, "/ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* All four identity files must appear in the input to Claude */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are bold and direct."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "Your name is Henry."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "John is a developer."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "Focus on speed."));
    /* Original message must still be present */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "hello"));
    /* Identity block must appear before the user message */
    const char *ident_pos = strstr(g_mock_proc_last_input, "You are bold and direct.");
    const char *msg_pos   = strstr(g_mock_proc_last_input, "hello");
    TEST_ASSERT(ident_pos < msg_pos);

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: missing identity files are silently skipped — no crash. */
static void test_llm_provider_identity_missing_files_skipped(void)
{
    mock_proc_reset();
    mock_fs_reset();

    /* Only SOUL.md present; all others absent */
    mock_fs_set("/ws/SOUL.md", "You are Henry.");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_provider_set_identity(llm, &g_mock_fs, "/ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are Henry."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "hello"));

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: identity + memory context both injected; identity precedes memory. */
static void test_llm_provider_identity_and_memory_combined(void)
{
    mock_proc_reset();
    mock_fs_reset();
    mock_http_reset();

    mock_fs_set("/ws/SOUL.md", "You are Henry.");
    mock_fs_set("/ws/USER.md", "John is a developer.");

    mock_http_set_response(
        "[1] diary.md (lines 1-3) | Score: 0.9\n"
        "--------\n"
        "John likes coffee.\n");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n"
        "memory_search_url = http://localhost:9999/search\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    memory_search_t *ms = memory_search_create(&g_mock_proc, &g_mock_http, cfg);
    llm_provider_set_memory(llm, ms);
    llm_provider_set_identity(llm, &g_mock_fs, "/ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    llm_provider_send(llm, "who am I", NULL, &resp);

    /* Both identity and memory context injected */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are Henry."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "John likes coffee."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "who am I"));

    /* Identity must appear before memory context */
    const char *ident_pos  = strstr(g_mock_proc_last_input, "You are Henry.");
    const char *memory_pos = strstr(g_mock_proc_last_input, "John likes coffee.");
    TEST_ASSERT(ident_pos < memory_pos);

    memory_search_free(ms);
    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: send_workspace uses provided workspace path for identity injection */
static void test_send_workspace_uses_workspace_path(void)
{
    mock_proc_reset();
    mock_fs_reset();

    /* Identity in a non-default workspace path */
    mock_fs_set("/other-ws/SOUL.md", "You are Relay.");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /default-ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    /* Set identity on default workspace (should NOT be used for send_workspace call) */
    llm_provider_set_identity(llm, &g_mock_fs, "/default-ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_workspace(llm, "hello", NULL,
                                         "/other-ws", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are Relay."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "hello"));

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: send_workspace with null provider_name uses the default backend */
static void test_send_workspace_null_provider_uses_default(void)
{
    mock_proc_reset();
    mock_fs_reset();

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"sw-sess\","
        "\"result\":\"workspace response\",\"duration_ms\":5}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_workspace(llm, "ping", NULL,
                                         "/ws", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("workspace response", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: existing send_with_retry still works after send_workspace changes */
static void test_send_workspace_backward_compat(void)
{
    mock_proc_reset();

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"bc-sess\","
        "\"result\":\"compat ok\",\"duration_ms\":1}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send_with_retry(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("compat ok", resp.result);

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: SKILLS.md is included in identity block when present */
static void test_llm_provider_identity_includes_skills(void)
{
    mock_proc_reset();
    mock_fs_reset();

    mock_fs_set("/ws/SOUL.md",       "You are bold.");
    mock_fs_set("/ws/SKILLS.md",     "| Speak | ~/relay/skills/speak.sh | TTS |");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_provider_set_identity(llm, &g_mock_fs, "/ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* SKILLS.md content must appear in input */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "Speak"));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "TTS"));
    /* Identity files must still be present */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are bold."));

    llm_provider_free(llm);
    config_free(cfg);
}

/* Test: missing SKILLS.md does not cause errors */
static void test_llm_provider_identity_missing_skills_ok(void)
{
    mock_proc_reset();
    mock_fs_reset();

    /* Only SOUL.md — no SKILLS.md */
    mock_fs_set("/ws/SOUL.md", "You are Henry.");

    mock_proc_set_output(
        "{\"type\":\"result\",\"session_id\":\"s\","
        "\"result\":\"ok\",\"duration_ms\":10}");

    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /ws\n";

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(llm);

    llm_provider_set_identity(llm, &g_mock_fs, "/ws");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(llm, "hello", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Should still work fine */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "You are Henry."));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "hello"));
    /* SKILLS.md should NOT appear */
    TEST_ASSERT_NULL(strstr(g_mock_proc_last_input, "SKILLS.md"));

    llm_provider_free(llm);
    config_free(cfg);
}

void test_llm_provider_suite(void)
{
    RUN_TEST(test_llm_provider_defaults_to_claude);
    RUN_TEST(test_llm_provider_selects_openai_codex);
    RUN_TEST(test_llm_provider_accepts_openai_alias);
    RUN_TEST(test_llm_provider_selects_gemini);
    RUN_TEST(test_llm_provider_accepts_gemini_alias);
    /* streaming tests */
    RUN_TEST(test_llm_provider_send_streaming_delivers_tokens);
    RUN_TEST(test_llm_provider_send_streaming_null_safety);
    RUN_TEST(test_llm_provider_send_streaming_uses_workspace_path);
    RUN_TEST(test_llm_provider_send_streaming_uses_default_when_no_workspace);
    /* in-flight counter */
    RUN_TEST(test_llm_provider_in_flight_zero_initially);
    RUN_TEST(test_llm_provider_in_flight_zero_after_send);
    /* identity injection */
    RUN_TEST(test_llm_provider_identity_injected_in_message);
    RUN_TEST(test_llm_provider_identity_missing_files_skipped);
    RUN_TEST(test_llm_provider_identity_and_memory_combined);
    /* skills injection */
    RUN_TEST(test_llm_provider_identity_includes_skills);
    RUN_TEST(test_llm_provider_identity_missing_skills_ok);
    /* send_workspace */
    RUN_TEST(test_send_workspace_uses_workspace_path);
    RUN_TEST(test_send_workspace_null_provider_uses_default);
    RUN_TEST(test_send_workspace_backward_compat);
}
