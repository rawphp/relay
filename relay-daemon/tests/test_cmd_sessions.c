#include "Unity/unity.h"
#include "cmd_sessions.h"
#include <string.h>

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_provider_claude_supported(void)
{
    char reply[256] = {0};
    int ok = cmd_sessions_provider_supported("claude", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, ok);
    TEST_ASSERT_EQUAL_STRING("", reply);
}

static void test_provider_default_supported(void)
{
    /* Empty or NULL provider = default = claude */
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

/* ── Suite ──────────────────────────────────────────────────────────── */

void test_cmd_sessions_suite(void)
{
    RUN_TEST(test_provider_claude_supported);
    RUN_TEST(test_provider_default_supported);
    RUN_TEST(test_provider_codex_unsupported);
    RUN_TEST(test_provider_gemini_unsupported);
    RUN_TEST(test_provider_unknown_unsupported);
}
