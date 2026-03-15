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

/* ── REQ-141: array content + system prefix stripping + cache ──────── */

static void test_discovery_array_content_format(void)
{
    mock_fs_reset();

    /* VS Code IDE format: content is an array of {type, text} objects */
    mock_fs_set(PROJ_DIR "/ide-001.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"Fix the button color\"}"
        "]}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("Fix the button color", results[0].summary);
}

static void test_discovery_strips_system_prefix(void)
{
    mock_fs_reset();

    /* First user message is identity injection (rejected wholesale).
     * Second user message has actual user text. */
    mock_fs_set(PROJ_DIR "/sys-001.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"[Identity context \\u2014 pre-injected]\\n"
        "\\n"
        "## SOUL.md\\n"
        "# SOUL heading\\n"
        "*You're not a chatbot.*\"}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"The actual user message is here\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Should skip the identity message and find the real user text */
    TEST_ASSERT_NOT_NULL(strstr(results[0].summary, "actual user message"));
}

static void test_discovery_strips_command_tags(void)
{
    mock_fs_reset();

    /* Content starts with <command-message> tags */
    mock_fs_set(PROJ_DIR "/cmd-001.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"<command-message>do-work</command-message>\\n"
        "<command-name>/do-work</command-name>\\n"
        "<command-args>start\\nBuild a login form</command-args>\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Should extract "Build a login form" from command-args, not the tags */
    TEST_ASSERT_NOT_NULL(strstr(results[0].summary, "Build a login form"));
}

static void test_discovery_cache_hit(void)
{
    mock_fs_reset();

    /* Set up a cache file with a pre-existing summary and correct version */
    mock_fs_set("/home/tom/.relay-session-cache.json",
        "{\"_version\":3,\"cached-001\":\"Cached summary from previous scan\"}");

    /* .jsonl file exists but its content shouldn't be read if cache hit */
    mock_fs_set(PROJ_DIR "/cached-001.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Wrong text\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("Cached summary from previous scan",
                             results[0].summary);
}

/* ── REQ-144: fix summary extraction ───────────────────────────────── */

static void test_discovery_array_ide_opened_file_stripped(void)
{
    mock_fs_reset();

    /* Array content where the only text item is <ide_opened_file> tag
     * Should extract the filename as summary */
    mock_fs_set(PROJ_DIR "/ide-only.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"<ide_opened_file>The user opened the file /Users/tom/EA/AGENTS.md in the IDE.</ide_opened_file>\"}"
        "]}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Should NOT show the raw tag — should extract filename or show something useful */
    TEST_ASSERT_NULL(strstr(results[0].summary, "<ide_opened_file>"));
}

static void test_discovery_identity_injection_skipped(void)
{
    mock_fs_reset();

    /* All user messages are identity injections — no real user text */
    mock_fs_set(PROJ_DIR "/soul-only.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"[Identity context \\u2014 pre-injected]\\n\\n"
        "## SOUL.md\\n"
        "# SOUL.md - Who You Are\\n\\n"
        "*You're not a chatbot. You're not an assistant.*\"}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":"
        "\"I'll help you set up the project structure.\"}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"[Identity context \\u2014 pre-injected]\\n\\n"
        "## SOUL.md\\n*You're not a chatbot.*\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Should NOT contain SOUL.md content */
    TEST_ASSERT_NULL(strstr(results[0].summary, "chatbot"));
}

static void test_discovery_skips_to_second_user_message(void)
{
    mock_fs_reset();

    /* First user message is system junk, second has real text */
    mock_fs_set(PROJ_DIR "/skip-first.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":["
        "{\"type\":\"text\",\"text\":\"<ide_opened_file>The user opened AGENTS.md</ide_opened_file>\"}"
        "]}}\n"
        "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":\"How can I help?\"}}\n"
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Build a REST API for users\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("Build a REST API for users", results[0].summary);
}

static void test_discovery_cache_version_invalidation(void)
{
    mock_fs_reset();

    /* Old cache without version key — should be ignored and rebuilt */
    mock_fs_set("/home/tom/.relay-session-cache.json",
        "{\"old-session\":\"Stale cached summary\"}");

    mock_fs_set(PROJ_DIR "/old-session.jsonl",
        "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"Fresh content\"}}\n");

    relay_cc_session_t results[10];
    int count = 0;
    session_discovery_scan(&g_mock_fs, "/Users/tom/project",
                           "/home/tom", results, 10, &count);

    TEST_ASSERT_EQUAL_INT(1, count);
    /* Should use fresh extraction, not stale cache */
    TEST_ASSERT_EQUAL_STRING("Fresh content", results[0].summary);
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
    /* REQ-141 */
    RUN_TEST(test_discovery_array_content_format);
    RUN_TEST(test_discovery_strips_system_prefix);
    RUN_TEST(test_discovery_strips_command_tags);
    RUN_TEST(test_discovery_cache_hit);
    /* REQ-144 */
    RUN_TEST(test_discovery_array_ide_opened_file_stripped);
    RUN_TEST(test_discovery_identity_injection_skipped);
    RUN_TEST(test_discovery_skips_to_second_user_message);
    RUN_TEST(test_discovery_cache_version_invalidation);
}
