#include "Unity/unity.h"
#include "cmd_workspace.h"
#include "mocks.h"
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static session_store_t *make_store(void)
{
    mock_fs_reset();
    return session_create(&g_mock_fs, &g_mock_clock, "/sessions.json", 24);
}

static config_t *make_config_two_workspaces(void)
{
    const char *text =
        "provider = claude\n"
        "\n"
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n"
        "\n"
        "[workspace \"code\"]\n"
        "path = ~/Code\n"
        "provider = gemini\n";
    return config_load_string(text);
}

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_cmd_not_workspace_command(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "hello there", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(0, handled);

    session_free(s);
    config_free(cfg);
}

static void test_cmd_session_switch_known(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space ea", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));

    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}

static void test_cmd_session_switch_unknown(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space missing", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "missing"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_sessions_lists_workspaces(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "code"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_sessions_marks_active(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    /* Set "code" as active */
    session_set_active_workspace(s, "user1", "code");
    cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    /* Active workspace should be marked with * */
    TEST_ASSERT_NOT_NULL(strstr(reply, "*"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_workspace_info(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    session_set_active_workspace(s, "user1", "ea");
    int handled = cmd_workspace_handle(s, cfg, "user1", "/workspace", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_close_clears_session(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    /* Create a session entry for the workspace */
    session_set(s, "user1", "sess-abc");
    session_set_workspace(s, "user1", "ea", "/home/ea", "claude");
    session_set_active_workspace(s, "user1", "ea");

    int handled = cmd_workspace_handle(s, cfg, "user1", "/close", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    /* Session for workspace should be gone */
    const session_entry_t *e = session_get_for_workspace(s, "user1", "ea");
    TEST_ASSERT_NULL(e);

    session_free(s);
    config_free(cfg);
}

static void test_cmd_clear_alias(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    session_set(s, "user1", "sess-def");
    session_set_workspace(s, "user1", "ea", "/home/ea", "claude");
    session_set_active_workspace(s, "user1", "ea");

    int handled = cmd_workspace_handle(s, cfg, "user1", "/clear", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    const session_entry_t *e = session_get_for_workspace(s, "user1", "ea");
    TEST_ASSERT_NULL(e);

    session_free(s);
    config_free(cfg);
}


static void test_cmd_session_no_arg_lists_workspaces(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    /* /session with no argument should show workspace list */
    int handled = cmd_workspace_handle(s, cfg, "user1", "/space", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "code"));

    session_free(s);
    config_free(cfg);
}


static void test_cmd_session_double_quoted_arg(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    /* /session "ea" — quotes should be stripped before workspace lookup */
    int handled = cmd_workspace_handle(s, cfg, "user1", "/space \"ea\"", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}

static void test_cmd_session_single_quoted_arg(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    /* /session 'ea' — single quotes should be stripped */
    int handled = cmd_workspace_handle(s, cfg, "user1", "/space 'ea'", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}

static config_t *make_config_with_fallback_path(void)
{
    /* Two named workspaces plus a global workspace_path fallback */
    const char *text =
        "workspace_path = /Users/agent/home\n"
        "\n"
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n"
        "\n"
        "[workspace \"code\"]\n"
        "path = ~/Code\n"
        "provider = claude\n";
    return config_load_string(text);
}

static config_t *make_config_fallback_matches_named(void)
{
    /* workspace_path equals the path of the first named workspace */
    const char *text =
        "workspace_path = ~/EA\n"
        "\n"
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n";
    return config_load_string(text);
}

static void test_cmd_sessions_shows_fallback_workspace(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_with_fallback_path();
    char reply[1024] = {0};

    /* No named workspace is active — fallback should appear with (default) label */
    int handled = cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "/Users/agent/home"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "(default)"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_sessions_no_duplicate_when_path_matches_named(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_fallback_matches_named();
    char reply[1024] = {0};

    cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    /* "ea" should appear exactly once — not duplicated as both named and (default) */
    const char *first = strstr(reply, "ea");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, "(default)"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_session_no_arg_shows_active_none(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    /* No workspace active — first line should say "Active: none" */
    cmd_workspace_handle(s, cfg, "user1", "/space", reply, sizeof(reply));

    TEST_ASSERT_NOT_NULL(strstr(reply, "Active: none"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_session_no_arg_shows_active_name(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    session_set_active_workspace(s, "user1", "ea");
    cmd_workspace_handle(s, cfg, "user1", "/space", reply, sizeof(reply));

    TEST_ASSERT_NOT_NULL(strstr(reply, "Active: ea"));

    session_free(s);
    config_free(cfg);
}

/* ── /space and /spaces tests (REQ-133) ────────────────────────────── */

static void test_cmd_space_switch_known(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space ea", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));
    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}

static void test_cmd_space_switch_unknown(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space missing", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "missing"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_spaces_lists_workspaces(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "code"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_space_no_arg_lists_workspaces(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "code"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_space_quoted_arg(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/space \"ea\"", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}


static void test_cmd_sessions_alias_still_works(void)
{
    /* /sessions should still work as an alias for /spaces */
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[512] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "ea"));

    session_free(s);
    config_free(cfg);
}

static void test_cmd_space_help_text_references_space(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_config_two_workspaces();
    char reply[256] = {0};

    /* /space with no valid arg should show help referencing /space */
    cmd_workspace_handle(s, cfg, "user1", "/space", reply, sizeof(reply));

    /* Help text should mention /space, not /session */
    TEST_ASSERT_NOT_NULL(strstr(reply, "/space"));

    session_free(s);
    config_free(cfg);
}

/* ── REQ-139: no-workspace edge case ───────────────────────────────── */

static config_t *make_empty_config(void)
{
    return config_load_string("provider = claude\n");
}

static void test_cmd_spaces_no_workspaces(void)
{
    session_store_t *s = make_store();
    config_t *cfg = make_empty_config();
    char reply[512] = {0};

    int handled = cmd_workspace_handle(s, cfg, "user1", "/spaces", reply, sizeof(reply));

    TEST_ASSERT_EQUAL_INT(1, handled);
    TEST_ASSERT_NOT_NULL(strstr(reply, "No workspaces configured"));

    session_free(s);
    config_free(cfg);
}

/* ── Suite ──────────────────────────────────────────────────────────── */

void test_cmd_workspace_suite(void)
{
    RUN_TEST(test_cmd_not_workspace_command);
    RUN_TEST(test_cmd_session_switch_known);
    RUN_TEST(test_cmd_session_switch_unknown);
    RUN_TEST(test_cmd_sessions_lists_workspaces);
    RUN_TEST(test_cmd_sessions_marks_active);
    RUN_TEST(test_cmd_session_no_arg_lists_workspaces);
    RUN_TEST(test_cmd_session_double_quoted_arg);
    RUN_TEST(test_cmd_session_single_quoted_arg);
    RUN_TEST(test_cmd_workspace_info);
    RUN_TEST(test_cmd_close_clears_session);
    RUN_TEST(test_cmd_clear_alias);
    RUN_TEST(test_cmd_sessions_shows_fallback_workspace);
    RUN_TEST(test_cmd_sessions_no_duplicate_when_path_matches_named);
    RUN_TEST(test_cmd_session_no_arg_shows_active_none);
    RUN_TEST(test_cmd_session_no_arg_shows_active_name);
    /* REQ-133: /space and /spaces */
    RUN_TEST(test_cmd_space_switch_known);
    RUN_TEST(test_cmd_space_switch_unknown);
    RUN_TEST(test_cmd_spaces_lists_workspaces);
    RUN_TEST(test_cmd_space_no_arg_lists_workspaces);
    RUN_TEST(test_cmd_space_quoted_arg);
    RUN_TEST(test_cmd_sessions_alias_still_works);
    RUN_TEST(test_cmd_space_help_text_references_space);
    /* REQ-139 */
    RUN_TEST(test_cmd_spaces_no_workspaces);
}
