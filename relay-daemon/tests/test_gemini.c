#include "Unity/unity.h"
#include "gemini.h"
#include "mocks.h"
#include <string.h>

static int args_contain(const char **args, const char *needle)
{
    if (!args || !needle) {
        return 0;
    }
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], needle) == 0) {
            return 1;
        }
    }
    return 0;
}

static int args_contain_pair(const char **args, const char *first, const char *second)
{
    if (!args || !first || !second) {
        return 0;
    }
    for (int i = 0; args[i] != NULL && args[i + 1] != NULL; i++) {
        if (strcmp(args[i], first) == 0 && strcmp(args[i + 1], second) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *arg_value(const char **args, const char *flag)
{
    if (!args || !flag) {
        return NULL;
    }
    for (int i = 0; args[i] != NULL && args[i + 1] != NULL; i++) {
        if (strcmp(args[i], flag) == 0) {
            return args[i + 1];
        }
    }
    return NULL;
}

static void test_gemini_send_basic(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"session_id\":\"session-42\",\"response\":\"ok\"}\n");

    const char *cfg_text =
        "gemini_binary = /usr/local/bin/gemini\n"
        "gemini_timeout = 90\n"
        "gemini_model = gemini-2.5-flash\n"
        "gemini_enable_sandbox = 1\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    gemini_t *gm = gemini_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(gm);

    gemini_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = gemini_send(gm, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("session-42", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("ok", resp.result);
    TEST_ASSERT_FALSE(resp.is_error);
    TEST_ASSERT_EQUAL_INT(90, g_mock_proc_last_timeout);

    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--prompt"));
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "--output-format", "json"));
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "--model", "gemini-2.5-flash"));
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "--include-directories", "/home/user/workspace"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--sandbox"));
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "--approval-mode", "plan"));
    const char *prompt_arg = arg_value(g_mock_proc_last_args, "--prompt");
    TEST_ASSERT_NOT_NULL(prompt_arg);
    TEST_ASSERT_NOT_NULL(strstr(prompt_arg, "Formatting instructions:"));

    gemini_free(gm);
    config_free(cfg);
}

static void test_gemini_send_resume_without_sandbox(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"session_id\":\"session-77\",\"response\":\"continued\"}\n");

    const char *cfg_text =
        "gemini_binary = /usr/local/bin/gemini\n"
        "gemini_timeout = 45\n"
        "gemini_model = gemini-2.5-pro\n"
        "gemini_enable_sandbox = 0\n"
        "gemini_approval_mode = none\n"
        "workspace_path = /workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    gemini_t *gm = gemini_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(gm);

    gemini_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = gemini_send(gm, "resume please", "session-10", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("session-77", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("continued", resp.result);

    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "--resume", "session-10"));
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "--sandbox"));
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "--approval-mode"));

    gemini_free(gm);
    config_free(cfg);
}

static void test_gemini_send_reports_json_error(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"session_id\":\"err\",\"error\":{\"message\":\"auth fail\"}}\n");

    const char *cfg_text =
        "gemini_binary = gemini\n"
        "workspace_path = /workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    gemini_t *gm = gemini_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(gm);

    gemini_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = gemini_send(gm, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_TRUE(resp.is_error);
    TEST_ASSERT_EQUAL_STRING("auth fail", resp.result);

    gemini_free(gm);
    config_free(cfg);
}

static void test_gemini_send_parses_json_with_preamble(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "Loaded cached credentials.\n"
        "{\n"
        "  \"session_id\": \"session-99\",\n"
        "  \"response\": \"ready\"\n"
        "}\n");

    const char *cfg_text =
        "gemini_binary = gemini\n"
        "workspace_path = /workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    gemini_t *gm = gemini_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(gm);

    gemini_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = gemini_send(gm, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("session-99", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("ready", resp.result);
    TEST_ASSERT_FALSE(resp.is_error);

    gemini_free(gm);
    config_free(cfg);
}

static void test_gemini_send_with_retry_drops_resume(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR;
    mock_proc_set_output_after_n_calls(1,
        "{\"session_id\":\"fresh\",\"response\":\"ok\"}\n");

    const char *cfg_text =
        "gemini_binary = gemini\n"
        "gemini_timeout = 30\n"
        "workspace_path = /workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    TEST_ASSERT_NOT_NULL(cfg);

    gemini_t *gm = gemini_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(gm);

    gemini_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = gemini_send_with_retry(gm, "retry", "stale-session", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("fresh", resp.session_id);
    TEST_ASSERT_EQUAL_INT(2, mock_proc_call_count());
    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "--resume"));

    gemini_free(gm);
    config_free(cfg);
}

void test_gemini_suite(void)
{
    RUN_TEST(test_gemini_send_basic);
    RUN_TEST(test_gemini_send_resume_without_sandbox);
    RUN_TEST(test_gemini_send_reports_json_error);
    RUN_TEST(test_gemini_send_parses_json_with_preamble);
    RUN_TEST(test_gemini_send_with_retry_drops_resume);
}
