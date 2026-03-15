#include "Unity/unity.h"
#include "interruption.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── Test: Create session registry ──────────────────────────────────── */
static void test_create_session_registry(void)
{
    active_sessions_t *sessions = active_sessions_create();
    TEST_ASSERT_NOT_NULL(sessions);
    active_sessions_free(sessions);
}

/* ── Test: Register session ─────────────────────────────────────────── */
static void test_register_session(void)
{
    active_sessions_t *sessions = active_sessions_create();

    active_sessions_register(sessions, "123456", 9999);

    /* Session should exist */
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "123456"));

    /* Different chat should not exist */
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "999999"));

    active_sessions_free(sessions);
}

/* ── Test: Register multiple sessions ───────────────────────────────── */
static void test_register_multiple_sessions(void)
{
    active_sessions_t *sessions = active_sessions_create();

    active_sessions_register(sessions, "user1", 1001);
    active_sessions_register(sessions, "user2", 1002);
    active_sessions_register(sessions, "user3", 1003);

    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user1"));
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user2"));
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user3"));

    active_sessions_free(sessions);
}

/* ── Test: Update existing session ──────────────────────────────────── */
static void test_update_existing_session(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Register initial PID */
    active_sessions_register(sessions, "123456", 1000);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "123456"));

    /* Update with new PID (simulating restart) */
    active_sessions_register(sessions, "123456", 2000);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "123456"));

    active_sessions_free(sessions);
}

/* ── Test: Cleanup removes session ──────────────────────────────────── */
static void test_cleanup_session(void)
{
    active_sessions_t *sessions = active_sessions_create();

    active_sessions_register(sessions, "123456", 9999);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "123456"));

    active_sessions_cleanup(sessions, "123456");
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "123456"));

    active_sessions_free(sessions);
}

/* ── Test: Cleanup non-existent session (should not crash) ──────────── */
static void test_cleanup_non_existent_session(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Should not crash */
    active_sessions_cleanup(sessions, "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "nonexistent"));

    active_sessions_free(sessions);
}

/* ── Test: Interrupt kills process ──────────────────────────────────── */
static void test_interrupt_kills_process(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Fork a test process that sleeps */
    pid_t pid = fork();

    if (pid == 0) {
        /* Child: sleep forever */
        while (1) {
            sleep(1);
        }
        _exit(0);
    }

    /* Parent: register and interrupt */
    active_sessions_register(sessions, "test_user", pid);

    /* Verify process is running */
    TEST_ASSERT_EQUAL_INT(0, kill(pid, 0)); /* Signal 0 checks if process exists */

    /* Interrupt should kill it */
    int result = active_sessions_interrupt(sessions, "test_user");
    TEST_ASSERT_EQUAL_INT(1, result);

    /* Give kill a moment to complete */
    usleep(150000); /* 150ms */

    /* Verify process is dead */
    TEST_ASSERT_EQUAL_INT(-1, kill(pid, 0));
    TEST_ASSERT_EQUAL_INT(ESRCH, errno); /* No such process */

    /* Session should be marked inactive */
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "test_user"));

    active_sessions_free(sessions);
}

/* ── Test: Interrupt non-existent session returns 0 ─────────────────── */
static void test_interrupt_non_existent_session(void)
{
    active_sessions_t *sessions = active_sessions_create();

    int result = active_sessions_interrupt(sessions, "nonexistent");
    TEST_ASSERT_EQUAL_INT(0, result);

    active_sessions_free(sessions);
}

/* ── Test: Max sessions limit ───────────────────────────────────────── */
static void test_max_sessions_limit(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Register 64 sessions (MAX_SESSIONS) */
    for (int i = 0; i < 64; i++) {
        char chat_id[64];
        snprintf(chat_id, sizeof(chat_id), "user%d", i);
        active_sessions_register(sessions, chat_id, 1000 + i);
    }

    /* All should be registered */
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user0"));
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user63"));

    /* 65th session should still work (replaces oldest) */
    active_sessions_register(sessions, "user65", 2000);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user65"));

    active_sessions_free(sessions);
}

/* ── Test: Empty chat_id handling ───────────────────────────────────── */
static void test_empty_chat_id(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Register with empty chat_id */
    active_sessions_register(sessions, "", 9999);

    /* Should handle gracefully */
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, ""));

    active_sessions_free(sessions);
}

/* ── Test: NULL chat_id handling ────────────────────────────────────── */
static void test_null_chat_id(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Should not crash with NULL */
    active_sessions_register(sessions, NULL, 9999);
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, NULL));

    int result = active_sessions_interrupt(sessions, NULL);
    TEST_ASSERT_EQUAL_INT(0, result);

    active_sessions_cleanup(sessions, NULL);

    active_sessions_free(sessions);
}

/* ── Test: Invalid PID handling ─────────────────────────────────────── */
static void test_invalid_pid(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Register with invalid PID */
    active_sessions_register(sessions, "test", -1);

    /* Should still track session */
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "test"));

    /* Interrupt won't kill anything but shouldn't crash */
    int result = active_sessions_interrupt(sessions, "test");
    TEST_ASSERT_EQUAL_INT(1, result);

    active_sessions_free(sessions);
}

/* ── Test: Concurrent session operations ────────────────────────────── */
static void test_concurrent_operations(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Register, check, cleanup in sequence */
    active_sessions_register(sessions, "user1", 1001);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user1"));

    active_sessions_register(sessions, "user2", 1002);
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user2"));

    active_sessions_cleanup(sessions, "user1");
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "user1"));
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, "user2"));

    active_sessions_free(sessions);
}

/* ── Test: Double cleanup (idempotent) ──────────────────────────────── */
static void test_double_cleanup(void)
{
    active_sessions_t *sessions = active_sessions_create();

    active_sessions_register(sessions, "test", 9999);

    active_sessions_cleanup(sessions, "test");
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "test"));

    /* Second cleanup should not crash */
    active_sessions_cleanup(sessions, "test");
    TEST_ASSERT_EQUAL_INT(0, active_sessions_has(sessions, "test"));

    active_sessions_free(sessions);
}

/* ── Test: Long chat_id handling ────────────────────────────────────── */
static void test_long_chat_id(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Create a very long chat_id */
    char long_id[128];
    memset(long_id, 'A', sizeof(long_id) - 1);
    long_id[sizeof(long_id) - 1] = '\0';

    /* Should truncate but not crash */
    active_sessions_register(sessions, long_id, 9999);

    /* Create truncated version to search for (first 63 chars + null terminator) */
    char truncated_id[64];
    memset(truncated_id, 'A', 63);
    truncated_id[63] = '\0';

    /* Should be findable by truncated version */
    TEST_ASSERT_EQUAL_INT(1, active_sessions_has(sessions, truncated_id));

    active_sessions_free(sessions);
}

/* ── Test: Process already dead ─────────────────────────────────────── */
static void test_interrupt_already_dead_process(void)
{
    active_sessions_t *sessions = active_sessions_create();

    /* Fork a process that exits immediately */
    pid_t pid = fork();

    if (pid == 0) {
        _exit(0); /* Exit immediately */
    }

    /* Wait for child to die */
    waitpid(pid, NULL, 0);

    /* Register the dead PID */
    active_sessions_register(sessions, "test", pid);

    /* Interrupt should handle gracefully */
    int result = active_sessions_interrupt(sessions, "test");
    TEST_ASSERT_EQUAL_INT(1, result);

    active_sessions_free(sessions);
}

/* ── Test: Free NULL registry ───────────────────────────────────────── */
static void test_free_null_registry(void)
{
    /* Should not crash */
    active_sessions_free(NULL);
    TEST_PASS();
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_interruption_suite(void)
{
    RUN_TEST(test_create_session_registry);
    RUN_TEST(test_register_session);
    RUN_TEST(test_register_multiple_sessions);
    RUN_TEST(test_update_existing_session);
    RUN_TEST(test_cleanup_session);
    RUN_TEST(test_cleanup_non_existent_session);
    RUN_TEST(test_interrupt_kills_process);
    RUN_TEST(test_interrupt_non_existent_session);
    RUN_TEST(test_max_sessions_limit);
    RUN_TEST(test_empty_chat_id);
    RUN_TEST(test_null_chat_id);
    RUN_TEST(test_invalid_pid);
    RUN_TEST(test_concurrent_operations);
    RUN_TEST(test_double_cleanup);
    RUN_TEST(test_long_chat_id);
    RUN_TEST(test_interrupt_already_dead_process);
    RUN_TEST(test_free_null_registry);
}
