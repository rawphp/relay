#include "Unity/unity.h"
#include "workspace_resolver.h"
#include "mocks.h"
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static session_store_t *make_store(void)
{
    mock_fs_reset();
    return session_create(&g_mock_fs, &g_mock_clock, "/sessions.json", 24);
}

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_resolve_active_workspace(void)
{
    session_store_t *s = make_store();
    config_t *cfg = config_load_string(
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n"
        "\n"
        "[workspace \"code\"]\n"
        "path = ~/Code\n"
        "provider = gemini\n");

    session_set_active_workspace(s, "user1", "ea");

    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(0, ws.is_error);
    TEST_ASSERT_EQUAL_INT(0, ws.is_fallback);
    TEST_ASSERT_EQUAL_STRING("ea", ws.name);
    TEST_ASSERT_NOT_NULL(strstr(ws.path, "EA"));

    session_free(s);
    config_free(cfg);
}

static void test_resolve_default_first_workspace(void)
{
    session_store_t *s = make_store();
    config_t *cfg = config_load_string(
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n"
        "\n"
        "[workspace \"code\"]\n"
        "path = ~/Code\n"
        "provider = gemini\n");

    /* No active workspace set */
    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(0, ws.is_error);
    TEST_ASSERT_EQUAL_INT(1, ws.is_fallback);
    TEST_ASSERT_EQUAL_STRING("ea", ws.name);

    /* Should have set active workspace for future calls */
    const char *active = session_get_active_workspace(s, "user1");
    TEST_ASSERT_EQUAL_STRING("ea", active);

    session_free(s);
    config_free(cfg);
}

static void test_resolve_global_fallback(void)
{
    session_store_t *s = make_store();
    config_t *cfg = config_load_string(
        "workspace_path = /global/workspace\n"
        "provider = claude\n");

    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(0, ws.is_error);
    TEST_ASSERT_EQUAL_INT(1, ws.is_fallback);
    TEST_ASSERT_EQUAL_STRING("/global/workspace", ws.path);

    session_free(s);
    config_free(cfg);
}

static void test_resolve_error_no_workspace(void)
{
    session_store_t *s = make_store();
    config_t *cfg = config_load_string("provider = claude\n");

    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(1, ws.is_error);

    session_free(s);
    config_free(cfg);
}

static void test_resolve_stale_active_falls_to_first(void)
{
    session_store_t *s = make_store();
    config_t *cfg = config_load_string(
        "[workspace \"ea\"]\n"
        "path = ~/EA\n"
        "provider = claude\n");

    /* Active is set to a workspace that no longer exists in config */
    session_set_active_workspace(s, "user1", "deleted");

    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(0, ws.is_error);
    TEST_ASSERT_EQUAL_INT(1, ws.is_fallback);
    TEST_ASSERT_EQUAL_STRING("ea", ws.name);

    session_free(s);
    config_free(cfg);
}

/* ── REQ-140: install dir fallback ──────────────────────────────────── */

static void test_resolve_install_dir_fallback(void)
{
    session_store_t *s = make_store();
    /* No workspace blocks or workspace_path */
    config_t *cfg = config_load_string("provider = claude\n");

    resolved_workspace_t ws;
    /* Pass config_path so install dir can be derived */
    workspace_resolve(s, cfg, "user1", "/home/kai/config/relay.conf", &ws);

    TEST_ASSERT_EQUAL_INT(0, ws.is_error);
    TEST_ASSERT_EQUAL_INT(1, ws.is_fallback);
    TEST_ASSERT_EQUAL_STRING("/home/kai", ws.path);
    TEST_ASSERT_EQUAL_STRING("kai", ws.name);

    session_free(s);
    config_free(cfg);
}

static void test_resolve_no_install_dir_errors(void)
{
    session_store_t *s = make_store();
    /* No workspace blocks, no workspace_path, no install_dir */
    config_t *cfg = config_load_string("provider = claude\n");

    resolved_workspace_t ws;
    workspace_resolve(s, cfg, "user1", NULL, &ws);

    TEST_ASSERT_EQUAL_INT(1, ws.is_error);

    session_free(s);
    config_free(cfg);
}

/* ── Suite ──────────────────────────────────────────────────────────── */

void test_workspace_resolver_suite(void)
{
    RUN_TEST(test_resolve_active_workspace);
    RUN_TEST(test_resolve_default_first_workspace);
    RUN_TEST(test_resolve_global_fallback);
    RUN_TEST(test_resolve_error_no_workspace);
    RUN_TEST(test_resolve_stale_active_falls_to_first);
    /* REQ-140 */
    RUN_TEST(test_resolve_install_dir_fallback);
    RUN_TEST(test_resolve_no_install_dir_errors);
}
