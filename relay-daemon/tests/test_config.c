#include "Unity/unity.h"
#include "relay.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

/* ── Test: Parse valid config with all keys ─────────────────────────── */
static void test_config_parse_valid(void)
{
    const char *text =
        "telegram_bot_token = abc123\n"
        "telegram_user_id = 123456789\n"
        "claude_timeout = 120\n"
        "workspace_path = /Users/john/life\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("abc123",
        config_get(cfg, "telegram_bot_token", ""));
    TEST_ASSERT_EQUAL_STRING("123456789",
        config_get(cfg, "telegram_user_id", ""));
    TEST_ASSERT_EQUAL_STRING("/Users/john/life",
        config_get(cfg, "workspace_path", ""));
    TEST_ASSERT_EQUAL_INT(120,
        config_get_int(cfg, "claude_timeout", 0));

    config_free(cfg);
}

/* ── Test: Missing key returns fallback ─────────────────────────────── */
static void test_config_missing_key_returns_fallback(void)
{
    const char *text = "key1 = value1\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("default",
        config_get(cfg, "nonexistent", "default"));
    TEST_ASSERT_EQUAL_INT(42,
        config_get_int(cfg, "nonexistent", 42));

    config_free(cfg);
}

/* ── Test: Comments and blank lines are ignored ─────────────────────── */
static void test_config_comments_and_blanks(void)
{
    const char *text =
        "# This is a comment\n"
        "\n"
        "key1 = value1\n"
        "   # Indented comment\n"
        "\n"
        "key2 = value2\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("value1", config_get(cfg, "key1", ""));
    TEST_ASSERT_EQUAL_STRING("value2", config_get(cfg, "key2", ""));

    config_free(cfg);
}

/* ── Test: Whitespace is trimmed from keys and values ───────────────── */
static void test_config_trims_whitespace(void)
{
    const char *text =
        "  key1  =  value1  \n"
        "key2=value2\n"
        "  key3  =  some longer value  \n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("value1", config_get(cfg, "key1", ""));
    TEST_ASSERT_EQUAL_STRING("value2", config_get(cfg, "key2", ""));
    TEST_ASSERT_EQUAL_STRING("some longer value", config_get(cfg, "key3", ""));

    config_free(cfg);
}

/* ── Test: Malformed lines are skipped (no =) ───────────────────────── */
static void test_config_skips_malformed_lines(void)
{
    const char *text =
        "valid_key = valid_value\n"
        "this line has no equals sign\n"
        "another_valid = works\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("valid_value",
        config_get(cfg, "valid_key", ""));
    TEST_ASSERT_EQUAL_STRING("works",
        config_get(cfg, "another_valid", ""));

    config_free(cfg);
}

/* ── Test: Integer parsing with non-numeric value returns fallback ──── */
static void test_config_int_non_numeric(void)
{
    const char *text = "port = not_a_number\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_INT(8080,
        config_get_int(cfg, "port", 8080));

    config_free(cfg);
}

/* ── Test: Empty config string is valid (zero entries) ──────────────── */
static void test_config_empty_string(void)
{
    config_t *cfg = config_load_string("");
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("fallback",
        config_get(cfg, "anything", "fallback"));

    config_free(cfg);
}

/* ── Test: NULL config string returns NULL ───────────────────────────── */
static void test_config_null_string(void)
{
    config_t *cfg = config_load_string(NULL);
    TEST_ASSERT_NULL(cfg);
}

/* ── Test: Values with = sign in them ───────────────────────────────── */
static void test_config_value_with_equals(void)
{
    const char *text = "url = https://api.example.com?key=abc&token=xyz\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    TEST_ASSERT_EQUAL_STRING("https://api.example.com?key=abc&token=xyz",
        config_get(cfg, "url", ""));

    config_free(cfg);
}

/* ── Test: Validate with all required keys present ─────────────────── */
static void test_config_validate_all_present(void)
{
    const char *text =
        "telegram_bot_token = abc123\n"
        "telegram_user_id = 123456789\n"
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /home/user\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    const char *required[] = {
        "telegram_bot_token", "telegram_user_id",
        "claude_binary", "workspace_path"
    };
    char errors[4][RELAY_MAX_VALUE];

    int missing = config_validate(cfg, required, 4, errors, 4);
    TEST_ASSERT_EQUAL_INT(0, missing);

    config_free(cfg);
}

/* ── Test: Validate catches missing required keys ──────────────────── */
static void test_config_validate_missing_keys(void)
{
    const char *text =
        "telegram_bot_token = abc123\n"
        "workspace_path = /home/user\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    const char *required[] = {
        "telegram_bot_token", "telegram_user_id",
        "claude_binary", "workspace_path"
    };
    char errors[4][RELAY_MAX_VALUE];

    int missing = config_validate(cfg, required, 4, errors, 4);
    TEST_ASSERT_EQUAL_INT(2, missing);
    /* Order should match required[] order */
    TEST_ASSERT_EQUAL_STRING("telegram_user_id", errors[0]);
    TEST_ASSERT_EQUAL_STRING("claude_binary", errors[1]);

    config_free(cfg);
}

/* ── Test: Validate with empty config catches all ──────────────────── */
static void test_config_validate_empty_config(void)
{
    config_t *cfg = config_load_string("");
    TEST_ASSERT_NOT_NULL(cfg);

    const char *required[] = { "key1", "key2" };
    char errors[2][RELAY_MAX_VALUE];

    int missing = config_validate(cfg, required, 2, errors, 2);
    TEST_ASSERT_EQUAL_INT(2, missing);

    config_free(cfg);
}

/* ── Test: Validate with NULL config returns -1 ────────────────────── */
static void test_config_validate_null_config(void)
{
    const char *required[] = { "key1" };
    char errors[1][RELAY_MAX_VALUE];

    int missing = config_validate(NULL, required, 1, errors, 1);
    TEST_ASSERT_EQUAL_INT(-1, missing);
}

/* ── Test: Reload scenario — valid new config passes validation ───── */
static void test_config_reload_valid_config(void)
{
    /* Simulate: old config has timeout=60 */
    const char *old_text =
        "telegram_bot_token = abc123\n"
        "telegram_user_id = 123456789\n"
        "claude_binary = /usr/bin/claude\n"
        "claude_timeout = 60\n"
        "workspace_path = /home/user\n";

    /* New config has timeout=300 */
    const char *new_text =
        "telegram_bot_token = abc123\n"
        "telegram_user_id = 123456789\n"
        "claude_binary = /usr/bin/claude\n"
        "claude_timeout = 300\n"
        "workspace_path = /home/user\n";

    config_t *old_cfg = config_load_string(old_text);
    config_t *new_cfg = config_load_string(new_text);
    TEST_ASSERT_NOT_NULL(old_cfg);
    TEST_ASSERT_NOT_NULL(new_cfg);

    /* Validate new config — should pass */
    const char *required[] = {
        "telegram_bot_token", "telegram_user_id",
        "claude_binary", "workspace_path"
    };
    char errors[4][RELAY_MAX_VALUE];
    int missing = config_validate(new_cfg, required, 4, errors, 4);
    TEST_ASSERT_EQUAL_INT(0, missing);

    /* Verify new value is accessible */
    TEST_ASSERT_EQUAL_INT(300,
        config_get_int(new_cfg, "claude_timeout", 0));

    config_free(old_cfg);
    config_free(new_cfg);
}

/* ── Test: Reload scenario — invalid config is rejected ──────────── */
static void test_config_reload_invalid_preserves_old(void)
{
    /* Old config is valid */
    const char *old_text =
        "telegram_bot_token = abc123\n"
        "telegram_user_id = 123456789\n"
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /home/user\n";

    /* New config is missing required keys */
    const char *new_text =
        "telegram_bot_token = new_token\n"
        "some_other_key = whatever\n";

    config_t *old_cfg = config_load_string(old_text);
    config_t *new_cfg = config_load_string(new_text);
    TEST_ASSERT_NOT_NULL(old_cfg);
    TEST_ASSERT_NOT_NULL(new_cfg);

    const char *required[] = {
        "telegram_bot_token", "telegram_user_id",
        "claude_binary", "workspace_path"
    };
    char errors[4][RELAY_MAX_VALUE];

    /* Old config validates fine */
    int old_missing = config_validate(old_cfg, required, 4, errors, 4);
    TEST_ASSERT_EQUAL_INT(0, old_missing);

    /* New config fails validation — 3 missing keys */
    int new_missing = config_validate(new_cfg, required, 4, errors, 4);
    TEST_ASSERT_EQUAL_INT(3, new_missing);

    /* In real reload: we'd free new_cfg and keep old_cfg */
    /* Old config values still intact */
    TEST_ASSERT_EQUAL_STRING("abc123",
        config_get(old_cfg, "telegram_bot_token", ""));
    TEST_ASSERT_EQUAL_STRING("123456789",
        config_get(old_cfg, "telegram_user_id", ""));

    config_free(new_cfg);  /* Rejected config freed */
    config_free(old_cfg);  /* Old config kept until later */
}

/* ── Suite registration ─────────────────────────────────────────────── */
/* Tests for workspace_def_t API (REQ-005) ──────────────────────────── */
static void test_workspace_count_empty(void)
{
    const char *text = "telegram_bot_token = abc123\n";
    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(0, config_get_workspace_count(cfg));
    config_free(cfg);
}

static void test_workspace_get_not_found(void)
{
    const char *text = "telegram_bot_token = abc123\n";
    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_NULL(config_get_workspace(cfg, "home"));
    TEST_ASSERT_NULL(config_get_workspace_by_index(cfg, 0));
    config_free(cfg);
}

/* \u2500\u2500 Tests for workspace block parser (REQ-006) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500 */
static void test_workspace_parse_single_block(void)
{
    const char *text =
        "telegram_bot_token = abc123\n"
        "\n"
        "[workspace \"ea\"]\n"
        "path = /Users/john/EA\n"
        "provider = claude\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(1, config_get_workspace_count(cfg));

    const workspace_def_t *ws = config_get_workspace(cfg, "ea");
    TEST_ASSERT_NOT_NULL(ws);
    TEST_ASSERT_EQUAL_STRING("ea", ws->name);
    TEST_ASSERT_EQUAL_STRING("/Users/john/EA", ws->path);
    TEST_ASSERT_EQUAL_STRING("claude", ws->provider);

    config_free(cfg);
}

static void test_workspace_parse_two_blocks(void)
{
    const char *text =
        "[workspace \"ea\"]\n"
        "path = /Users/john/EA\n"
        "provider = claude\n"
        "\n"
        "[workspace \"myapp\"]\n"
        "path = /Users/john/Code/myapp\n"
        "provider = codex\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(2, config_get_workspace_count(cfg));

    const workspace_def_t *ws0 = config_get_workspace_by_index(cfg, 0);
    TEST_ASSERT_NOT_NULL(ws0);
    TEST_ASSERT_EQUAL_STRING("ea", ws0->name);

    const workspace_def_t *ws1 = config_get_workspace_by_index(cfg, 1);
    TEST_ASSERT_NOT_NULL(ws1);
    TEST_ASSERT_EQUAL_STRING("myapp", ws1->name);
    TEST_ASSERT_EQUAL_STRING("codex", ws1->provider);

    config_free(cfg);
}

static void test_workspace_parse_lookup_missing(void)
{
    const char *text =
        "[workspace \"ea\"]\n"
        "path = /Users/john/EA\n"
        "provider = claude\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_NULL(config_get_workspace(cfg, "nonexistent"));
    TEST_ASSERT_NULL(config_get_workspace_by_index(cfg, 1));
    config_free(cfg);
}

static void test_workspace_tilde_expansion(void)
{
    const char *home = getenv("HOME");
    if (!home) {
        TEST_IGNORE_MESSAGE("HOME not set, skipping tilde expansion test");
        return;
    }

    const char *text =
        "[workspace \"home\"]\n"
        "path = ~/EA\n"
        "provider = claude\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL_INT(1, config_get_workspace_count(cfg));

    const workspace_def_t *ws = config_get_workspace(cfg, "home");
    TEST_ASSERT_NOT_NULL(ws);
    /* Path should have ~ expanded to $HOME */
    TEST_ASSERT_TRUE(ws->path[0] == '/');
    TEST_ASSERT_TRUE(strncmp(ws->path, home, strlen(home)) == 0);

    config_free(cfg);
}

static void test_workspace_max_exceeded(void)
{
    /* Build a config string with RELAY_MAX_WORKSPACES + 1 workspace blocks.
     * The 17th workspace should be silently ignored. */
    char text[8192];
    int pos = 0;
    for (int i = 0; i <= RELAY_MAX_WORKSPACES; i++) {
        pos += snprintf(text + pos, sizeof(text) - (size_t)pos,
                        "[workspace \"ws%d\"]\npath = /tmp/ws%d\nprovider = claude\n\n",
                        i, i);
    }

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);
    /* Should have exactly RELAY_MAX_WORKSPACES, not RELAY_MAX_WORKSPACES+1 */
    TEST_ASSERT_EQUAL_INT(RELAY_MAX_WORKSPACES, config_get_workspace_count(cfg));
    config_free(cfg);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_config_suite(void)
{
    RUN_TEST(test_config_parse_valid);
    RUN_TEST(test_config_missing_key_returns_fallback);
    RUN_TEST(test_config_comments_and_blanks);
    RUN_TEST(test_config_trims_whitespace);
    RUN_TEST(test_config_skips_malformed_lines);
    RUN_TEST(test_config_int_non_numeric);
    RUN_TEST(test_config_empty_string);
    RUN_TEST(test_config_null_string);
    RUN_TEST(test_config_value_with_equals);
    RUN_TEST(test_config_validate_all_present);
    RUN_TEST(test_config_validate_missing_keys);
    RUN_TEST(test_config_validate_empty_config);
    RUN_TEST(test_config_validate_null_config);
    RUN_TEST(test_config_reload_valid_config);
    RUN_TEST(test_config_reload_invalid_preserves_old);
    RUN_TEST(test_workspace_count_empty);
    RUN_TEST(test_workspace_get_not_found);
    RUN_TEST(test_workspace_parse_single_block);
    RUN_TEST(test_workspace_parse_two_blocks);
    RUN_TEST(test_workspace_parse_lookup_missing);
    RUN_TEST(test_workspace_tilde_expansion);
    RUN_TEST(test_workspace_max_exceeded);
}
