#include "Unity/unity.h"
#include "openai_codex.h"
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

static int args_contain_pair(const char **args, const char *a, const char *b)
{
    if (!args || !a || !b) {
        return 0;
    }
    for (int i = 0; args[i] != NULL && args[i + 1] != NULL; i++) {
        if (strcmp(args[i], a) == 0 && strcmp(args[i + 1], b) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_openai_codex_parse_valid_jsonl(void)
{
    const char *jsonl =
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-123\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"hello from codex\"}}\n"
        "{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}\n";

    openai_codex_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = openai_codex_parse_response(jsonl, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("thread-123", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("hello from codex", resp.result);
    TEST_ASSERT_EQUAL_INT(0, resp.is_error);
}

static void test_openai_codex_send_exec_and_parse(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-456\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"done\"}}\n");

    const char *cfg_text =
        "openai_binary = /usr/local/bin/codex\n"
        "openai_timeout = 75\n"
        "openai_model = gpt-5-codex\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    openai_codex_t *oa = openai_codex_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(oa);

    openai_codex_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = openai_codex_send(oa, "hello", NULL, NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("thread-456", resp.session_id);
    TEST_ASSERT_EQUAL_STRING("done", resp.result);
    TEST_ASSERT_EQUAL_INT(75, g_mock_proc_last_timeout);

    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "exec"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--json"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--full-auto"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--full-auto"));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "Formatting instructions:"));

    openai_codex_free(oa);
    config_free(cfg);
}

static void test_openai_codex_send_resume_when_session_present(void)
{
    mock_proc_reset();
    mock_proc_set_output(
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-789\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"continued\"}}\n");

    const char *cfg_text =
        "openai_binary = /usr/local/bin/codex\n"
        "openai_timeout = 60\n"
        "openai_model = gpt-5-codex\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    openai_codex_t *oa = openai_codex_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(oa);

    openai_codex_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = openai_codex_send(oa, "next", "thread-789", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "resume"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "--full-auto"));
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "--sandbox"));
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "--cd"));
    TEST_ASSERT_TRUE(args_contain_pair(g_mock_proc_last_args, "resume", "--full-auto"));
    TEST_ASSERT_TRUE(args_contain(g_mock_proc_last_args, "thread-789"));

    openai_codex_free(oa);
    config_free(cfg);
}

static void test_openai_codex_send_with_retry_falls_back_without_resume(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR;
    mock_proc_set_output_after_n_calls(
        1,
        "{\"type\":\"thread.started\",\"thread_id\":\"thread-fresh\"}\n"
        "{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"fresh path\"}}\n");

    const char *cfg_text =
        "openai_binary = /usr/local/bin/codex\n"
        "openai_timeout = 60\n"
        "openai_model = gpt-5-codex\n"
        "openai_retry_count = 3\n"
        "openai_retry_backoff_ms = 1\n"
        "workspace_path = /home/user/workspace\n";

    config_t *cfg = config_load_string(cfg_text);
    openai_codex_t *oa = openai_codex_create(&g_mock_proc, cfg);
    TEST_ASSERT_NOT_NULL(oa);

    openai_codex_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = openai_codex_send_with_retry(oa, "retry", "thread-stale", NULL, &resp);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("fresh path", resp.result);
    TEST_ASSERT_EQUAL_STRING("thread-fresh", resp.session_id);
    TEST_ASSERT_EQUAL_INT(2, mock_proc_call_count());
    TEST_ASSERT_NOT_NULL(g_mock_proc_last_args);
    TEST_ASSERT_FALSE(args_contain(g_mock_proc_last_args, "resume"));

    openai_codex_free(oa);
    config_free(cfg);
}

void test_openai_codex_suite(void)
{
    RUN_TEST(test_openai_codex_parse_valid_jsonl);
    RUN_TEST(test_openai_codex_send_exec_and_parse);
    RUN_TEST(test_openai_codex_send_resume_when_session_present);
    RUN_TEST(test_openai_codex_send_with_retry_falls_back_without_resume);
}
