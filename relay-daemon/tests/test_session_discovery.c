#include "Unity/unity.h"
#include "mocks.h"
#include "session_discovery.h"
#include <string.h>

#define PROJ_DIR "/home/tom/.claude/projects/-Users-tom-project"

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_discovery_happy_path(void)
{
    mock_fs_reset();

    mock_fs_set(PROJ_DIR "/aaa-bbb-ccc.jsonl",
        "{\"type\":\"queue-operation\"}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Fix the login bug\"}}\n");

    mock_fs_set(PROJ_DIR "/ddd-eee-fff.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Add unit tests\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    int rc = session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                                    "/home/tom",
                                    results, 10, &count);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(2, count);
    int found_aaa = 0, found_ddd = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(results[i].session_id, "aaa-bbb-ccc") == 0) {
            found_aaa = 1;
            TEST_ASSERT_EQUAL_STRING("Fix the login bug", results[i].summary);
        }
        if (strcmp(results[i].session_id, "ddd-eee-fff") == 0) {
            found_ddd = 1;
            TEST_ASSERT_EQUAL_STRING("Add unit tests", results[i].summary);
        }
    }
    TEST_ASSERT_TRUE(found_aaa);
    TEST_ASSERT_TRUE(found_ddd);
}

static void test_discovery_empty_directory(void)
{
    mock_fs_reset();

    relay_cc_session_t results[10];
    int count = -1;
    int rc = session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                                    "/home/tom",
                                    results, 10, &count);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, count);
}

static void test_discovery_missing_directory(void)
{
    mock_fs_reset();

    relay_cc_session_t results[10];
    int count = -1;
    int rc = session_discovery_scan(&g_mock_fs, "/nonexistent/path",
                                    "/home/tom",
                                    results, 10, &count);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, count);
}

static void test_discovery_malformed_json_skipped(void)
{
    mock_fs_reset();

    mock_fs_set(PROJ_DIR "/abc-123.jsonl",
        "NOT VALID JSON\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Hello world\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    int rc = session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                                    "/home/tom",
                                    results, 10, &count);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("abc-123", results[0].session_id);
    TEST_ASSERT_EQUAL_STRING("Hello world", results[0].summary);
}

static void test_discovery_no_user_message(void)
{
    mock_fs_reset();

    mock_fs_set(PROJ_DIR "/xyz-789.jsonl",
        "{\"type\":\"queue-operation\"}\n"
        "{\"type\":\"file-history-snapshot\"}\n");

    relay_cc_session_t results[10];
    int count = 0;
    int rc = session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                                    "/home/tom",
                                    results, 10, &count);

    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("xyz-789", results[0].session_id);
    TEST_ASSERT_EQUAL_STRING("", results[0].summary);
}

static void test_discovery_summary_truncated(void)
{
    mock_fs_reset();

    mock_fs_set(PROJ_DIR "/trunc-001.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\""
        "This is a very long message that exceeds eighty characters and should be truncated at the boundary properly"
        "\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom",
                           results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_TRUE(strlen(results[0].summary) <= 80);
}

/* ── Suite ──────────────────────────────────────────────────────────── */

void test_session_discovery_suite(void)
{
    RUN_TEST(test_discovery_happy_path);
    RUN_TEST(test_discovery_empty_directory);
    RUN_TEST(test_discovery_missing_directory);
    RUN_TEST(test_discovery_malformed_json_skipped);
    RUN_TEST(test_discovery_no_user_message);
    RUN_TEST(test_discovery_summary_truncated);
}
