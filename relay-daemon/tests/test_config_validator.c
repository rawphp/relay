#include "Unity/unity.h"
#include "relay.h"
#include "config.h"
#include "config_validator.h"
#include <string.h>

/* ── Test: Valid configuration passes validation ─────────────────────────── */
static void test_valid_config_no_errors(void)
{
    const char *text =
        "llm_provider = claude\n"
        "session_expiry_hours = 24\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_EQUAL_INT(0, error_count);

    config_free(cfg);
}

/* ── Test: Invalid LLM provider generates error ─────────────────────────── */
static void test_invalid_llm_provider(void)
{
    const char *text = "llm_provider = invalid_provider\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_GREATER_THAN(0, error_count);
    TEST_ASSERT_TRUE(strstr(errors[0], "llm_provider") != NULL);
    TEST_ASSERT_TRUE(strstr(errors[0], "invalid") != NULL);

    config_free(cfg);
}

/* ── Test: Valid OpenAI provider with valid sandbox ─────────────────────────── */
static void test_openai_provider_valid_sandbox(void)
{
    const char *text =
        "llm_provider = openai_codex\n"
        "openai_sandbox = workspace-write\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_EQUAL_INT(0, error_count);

    config_free(cfg);
}

/* ── Test: OpenAI provider with invalid sandbox generates error ─────────────────── */
static void test_openai_provider_invalid_sandbox(void)
{
    const char *text =
        "llm_provider = openai\n"
        "openai_sandbox = invalid_mode\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_GREATER_THAN(0, error_count);
    TEST_ASSERT_TRUE(strstr(errors[0], "openai_sandbox") != NULL);

    config_free(cfg);
}

/* ── Test: Gemini provider with valid settings ─────────────────────────── */
static void test_gemini_provider_valid_settings(void)
{
    const char *text =
        "llm_provider = gemini\n"
        "gemini_enable_sandbox = 1\n"
        "gemini_approval_mode = plan\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_EQUAL_INT(0, error_count);

    config_free(cfg);
}

/* ── Test: Gemini provider with invalid approval mode ─────────────────── */
static void test_gemini_provider_invalid_approval_mode(void)
{
    const char *text =
        "llm_provider = google_gemini\n"
        "gemini_approval_mode = invalid_mode\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_GREATER_THAN(0, error_count);
    TEST_ASSERT_TRUE(strstr(errors[0], "gemini_approval_mode") != NULL);

    config_free(cfg);
}

/* ── Test: Zero session_expiry_hours generates error ─────────────────── */
static void test_zero_session_expiry_hours(void)
{
    const char *text = "session_expiry_hours = 0\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_GREATER_THAN(0, error_count);
    TEST_ASSERT_TRUE(strstr(errors[0], "session_expiry_hours") != NULL);

    config_free(cfg);
}

/* ── Test: Multiple errors collected ─────────────────────────── */
static void test_multiple_validation_errors(void)
{
    const char *text =
        "llm_provider = invalid\n"
        "session_expiry_hours = 0\n";

    config_t *cfg = config_load_string(text);
    TEST_ASSERT_NOT_NULL(cfg);

    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(cfg, errors, 10);

    TEST_ASSERT_EQUAL_INT(2, error_count);
    TEST_ASSERT_TRUE(strstr(errors[0], "llm_provider") != NULL);
    TEST_ASSERT_TRUE(strstr(errors[1], "session_expiry_hours") != NULL);

    config_free(cfg);
}

/* ── Test: NULL config handled gracefully ─────────────────────────── */
static void test_null_config_returns_zero(void)
{
    char errors[10][RELAY_MAX_VALUE];
    int error_count = config_validate_options(NULL, errors, 10);

    TEST_ASSERT_EQUAL_INT(0, error_count);
}

/* ── Test suite ─────────────────────────────────────────────── */
void test_config_validator_suite(void)
{
    RUN_TEST(test_valid_config_no_errors);
    RUN_TEST(test_invalid_llm_provider);
    RUN_TEST(test_openai_provider_valid_sandbox);
    RUN_TEST(test_openai_provider_invalid_sandbox);
    RUN_TEST(test_gemini_provider_valid_settings);
    RUN_TEST(test_gemini_provider_invalid_approval_mode);
    RUN_TEST(test_zero_session_expiry_hours);
    RUN_TEST(test_multiple_validation_errors);
    RUN_TEST(test_null_config_returns_zero);
}
