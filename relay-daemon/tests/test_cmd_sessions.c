#include "Unity/unity.h"
#include "cmd_sessions.h"
#include "mocks.h"
#include <stdlib.h>
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static session_store_t *make_store(void)
{
    mock_fs_reset();
    return session_create(&g_mock_fs, &g_mock_clock, "/sessions.json", 24);
}

static config_t *make_claude_config(void)
{
    const char *text =
        "provider = claude\n"
        "\n"
        "[workspace \"relay\"]\n"
        "path = /Users/tom/project\n"
        "provider = claude\n";
    return config_load_string(text);
}

static config_t *make_gemini_config(void)
{
    const char *text =
        "provider = gemini\n"
        "\n"
        "[workspace \"relay\"]\n"
        "path = /Users/tom/project\n"
        "provider = gemini\n";
    return config_load_string(text);
}

/* ── Provider gating tests ─────────────────────────────────────────── */

static void test_provider_claude_supported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("claude", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, ok);
    TEST_ASSERT_EQUAL_STRING("", reply);
}

static void test_provider_default_supported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, ok);

    ok = cmd_sessions_provider_supported(NULL, reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, ok);
}

static void test_provider_codex_unsupported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("openai_codex", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, ok);
    TEST_ASSERT_NOT_NULL(strstr(reply, "OpenAI Codex"));
}

static void test_provider_gemini_unsupported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("gemini", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, ok);
    TEST_ASSERT_NOT_NULL(strstr(reply, "Gemini"));
}

static void test_provider_unknown_unsupported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("some_new_llm", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, ok);
    TEST_ASSERT_NOT_NULL(strstr(reply, "some_new_llm"));
}

/* ── /sessions command handler tests ───────────────────────────────── */

static void test_sessions_not_sessions_command(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_claude_config();
    char reply[512] = {0};

    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/help", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, handled);

    session_free(s);
    config_free(cfg);
}

static void test_sessions_no_workspace_active(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_claude_config();
    char reply[512] = {0};

    /* No active workspace set */
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/sessions", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, handled);
    /* Should still work — falls back to first workspace */

    session_free(s);
    config_free(cfg);
}

static void test_sessions_gemini_provider_rejected(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_gemini_config();
    char reply[512] = {0};

    session_set_active_workspace(s, "user1", "relay");
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/sessions", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "Gemini"));

    session_free(s);
    config_free(cfg);
}

static void test_sessions_lists_discovered_sessions(void)
{
    mock_fs_reset();

    /* Override HOME so session discovery looks in mock paths */
    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    /* Set up mock .jsonl files in the encoded directory */
    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/aaa-111.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Fix login\"}}\n");
    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/bbb-222.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Add tests\"}}\n");

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();

    session_set_active_workspace(s, "user1", "relay");
    char reply[1024] = {0};
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/sessions", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    /* Should list both sessions */
    TEST_ASSERT_NOT_NULL(strstr(reply, "Fix login"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "Add tests"));
    /* Should have "Start new" option */
    TEST_ASSERT_NOT_NULL(strstr(reply, "new session"));

    session_free(s);
    config_free(cfg);

    /* Restore HOME */
    if (orig_home) {
        setenv("HOME", orig_home, 1);
        free(orig_home);
    }
}

static void test_sessions_no_sessions_found(void)
{
    mock_fs_reset();
    /* No .jsonl files */

    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();

    session_set_active_workspace(s, "user1", "relay");
    char reply[512] = {0};
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/sessions", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "No previous sessions"));

    session_free(s);
    config_free(cfg);

    if (orig_home) {
        setenv("HOME", orig_home, 1);
        free(orig_home);
    }
}

/* ── REQ-139: no-workspace tests ────────────────────────────────────── */

static config_t *make_empty_config(void)
{
    /* No workspace blocks at all */
    const char *text = "provider = claude\n";
    return config_load_string(text);
}

static void test_sessions_no_workspace_configured(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_empty_config();
    char reply[512] = {0};

    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/sessions", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    /* Should mention relay.conf */
    TEST_ASSERT_NOT_NULL(strstr(reply, "relay.conf"));

    session_free(s);
    config_free(cfg);
}

/* ── REQ-143: space name in header ──────────────────────────────────── */

static void test_sessions_header_shows_space_name(void)
{
    mock_fs_reset();
    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/aaa.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Hi\"}}\n");

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();
    session_set_active_workspace(s, "user1", "relay");

    char reply[1024] = {0};
    cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                        "/sessions", reply, sizeof(reply));

    /* Header should show workspace name "relay" */
    TEST_ASSERT_NOT_NULL(strstr(reply, "relay"));

    session_free(s);
    config_free(cfg);
    if (orig_home) { setenv("HOME", orig_home, 1); free(orig_home); }
}

static config_t *make_global_path_config(void)
{
    const char *text =
        "provider = claude\n"
        "workspace_path = /Users/tom/project\n";
    return config_load_string(text);
}

static void test_sessions_header_shows_basename_fallback(void)
{
    mock_fs_reset();
    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/bbb.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Hi\"}}\n");

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_global_path_config();

    char reply[1024] = {0};
    cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                        "/sessions", reply, sizeof(reply));

    /* No workspace name — should show basename "project", not full path */
    TEST_ASSERT_NOT_NULL(strstr(reply, "Sessions in project:"));

    session_free(s);
    config_free(cfg);
    if (orig_home) { setenv("HOME", orig_home, 1); free(orig_home); }
}

/* ── REQ-142: /session <N> selection ────────────────────────────────── */

static void test_session_select_valid(void)
{
    mock_fs_reset();
    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/aaa-111.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Fix login\"}}\n");

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();
    session_set_active_workspace(s, "user1", "relay");

    /* First list sessions to populate the cache */
    char reply[1024] = {0};
    cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                        "/sessions", reply, sizeof(reply));

    /* Now select session 1 */
    memset(reply, 0, sizeof(reply));
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/session 1", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "aaa-111"));

    session_free(s);
    config_free(cfg);
    if (orig_home) { setenv("HOME", orig_home, 1); free(orig_home); }
}

static void test_session_select_zero_new(void)
{
    mock_fs_reset();
    char *orig_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    setenv("HOME", "/home/tom", 1);

    mock_fs_set("/home/tom/.claude/projects/-Users-tom-project/aaa-111.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Fix login\"}}\n");

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();
    session_set_active_workspace(s, "user1", "relay");

    /* List then select 0 = new session */
    char reply[1024] = {0};
    cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                        "/sessions", reply, sizeof(reply));

    memset(reply, 0, sizeof(reply));
    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/session 0", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "new session"));

    session_free(s);
    config_free(cfg);
    if (orig_home) { setenv("HOME", orig_home, 1); free(orig_home); }
}

static void test_session_select_out_of_range(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();
    char reply[512] = {0};

    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/session 99", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    /* Should indicate invalid selection */
    TEST_ASSERT_NOT_NULL(strstr(reply, "/sessions"));

    session_free(s);
    config_free(cfg);
}

static void test_session_no_arg_shows_usage(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                        "/sessions.json", 24);
    config_t *cfg = make_claude_config();
    char reply[512] = {0};

    int handled = cmd_sessions_handle(&g_mock_fs, s, cfg, "user1",
                                      "/session", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "/session"));

    session_free(s);
    config_free(cfg);
}

/* ── Suite ──────────────────────────────────────────────────────────── */

void test_cmd_sessions_suite(void)
{
    RUN_TEST(test_provider_claude_supported);
    RUN_TEST(test_provider_default_supported);
    RUN_TEST(test_provider_codex_unsupported);
    RUN_TEST(test_provider_gemini_unsupported);
    RUN_TEST(test_provider_unknown_unsupported);
    /* REQ-136: /sessions command handler */
    RUN_TEST(test_sessions_not_sessions_command);
    RUN_TEST(test_sessions_no_workspace_active);
    RUN_TEST(test_sessions_gemini_provider_rejected);
    RUN_TEST(test_sessions_lists_discovered_sessions);
    RUN_TEST(test_sessions_no_sessions_found);
    /* REQ-139 */
    RUN_TEST(test_sessions_no_workspace_configured);
    /* REQ-143: space name in header */
    RUN_TEST(test_sessions_header_shows_space_name);
    RUN_TEST(test_sessions_header_shows_basename_fallback);
    /* REQ-142 */
    RUN_TEST(test_session_select_valid);
    RUN_TEST(test_session_select_zero_new);
    RUN_TEST(test_session_select_out_of_range);
    RUN_TEST(test_session_no_arg_shows_usage);
}
