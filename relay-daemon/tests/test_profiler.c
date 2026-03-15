#include "Unity/unity.h"
#include "profiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_profiler_init_disabled(void)
{
    const char *path = TEST_TMP_DIR "/relay_profiler_disabled.jsonl";
    unlink(path);

    int rc = profiler_init(path, 0);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, profiler_enabled());

    profiler_emit_event_ctx("req", "chat", "claude", "request.total", 10, "ok", "detail");

    FILE *f = fopen(path, "r");
    TEST_ASSERT_NULL(f);
    profiler_close();
}

static void test_profiler_write_event_with_context(void)
{
    const char *path = TEST_TMP_DIR "/relay_profiler_enabled.jsonl";
    unlink(path);

    int rc = profiler_init(path, 1);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, profiler_enabled());

    profiler_set_context("req-1", "chat-1", "claude");
    profiler_emit_event("llm.request", 123, "ok", "streaming");
    profiler_clear_context();
    profiler_close();

    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(f);

    char line[1024];
    char *got = fgets(line, sizeof(line), f);
    fclose(f);
    unlink(path);

    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_NOT_NULL(strstr(line, "\"request_id\":\"req-1\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"chat_id\":\"chat-1\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"provider\":\"claude\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"stage\":\"llm.request\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"duration_ms\":123"));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"status\":\"ok\""));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"detail\":\"streaming\""));
}

static void test_profiler_creates_parent_dirs(void)
{
    const char *dir = TEST_TMP_DIR "/relay_profiler_nested";
    const char *path = TEST_TMP_DIR "/relay_profiler_nested/deep/profile.jsonl";
    unlink(path);
    rmdir(TEST_TMP_DIR "/relay_profiler_nested/deep");
    rmdir(dir);

    int rc = profiler_init(path, 1);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    profiler_emit_event_ctx("req", "chat", "claude", "request.total", 1, "ok", "nested");
    profiler_close();

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    unlink(path);
    rmdir(TEST_TMP_DIR "/relay_profiler_nested/deep");
    rmdir(dir);
}

static void test_profiler_timer_elapsed_non_negative(void)
{
    profiler_timer_t timer;
    profiler_timer_start(&timer);
    long ms = profiler_timer_elapsed_ms(&timer);
    TEST_ASSERT_TRUE(ms >= 0);
}

void test_profiler_suite(void)
{
    RUN_TEST(test_profiler_init_disabled);
    RUN_TEST(test_profiler_write_event_with_context);
    RUN_TEST(test_profiler_creates_parent_dirs);
    RUN_TEST(test_profiler_timer_elapsed_non_negative);
}
