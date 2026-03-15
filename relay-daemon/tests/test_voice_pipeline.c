#include "Unity/unity.h"
#include "mocks.h"
#include "voice_pipeline.h"
#include <string.h>

/* ── Tests: voice_pipeline_build_args ──────────────────────────────── */

static void test_build_args_happy_path(void)
{
    const char *argv[VOICE_PIPELINE_MAX_ARGS];
    char bin_buf[RELAY_MAX_PATH];
    char cfg_buf[RELAY_MAX_PATH];

    int rc = voice_pipeline_build_args("/home/testuser", "12345", "/tmp/voice.ogg",
                                        argv, VOICE_PIPELINE_MAX_ARGS,
                                        bin_buf, sizeof(bin_buf),
                                        cfg_buf, sizeof(cfg_buf));

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* argv[0] = script path */
    TEST_ASSERT_EQUAL_STRING(
        "/home/testuser/relay/lib/voice/voice_daemon_bridge.py", argv[0]);
    /* argv[1..2] = --config <path> */
    TEST_ASSERT_EQUAL_STRING("--config", argv[1]);
    TEST_ASSERT_EQUAL_STRING(
        "/home/testuser/relay/config/voice.json", argv[2]);
    /* argv[3] = subcommand */
    TEST_ASSERT_EQUAL_STRING("process-audio-telegram", argv[3]);
    /* argv[4] = chat_id */
    TEST_ASSERT_EQUAL_STRING("12345", argv[4]);
    /* argv[5] = audio_path */
    TEST_ASSERT_EQUAL_STRING("/tmp/voice.ogg", argv[5]);
    /* argv[6] = NULL terminator */
    TEST_ASSERT_NULL(argv[6]);
}

static void test_build_args_null_home(void)
{
    const char *argv[VOICE_PIPELINE_MAX_ARGS];
    char bin_buf[RELAY_MAX_PATH];
    char cfg_buf[RELAY_MAX_PATH];

    int rc = voice_pipeline_build_args(NULL, "12345", "/tmp/voice.ogg",
                                        argv, VOICE_PIPELINE_MAX_ARGS,
                                        bin_buf, sizeof(bin_buf),
                                        cfg_buf, sizeof(cfg_buf));

    TEST_ASSERT_EQUAL_INT(RELAY_ERR_INVALID, rc);
}

static void test_build_args_null_chat_id(void)
{
    const char *argv[VOICE_PIPELINE_MAX_ARGS];
    char bin_buf[RELAY_MAX_PATH];
    char cfg_buf[RELAY_MAX_PATH];

    int rc = voice_pipeline_build_args("/home/user", NULL, "/tmp/voice.ogg",
                                        argv, VOICE_PIPELINE_MAX_ARGS,
                                        bin_buf, sizeof(bin_buf),
                                        cfg_buf, sizeof(cfg_buf));

    TEST_ASSERT_EQUAL_INT(RELAY_ERR_INVALID, rc);
}

static void test_build_args_null_audio_path(void)
{
    const char *argv[VOICE_PIPELINE_MAX_ARGS];
    char bin_buf[RELAY_MAX_PATH];
    char cfg_buf[RELAY_MAX_PATH];

    int rc = voice_pipeline_build_args("/home/user", "12345", NULL,
                                        argv, VOICE_PIPELINE_MAX_ARGS,
                                        bin_buf, sizeof(bin_buf),
                                        cfg_buf, sizeof(cfg_buf));

    TEST_ASSERT_EQUAL_INT(RELAY_ERR_INVALID, rc);
}

/* ── Tests: voice_pipeline_run ─────────────────────────────────────── */

static void test_run_happy_path(void)
{
    mock_proc_reset();
    mock_proc_set_output("{\"status\":\"success\",\"transcription\":\"hello\"}");

    char output[8192];
    int rc = voice_pipeline_run("/home/user", "12345", "/tmp/voice.ogg",
                                 &g_mock_proc, output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_NOT_NULL(strstr(output, "success"));
    /* Verify mock proc was called (not popen) */
    TEST_ASSERT_EQUAL_INT(1, mock_proc_call_count());
}

static void test_run_null_home_returns_error(void)
{
    mock_proc_reset();
    char output[8192];
    int rc = voice_pipeline_run(NULL, "12345", "/tmp/voice.ogg",
                                 &g_mock_proc, output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(RELAY_ERR_INVALID, rc);
    /* proc should NOT have been called */
    TEST_ASSERT_EQUAL_INT(0, mock_proc_call_count());
}

static void test_run_proc_failure(void)
{
    mock_proc_reset();
    g_mock_proc_status = RELAY_ERR;

    char output[8192];
    int rc = voice_pipeline_run("/home/user", "12345", "/tmp/voice.ogg",
                                 &g_mock_proc, output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
}

/* ── Suite ─────────────────────────────────────────────────────────── */

void test_voice_pipeline_suite(void)
{
    RUN_TEST(test_build_args_happy_path);
    RUN_TEST(test_build_args_null_home);
    RUN_TEST(test_build_args_null_chat_id);
    RUN_TEST(test_build_args_null_audio_path);
    RUN_TEST(test_run_happy_path);
    RUN_TEST(test_run_null_home_returns_error);
    RUN_TEST(test_run_proc_failure);
}
