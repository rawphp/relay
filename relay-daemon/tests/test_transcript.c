#include "Unity/unity.h"
#include "transcript.h"
#include "mocks.h"

#define TEST_TX_DIR TEST_TMP_DIR "/relay_transcripts"

/* ── Test: Log inbound message ──────────────────────────────────────── */
static void test_transcript_log_inbound(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400; /* 2024-02-16 */

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_inbound(tx, "12345", "hello kai");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Check file was written */
    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    TEST_ASSERT_TRUE(g_mock_fs.file_exists(path));

    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"in\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"chat_id\":\"12345\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "hello kai"));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log outbound response (no message_id) ────────────────────── */
static void test_transcript_log_outbound(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_outbound(tx, "12345", "Sure, I can help.",
                                     "sess-abc", 1500, 0);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"out\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"session_id\":\"sess-abc\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"duration_ms\":1500"));
    /* message_id=0: field must NOT appear */
    TEST_ASSERT_NULL(strstr(content, "\"message_id\""));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log outbound response with message_id persisted ──────────── */
static void test_transcript_log_outbound_with_message_id(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_outbound(tx, "12345", "Hello there!",
                                     "sess-x", 800, 42);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"out\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"message_id\":42"));
    TEST_ASSERT_NOT_NULL(strstr(content, "Hello there!"));
    free(content);

    transcript_free(tx);
}

/* ── Test: Find outbound message text by message_id ─────────────────── */
static void test_transcript_find_by_message_id(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* Log two entries: one with message_id=99, one without */
    transcript_log_outbound(tx, "12345", "Specific response text",
                            "sess-y", 500, 99);
    transcript_log_outbound(tx, "12345", "Generic response",
                            "sess-y", 300, 0);

    /* Find the entry with message_id=99 */
    char buf[256];
    int found = transcript_find_by_message_id(tx, 99, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, found);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Specific response text"));

    /* Unknown id → not found */
    int not_found = transcript_find_by_message_id(tx, 999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, not_found);

    /* id=0 is never stored → not found */
    int zero = transcript_find_by_message_id(tx, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, zero);

    transcript_free(tx);
}

/* ── Test: Multiple entries append ──────────────────────────────────── */
static void test_transcript_appends(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    transcript_log_inbound(tx, "12345", "msg1");
    transcript_log_outbound(tx, "12345", "resp1", "s1", 100, 0);
    transcript_log_inbound(tx, "12345", "msg2");

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "msg1"));
    TEST_ASSERT_NOT_NULL(strstr(content, "resp1"));
    TEST_ASSERT_NOT_NULL(strstr(content, "msg2"));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log reaction (no reacted_to text known) ──────────────────── */
static void test_transcript_log_reaction(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_reaction(tx, "12345", "\xf0\x9f\x91\x8d", NULL);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"reaction\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"chat_id\":\"12345\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"emoji\""));
    /* NULL reacted_to: field must NOT appear */
    TEST_ASSERT_NULL(strstr(content, "\"reacted_to\""));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log reaction with reacted_to text ────────────────────────── */
static void test_transcript_log_reaction_with_reacted_to(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_reaction(tx, "12345", "\xf0\x9f\x91\x8d",
                                     "Sure, I can help.");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"reaction\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"reacted_to\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "Sure, I can help."));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log probe outcome (responded) ────────────────────────────── */
static void test_transcript_log_probe_outcome(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_probe_outcome(tx, "slack-workspace",
                                           "1234.5678", "1234.9999",
                                           "responded", 120);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"direction\":\"probe_outcome\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"outcome\":\"responded\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"thread_ts\":\"1234.5678\""));
    TEST_ASSERT_NOT_NULL(strstr(content, "\"response_time_sec\":120"));
    free(content);

    transcript_free(tx);
}

/* ── Test: Log probe outcome (ignored, no response_time) ────────────── */
static void test_transcript_log_probe_outcome_ignored(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    int rc = transcript_log_probe_outcome(tx, "slack-workspace",
                                           "1234.5678", "1234.9999",
                                           "ignored", -1);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    const char *path = TEST_TX_DIR "/2024-02-16.jsonl";
    char *content = g_mock_fs.read_file(path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "\"outcome\":\"ignored\""));
    TEST_ASSERT_NULL(strstr(content, "\"response_time_sec\""));
    free(content);

    transcript_free(tx);
}

/* ── Test: Read recent entries — happy path ─────────────────────────── */
static void test_transcript_read_recent_happy_path(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* Log a few exchanges */
    transcript_log_inbound(tx, "12345", "hello");
    transcript_log_outbound(tx, "12345", "hi there", "s1", 100, 0);
    transcript_log_inbound(tx, "12345", "how are you?");
    transcript_log_outbound(tx, "12345", "I'm doing well!", "s1", 200, 0);

    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int rc = transcript_read_recent(tx, "12345", 10, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Should contain all 4 entries formatted */
    TEST_ASSERT_NOT_NULL(strstr(buf, "hello"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "hi there"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "how are you?"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "I'm doing well!"));

    transcript_free(tx);
}

/* ── Test: Read recent entries — empty transcript ──────────────────── */
static void test_transcript_read_recent_empty(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* No entries logged */
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int rc = transcript_read_recent(tx, "12345", 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("", buf);

    transcript_free(tx);
}

/* ── Test: Read recent entries — fewer entries than requested ────────── */
static void test_transcript_read_recent_fewer_than_max(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* Only 2 entries */
    transcript_log_inbound(tx, "12345", "just one");
    transcript_log_outbound(tx, "12345", "got it", "s1", 100, 0);

    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int rc = transcript_read_recent(tx, "12345", 20, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "just one"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "got it"));

    transcript_free(tx);
}

/* ── Test: Read recent entries — filters by chat_id ─────────────────── */
static void test_transcript_read_recent_filters_chat_id(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* Mix of chat IDs */
    transcript_log_inbound(tx, "AAA", "from AAA");
    transcript_log_inbound(tx, "BBB", "from BBB");
    transcript_log_outbound(tx, "AAA", "reply to AAA", "s1", 100, 0);

    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int rc = transcript_read_recent(tx, "AAA", 10, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_NOT_NULL(strstr(buf, "from AAA"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "reply to AAA"));
    TEST_ASSERT_NULL(strstr(buf, "from BBB"));

    transcript_free(tx);
}

/* ── Test: Read recent respects max_entries limit ───────────────────── */
static void test_transcript_read_recent_max_entries(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    transcript_t *tx = transcript_create(&g_mock_fs, &g_mock_clock,
                                          TEST_TX_DIR);
    TEST_ASSERT_NOT_NULL(tx);

    /* Log 6 entries for the same chat */
    transcript_log_inbound(tx, "12345", "msg1");
    transcript_log_outbound(tx, "12345", "resp1", "s1", 100, 0);
    transcript_log_inbound(tx, "12345", "msg2");
    transcript_log_outbound(tx, "12345", "resp2", "s1", 100, 0);
    transcript_log_inbound(tx, "12345", "msg3");
    transcript_log_outbound(tx, "12345", "resp3", "s1", 100, 0);

    /* Request only last 2 entries */
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int rc = transcript_read_recent(tx, "12345", 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Should have the last 2 (msg3, resp3) but NOT msg1/resp1 */
    TEST_ASSERT_NOT_NULL(strstr(buf, "msg3"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "resp3"));
    TEST_ASSERT_NULL(strstr(buf, "msg1"));
    TEST_ASSERT_NULL(strstr(buf, "resp1"));

    transcript_free(tx);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_transcript_suite(void)
{
    RUN_TEST(test_transcript_log_inbound);
    RUN_TEST(test_transcript_log_outbound);
    RUN_TEST(test_transcript_log_outbound_with_message_id);
    RUN_TEST(test_transcript_find_by_message_id);
    RUN_TEST(test_transcript_appends);
    RUN_TEST(test_transcript_log_reaction);
    RUN_TEST(test_transcript_log_reaction_with_reacted_to);
    RUN_TEST(test_transcript_log_probe_outcome);
    RUN_TEST(test_transcript_log_probe_outcome_ignored);
    RUN_TEST(test_transcript_read_recent_happy_path);
    RUN_TEST(test_transcript_read_recent_empty);
    RUN_TEST(test_transcript_read_recent_fewer_than_max);
    RUN_TEST(test_transcript_read_recent_filters_chat_id);
    RUN_TEST(test_transcript_read_recent_max_entries);
}
