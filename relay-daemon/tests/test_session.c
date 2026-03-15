#include "Unity/unity.h"
#include "session.h"
#include "mocks.h"

#define TEST_SESSION_PATH TEST_TMP_DIR "/test_sessions.json"

/* ── Test: Store and retrieve session ───────────────────────────────── */
static void test_session_store_and_retrieve(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    /* No session initially */
    TEST_ASSERT_NULL(session_get(store, "12345"));

    /* Set a session */
    TEST_ASSERT_EQUAL_INT(RELAY_OK,
        session_set(store, "12345", "sess-abc-123"));

    /* Retrieve it */
    const char *sid = session_get(store, "12345");
    TEST_ASSERT_NOT_NULL(sid);
    TEST_ASSERT_EQUAL_STRING("sess-abc-123", sid);

    session_free(store);
}

/* ── Test: Multiple sessions ────────────────────────────────────────── */
static void test_session_multiple(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "user1", "sess-1");
    session_set(store, "user2", "sess-2");
    session_set(store, "user3", "sess-3");

    TEST_ASSERT_EQUAL_STRING("sess-1", session_get(store, "user1"));
    TEST_ASSERT_EQUAL_STRING("sess-2", session_get(store, "user2"));
    TEST_ASSERT_EQUAL_STRING("sess-3", session_get(store, "user3"));

    session_free(store);
}

/* ── Test: Update existing session ──────────────────────────────────── */
static void test_session_update(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "12345", "old-session");
    session_set(store, "12345", "new-session");

    TEST_ASSERT_EQUAL_STRING("new-session", session_get(store, "12345"));

    session_free(store);
}

/* ── Test: Persist and reload ───────────────────────────────────────── */
static void test_session_persist_and_reload(void)
{
    mock_fs_reset();

    /* Create and populate */
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "12345", "sess-abc");
    session_free(store);

    /* Reload from persisted file */
    session_store_t *store2 = session_create(&g_mock_fs, &g_mock_clock,
                                              TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store2);

    const char *sid = session_get(store2, "12345");
    TEST_ASSERT_NOT_NULL(sid);
    TEST_ASSERT_EQUAL_STRING("sess-abc", sid);

    session_free(store2);
}

/* ── Test: Clear removes session ────────────────────────────────────── */
static void test_session_clear_removes(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "12345", "sess-abc");
    TEST_ASSERT_NOT_NULL(session_get(store, "12345"));

    session_clear(store, "12345");
    TEST_ASSERT_NULL(session_get(store, "12345"));

    session_free(store);
}

/* ── Test: Expire old sessions ──────────────────────────────────────── */
static void test_session_expiry(void)
{
    mock_fs_reset();

    /* Set initial time */
    g_mock_time = 1000000;

    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 1);
    TEST_ASSERT_NOT_NULL(store);

    /* Add session at t=1000000 */
    session_set(store, "12345", "sess-old");

    /* Advance time by 2 hours (past 1 hour expiry) */
    g_mock_time = 1000000 + 7200;

    session_expire(store);

    TEST_ASSERT_NULL(session_get(store, "12345"));

    /* Reset mock time */
    g_mock_time = 1708070400;

    session_free(store);
}

/* ── Test: Corrupt file handled gracefully ──────────────────────────── */
static void test_session_corrupt_file(void)
{
    mock_fs_reset();
    mock_fs_set(TEST_SESSION_PATH, "this is not valid json{{{");

    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    /* Should still create — just starts empty */
    TEST_ASSERT_NOT_NULL(store);
    TEST_ASSERT_NULL(session_get(store, "anything"));

    session_free(store);
}

/* ── Test: Empty store creates fine ─────────────────────────────────── */
static void test_session_empty_store(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);
    TEST_ASSERT_NULL(session_get(store, "nonexistent"));
    session_free(store);
}

/* ── Test: last_used tracking ───────────────────────────────────────── */
static void test_session_last_used(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "12345", "sess-abc");

    time_t last = session_last_used(store, "12345");
    TEST_ASSERT_EQUAL(1708070400, last);

    /* Non-existent returns 0 */
    TEST_ASSERT_EQUAL(0, session_last_used(store, "nonexistent"));

    session_free(store);
}

/* ── Test: token estimation ─────────────────────────────────────────── */
static void test_session_estimate_tokens(void)
{
    /* Word-count × 1.3 heuristic: (words * 13) / 10, integer division */

    /* Edge cases */
    TEST_ASSERT_EQUAL(0, session_estimate_tokens(NULL));
    TEST_ASSERT_EQUAL(0, session_estimate_tokens(""));

    /* 1 word → (1 * 13) / 10 = 1 */
    TEST_ASSERT_EQUAL(1, session_estimate_tokens("hello"));

    /* 2 words → (2 * 13) / 10 = 2 */
    TEST_ASSERT_EQUAL(2, session_estimate_tokens("hello world"));

    /* 9-word pangram → (9 * 13) / 10 = 11 */
    /* chars/4 would give 43/4=10 — verifies we're using word-count */
    TEST_ASSERT_EQUAL(11, session_estimate_tokens(
        "The quick brown fox jumps over the lazy dog"));

    /* Single long word (20 chars): chars/4=5, word-count=1 */
    TEST_ASSERT_EQUAL(1, session_estimate_tokens("supercalifragilistic"));

    /* 10 single-char words: chars/4=4, word-count → (10*13)/10=13 */
    TEST_ASSERT_EQUAL(13, session_estimate_tokens("a b c d e f g h i j"));
}

/* ── Test: estimate_tokens punctuation + safety multiplier ─────────── */
static void test_session_estimate_tokens_punct_and_safety(void)
{
    /* Punctuation chars each add 1 token */

    /* "Hi, there!" → 2 words, 2 puncts → (2*13)/10 + 2 = 2 + 2 = 4 */
    TEST_ASSERT_EQUAL(4, session_estimate_tokens("Hi, there!"));

    /* "...!!!" → 0 words, 6 puncts → 0 + 6 = 6 */
    TEST_ASSERT_EQUAL(6, session_estimate_tokens("...!!!"));

    /* Long message (>2000 chars): safety multiplier ×1.2 applied */
    /* Fill with "ab " × 700 → 700 words, strlen=2100 */
    /* base: (700*13)/10 = 910; with safety: 910*12/10 = 1092 */
    char long_msg[2101];
    for (int i = 0; i < 700; i++) {
        long_msg[i * 3 + 0] = 'a';
        long_msg[i * 3 + 1] = 'b';
        long_msg[i * 3 + 2] = ' ';
    }
    long_msg[2100] = '\0';
    TEST_ASSERT_EQUAL(1092, session_estimate_tokens(long_msg));
}

/* ── Test: token tracking ───────────────────────────────────────────── */
static void test_session_token_tracking(void)
{
    mock_fs_reset();
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    /* Create session */
    session_set(store, "12345", "sess-abc");

    /* Initial count is 0 */
    TEST_ASSERT_EQUAL(0, session_get_context_tokens(store, "12345"));

    /* Add tokens */
    session_add_tokens(store, "12345", 1000);
    TEST_ASSERT_EQUAL(1000, session_get_context_tokens(store, "12345"));

    /* Add more tokens */
    session_add_tokens(store, "12345", 500);
    TEST_ASSERT_EQUAL(1500, session_get_context_tokens(store, "12345"));

    /* Non-existent chat returns 0 */
    TEST_ASSERT_EQUAL(0, session_get_context_tokens(store, "nonexistent"));

    session_free(store);
}

/* ── Test: token persistence ────────────────────────────────────────── */
static void test_session_token_persistence(void)
{
    mock_fs_reset();

    /* Create and add tokens */
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    session_set(store, "12345", "sess-abc");
    session_add_tokens(store, "12345", 5000);
    session_free(store);

    /* Reload and check tokens persisted */
    session_store_t *store2 = session_create(&g_mock_fs, &g_mock_clock,
                                              TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store2);

    TEST_ASSERT_EQUAL(5000, session_get_context_tokens(store2, "12345"));

    session_free(store2);
}

/* ── Test: deferred persistence with flush ──────────────────────────── */
static void test_session_deferred_persistence(void)
{
    mock_fs_reset();

    /* Create store */
    session_store_t *store = session_create(&g_mock_fs, &g_mock_clock,
                                             TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store);

    /* Set session and add tokens (deferred - not persisted yet) */
    session_set(store, "12345", "sess-abc");
    session_add_tokens(store, "12345", 1000);

    /* Manually flush to persist */
    TEST_ASSERT_EQUAL_INT(RELAY_OK, session_flush(store));

    /* Second flush should be no-op (not dirty) */
    TEST_ASSERT_EQUAL_INT(RELAY_OK, session_flush(store));

    session_free(store);

    /* Reload and verify data was persisted */
    session_store_t *store2 = session_create(&g_mock_fs, &g_mock_clock,
                                              TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(store2);

    TEST_ASSERT_EQUAL_STRING("sess-abc", session_get(store2, "12345"));
    TEST_ASSERT_EQUAL(1000, session_get_context_tokens(store2, "12345"));

    session_free(store2);
}

/* ── Test: memory flush detection ───────────────────────────────────── */
static void test_session_needs_memory_flush(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");

    /* Below threshold - no flush needed */
    session_add_tokens(s, "chat1", 100000);
    TEST_ASSERT_FALSE(session_needs_memory_flush(s, "chat1", 176000));

    /* Above threshold - flush needed */
    session_add_tokens(s, "chat1", 80000);
    TEST_ASSERT_TRUE(session_needs_memory_flush(s, "chat1", 176000));

    /* Mark as flushed - no longer needed */
    session_mark_memory_flushed(s, "chat1");
    TEST_ASSERT_FALSE(session_needs_memory_flush(s, "chat1", 176000));

    session_free(s);
}

/* ── Test: memory flush marking ─────────────────────────────────────── */
static void test_session_mark_memory_flushed(void)
{
    mock_fs_reset();
    g_mock_time = 1708070400;

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");
    session_add_tokens(s, "chat1", 180000);

    /* Initially needs flush */
    TEST_ASSERT_TRUE(session_needs_memory_flush(s, "chat1", 176000));

    /* Mark as flushed */
    TEST_ASSERT_EQUAL_INT(RELAY_OK, session_mark_memory_flushed(s, "chat1"));

    /* No longer needs flush */
    TEST_ASSERT_FALSE(session_needs_memory_flush(s, "chat1", 176000));

    /* Marking non-existent chat fails */
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, session_mark_memory_flushed(s, "nonexistent"));

    session_free(s);
}

/* ── Test: flush after compaction cycle ─────────────────────────────── */
static void test_session_flush_after_compaction(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");
    session_add_tokens(s, "chat1", 180000);

    /* Flush at 180K */
    session_mark_memory_flushed(s, "chat1");

    /* After flushing for cycle 0, same threshold should NOT trigger new flush */
    session_add_tokens(s, "chat1", 10000);  /* Now at 190K */
    /* memory_flush_compaction_count is 0, compaction_count is 0 */
    /* So flush should NOT be needed (already flushed for cycle 0) */
    TEST_ASSERT_FALSE(session_needs_memory_flush(s, "chat1", 176000));

    session_free(s);
}

/* ── Suite registration ─────────────────────────────────────────────── */
/* Tests for workspace fields (REQ-007) ─────────────────────────────── */
static void test_session_workspace_set_get(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");

    session_set_workspace(s, "chat1", "ea", "/Users/john/EA", "claude");

    TEST_ASSERT_EQUAL_STRING("ea",
        session_get_workspace_name(s, "chat1"));
    TEST_ASSERT_EQUAL_STRING("/Users/john/EA",
        session_get_workspace_path(s, "chat1"));
    TEST_ASSERT_EQUAL_STRING("claude",
        session_get_workspace_provider(s, "chat1"));

    session_free(s);
}

static void test_session_workspace_persists(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");
    session_set_workspace(s, "chat1", "myapp", "/Users/john/Code/myapp",
                          "codex");
    session_flush(s);
    session_free(s);

    /* Reload */
    session_store_t *s2 = session_create(&g_mock_fs, &g_mock_clock,
                                          TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_EQUAL_STRING("myapp",
        session_get_workspace_name(s2, "chat1"));
    TEST_ASSERT_EQUAL_STRING("/Users/john/Code/myapp",
        session_get_workspace_path(s2, "chat1"));
    TEST_ASSERT_EQUAL_STRING("codex",
        session_get_workspace_provider(s2, "chat1"));
    session_free(s2);
}

static void test_session_workspace_backward_compat(void)
{
    /* Old JSON without workspace fields should load cleanly */
    mock_fs_reset();
    const char *old_json =
        "{\"sessions\":[{\"chat_id\":\"chat1\",\"session_id\":\"sess1\","
        "\"last_used\":1000,\"context_tokens\":0,\"compaction_count\":0,"
        "\"memory_flush_at\":0,\"memory_flush_compaction_count\":0}]}";
    mock_fs_set(TEST_SESSION_PATH, old_json);

    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    /* Workspace fields should be empty strings, not NULL */
    TEST_ASSERT_EQUAL_STRING("",
        session_get_workspace_name(s, "chat1"));
    session_free(s);
}

/* ── Tests for active workspace tracking (REQ-008) ──────────────────── */
static void test_session_active_workspace_default(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    /* No active workspace set yet — should return empty string */
    TEST_ASSERT_EQUAL_STRING("",
        session_get_active_workspace(s, "chat1"));
    session_free(s);
}

static void test_session_active_workspace_set_get(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set_active_workspace(s, "chat1", "ea");
    TEST_ASSERT_EQUAL_STRING("ea",
        session_get_active_workspace(s, "chat1"));
    session_free(s);
}

static void test_session_get_for_workspace(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    /* Create a session tagged with workspace "ea" */
    session_set(s, "chat1", "sess-ea");
    session_set_workspace(s, "chat1", "ea", "/Users/john/EA", "claude");

    const session_entry_t *e =
        session_get_for_workspace(s, "chat1", "ea");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("sess-ea", e->session_id);

    TEST_ASSERT_NULL(
        session_get_for_workspace(s, "chat1", "nonexistent"));
    session_free(s);
}

static void test_session_list_for_chat(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess1");
    session_set_workspace(s, "chat1", "ea", "/Users/john/EA", "claude");

    const session_entry_t *out[8];
    int count = session_list_for_chat(s, "chat1", out, 8);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("sess1", out[0]->session_id);
    session_free(s);
}

static void test_session_close_workspace(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "chat1", "sess-ea");
    session_set_workspace(s, "chat1", "ea", "/Users/john/EA", "claude");

    session_close_workspace(s, "chat1", "ea");
    TEST_ASSERT_NULL(session_get_for_workspace(s, "chat1", "ea"));
    session_free(s);
}


/* ── Test: Active workspace persists across reload (REQ-042) ─────────── */
static void test_session_active_workspace_persists(void)
{
    mock_fs_reset();

    /* Set active workspace and free (triggers flush) */
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set_active_workspace(s, "chat1", "life");
    session_free(s);

    /* Reload from persisted file */
    session_store_t *s2 = session_create(&g_mock_fs, &g_mock_clock,
                                          TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_EQUAL_STRING("life",
        session_get_active_workspace(s2, "chat1"));
    session_free(s2);
}


static void test_session_active_workspace_update_persists(void)
{
    mock_fs_reset();

    /* Set then update active workspace */
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set_active_workspace(s, "chat1", "main");
    session_set_active_workspace(s, "chat1", "life");  /* update */
    session_free(s);

    /* Reload and verify updated value persisted */
    session_store_t *s2 = session_create(&g_mock_fs, &g_mock_clock,
                                          TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_EQUAL_STRING("life",
        session_get_active_workspace(s2, "chat1"));
    session_free(s2);
}


/* ── Test: session_get_if_workspace_matches (REQ-043) ───────────────── */
static void test_session_get_if_workspace_matches_hit(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "claude:chat1", "sess-main");
    session_set_workspace(s, "claude:chat1", "main", "/Users/john/EA", "claude");

    /* Same workspace — should return session_id */
    const char *sid = session_get_if_workspace_matches(s, "claude:chat1", "main");
    TEST_ASSERT_NOT_NULL(sid);
    TEST_ASSERT_EQUAL_STRING("sess-main", sid);

    session_free(s);
}

static void test_session_get_if_workspace_matches_miss(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);
    session_set(s, "claude:chat1", "sess-main");
    session_set_workspace(s, "claude:chat1", "main", "/Users/john/EA", "claude");

    /* Different workspace — should return NULL (start fresh) */
    const char *sid = session_get_if_workspace_matches(s, "claude:chat1", "life");
    TEST_ASSERT_NULL(sid);

    session_free(s);
}

static void test_session_get_if_workspace_matches_no_session(void)
{
    mock_fs_reset();
    session_store_t *s = session_create(&g_mock_fs, &g_mock_clock,
                                         TEST_SESSION_PATH, 24);
    TEST_ASSERT_NOT_NULL(s);

    /* No session at all — should return NULL */
    const char *sid = session_get_if_workspace_matches(s, "claude:chat1", "main");
    TEST_ASSERT_NULL(sid);

    session_free(s);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_session_suite(void)
{
    RUN_TEST(test_session_store_and_retrieve);
    RUN_TEST(test_session_multiple);
    RUN_TEST(test_session_update);
    RUN_TEST(test_session_persist_and_reload);
    RUN_TEST(test_session_clear_removes);
    RUN_TEST(test_session_expiry);
    RUN_TEST(test_session_corrupt_file);
    RUN_TEST(test_session_empty_store);
    RUN_TEST(test_session_last_used);
    RUN_TEST(test_session_estimate_tokens);
    RUN_TEST(test_session_estimate_tokens_punct_and_safety);
    RUN_TEST(test_session_token_tracking);
    RUN_TEST(test_session_token_persistence);
    RUN_TEST(test_session_deferred_persistence);
    RUN_TEST(test_session_needs_memory_flush);
    RUN_TEST(test_session_mark_memory_flushed);
    RUN_TEST(test_session_flush_after_compaction);
    RUN_TEST(test_session_workspace_set_get);
    RUN_TEST(test_session_workspace_persists);
    RUN_TEST(test_session_workspace_backward_compat);
    RUN_TEST(test_session_active_workspace_default);
    RUN_TEST(test_session_active_workspace_set_get);
    RUN_TEST(test_session_active_workspace_persists);
    RUN_TEST(test_session_active_workspace_update_persists);
    RUN_TEST(test_session_get_for_workspace);
    RUN_TEST(test_session_list_for_chat);
    RUN_TEST(test_session_close_workspace);
    RUN_TEST(test_session_get_if_workspace_matches_hit);
    RUN_TEST(test_session_get_if_workspace_matches_miss);
    RUN_TEST(test_session_get_if_workspace_matches_no_session);
}
