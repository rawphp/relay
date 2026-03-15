#include "Unity/unity.h"
#include "el_reply.h"
#include "el_spinner.h"
#include "workspace_resolver.h"
#include <string.h>
#include <stdlib.h>

/* ── Tests: el_pick_reply_text ──────────────────────────────────────── */

/* mock LLM returning RELAY_OK, is_error=0, result="" with no streaming */
static void test_empty_result_sends_fallback(void)
{
    const char *text = el_pick_reply_text(/*first_chunk_sent=*/0, /*llm_result=*/"");
    TEST_ASSERT_EQUAL_STRING("(no response)", text);
}

static void test_nonempty_result_no_fallback(void)
{
    const char *text = el_pick_reply_text(0, "Hello world");
    TEST_ASSERT_EQUAL_STRING("Hello world", text);
}

/* When streaming delivered tokens, even empty llm_result is not fallback */
static void test_streaming_sent_no_fallback(void)
{
    const char *text = el_pick_reply_text(/*first_chunk_sent=*/1, "");
    TEST_ASSERT_EQUAL_STRING("", text);
}

/* ── Tests: el_llm_error_text ───────────────────────────────────────── */

/* mock LLM returning RELAY_ERR → verify apology string used */
static void test_llm_error_sends_apology(void)
{
    const char *apology = el_llm_error_text();
    TEST_ASSERT_NOT_NULL(apology);
    TEST_ASSERT_TRUE(strlen(apology) > 0);
    /* Must not expose raw error detail — keep it short and generic */
    TEST_ASSERT_EQUAL_STRING(
        "Sorry, I couldn't process that. Please try again.", apology);
}

/* ── Tests: el_reload_ok_text / el_reload_error_text ───────────────── */

static void test_reload_ok_text(void)
{
    const char *text = el_reload_ok_text();
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_STRING("Config reloaded.", text);
}

static void test_reload_error_text(void)
{
    const char *text = el_reload_error_text();
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
}

/* ── Tests: el_status_text ──────────────────────────────────────────── */

static void test_status_text_sidecar_healthy(void)
{
    const char *text = el_status_text(/*sidecar_healthy=*/1);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
    /* Must mention memory sidecar running */
    TEST_ASSERT_NOT_NULL(strstr(text, "running"));
}

static void test_status_text_sidecar_unhealthy(void)
{
    const char *text = el_status_text(/*sidecar_healthy=*/0);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
    /* Must mention memory sidecar stopped */
    TEST_ASSERT_NOT_NULL(strstr(text, "stopped"));
}

static void test_status_text_is_short(void)
{
    /* Must be a single short message — no wall of text */
    const char *text0 = el_status_text(0);
    const char *text1 = el_status_text(1);
    TEST_ASSERT_TRUE(strlen(text0) < 200);
    TEST_ASSERT_TRUE(strlen(text1) < 200);
}

/* ── Tests: el_restart_text ─────────────────────────────────────────── */

static void test_restart_text_not_null(void)
{
    const char *text = el_restart_text();
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
}

static void test_restart_text_is_short(void)
{
    const char *text = el_restart_text();
    TEST_ASSERT_TRUE(strlen(text) < 200);
}

/* ── Tests: el_restart_cooldown_text ────────────────────────────────── */

static void test_restart_cooldown_text_not_null(void)
{
    const char *text = el_restart_cooldown_text();
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
}

static void test_restart_cooldown_text_is_short(void)
{
    const char *text = el_restart_cooldown_text();
    TEST_ASSERT_TRUE(strlen(text) < 200);
}

static void test_restart_cooldown_text_differs_from_restart(void)
{
    const char *cooldown = el_restart_cooldown_text();
    const char *restart  = el_restart_text();
    TEST_ASSERT_TRUE(strcmp(cooldown, restart) != 0);
}

/* ── Tests: el_spinner_frame ────────────────────────────────────────── */

static void test_spinner_has_frames(void)
{
    TEST_ASSERT_GREATER_THAN(0, el_spinner_frame_count());
}

static void test_spinner_frames_not_empty(void)
{
    int n = el_spinner_frame_count();
    for (int i = 0; i < n; i++) {
        const char *f = el_spinner_frame(i);
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_GREATER_THAN(0, (int)strlen(f));
    }
}

static void test_spinner_frames_cycle(void)
{
    int n = el_spinner_frame_count();
    /* frame(n) must equal frame(0) — it wraps */
    TEST_ASSERT_EQUAL_STRING(el_spinner_frame(0), el_spinner_frame(n));
    TEST_ASSERT_EQUAL_STRING(el_spinner_frame(1), el_spinner_frame(n + 1));
}

static void test_spinner_adjacent_frames_differ(void)
{
    /* At least the first two frames must look different */
    const char *f0 = el_spinner_frame(0);
    const char *f1 = el_spinner_frame(1);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(f0, f1));
}

static void test_spinner_frames_use_visible_chars(void)
{
    /* All frames must use only printable ASCII — no tiny Unicode glyphs */
    int n = el_spinner_frame_count();
    for (int i = 0; i < n; i++) {
        const char *f = el_spinner_frame(i);
        for (const char *p = f; *p; p++) {
            TEST_ASSERT_TRUE_MESSAGE(
                (*p >= 0x20 && *p <= 0x7E) || *p == '\0',
                "Spinner frame contains non-ASCII character");
        }
    }
}

static void test_spinner_first_frame_contains_dot(void)
{
    /* First frame must contain at least one dot — accumulating animation */
    const char *f0 = el_spinner_frame(0);
    TEST_ASSERT_NOT_NULL(strchr(f0, '.'));
}

static void test_spinner_frames_dots_only(void)
{
    /* Every frame must contain only dot characters — no text like "Working" */
    int n = el_spinner_frame_count();
    for (int i = 0; i < n; i++) {
        const char *f = el_spinner_frame(i);
        for (const char *p = f; *p; p++) {
            TEST_ASSERT_EQUAL_MESSAGE('.', *p,
                "Spinner frame must contain only dots");
        }
    }
}

static void test_spinner_frames_accumulate_forward(void)
{
    /* Each frame must be longer than the previous — forward accumulation */
    int n = el_spinner_frame_count();
    for (int i = 1; i < n; i++) {
        size_t prev_len = strlen(el_spinner_frame(i - 1));
        size_t curr_len = strlen(el_spinner_frame(i));
        TEST_ASSERT_GREATER_THAN(prev_len, curr_len);
    }
}

static void test_placeholder_matches_first_spinner_frame(void)
{
    /* The placeholder sent before spinner starts must equal the first frame
     * so the transition is seamless. el_placeholder_text() provides this. */
    const char *placeholder = el_placeholder_text();
    const char *first_frame = el_spinner_frame(0);
    TEST_ASSERT_EQUAL_STRING(first_frame, placeholder);
}

/* ── Tests: el_build_photo_path ─────────────────────────────────────── */

static void test_photo_path_uses_photo_dir_and_file_id(void)
{
    char out[512];
    el_build_photo_path("/home/relay/data/.telegram-photos", "/home/relay",
                        "AgACfile123", "jpg", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "/home/relay/data/.telegram-photos/AgACfile123.jpg", out);
}

static void test_photo_path_fallback_to_workspace_when_no_photo_dir(void)
{
    char out[512];
    el_build_photo_path(NULL, "/home/relay", "AgACfile456", "jpg",
                        out, sizeof(out));
    /* Falls back to {workspace}/data/.telegram-photos/{file_id}.{ext} */
    TEST_ASSERT_EQUAL_STRING(
        "/home/relay/data/.telegram-photos/AgACfile456.jpg", out);
}

static void test_photo_path_uses_correct_extension(void)
{
    char out[512];
    el_build_photo_path("/photos", NULL, "file789", "png", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/photos/file789.png", out);
}

/* ── Tests: el_poll_abort_is_shutdown ───────────────────────────────── */

/* When loop_running == 0 (shutdown), abort is intentional — suppress WARN. */
static void test_poll_abort_shutdown_when_not_running(void)
{
    TEST_ASSERT_EQUAL_INT(1, el_poll_abort_is_shutdown(/*loop_running=*/0));
}

/* When loop_running == 1 (normal operation), abort is spurious — log WARN. */
static void test_poll_abort_not_shutdown_when_running(void)
{
    TEST_ASSERT_EQUAL_INT(0, el_poll_abort_is_shutdown(/*loop_running=*/1));
}

/* ── Tests: el_poll_failure_tag ─────────────────────────────────────── */

static void test_poll_failure_tag_dns(void)
{
    const char *tag = el_poll_failure_tag("curl: Couldn't resolve host name");
    TEST_ASSERT_EQUAL_STRING("dns", tag);
}

static void test_poll_failure_tag_timeout(void)
{
    const char *tag = el_poll_failure_tag("curl: Timeout was reached");
    TEST_ASSERT_EQUAL_STRING("timeout", tag);
}

static void test_poll_failure_tag_abort(void)
{
    const char *tag = el_poll_failure_tag(
        "curl: Operation was aborted by an application callback");
    TEST_ASSERT_EQUAL_STRING("abort", tag);
}

static void test_poll_failure_tag_generic(void)
{
    const char *tag = el_poll_failure_tag("curl: Something else happened");
    TEST_ASSERT_EQUAL_STRING("network", tag);
}

static void test_poll_failure_tag_null(void)
{
    const char *tag = el_poll_failure_tag(NULL);
    TEST_ASSERT_EQUAL_STRING("network", tag);
}

static void test_poll_failure_tag_empty(void)
{
    const char *tag = el_poll_failure_tag("");
    TEST_ASSERT_EQUAL_STRING("network", tag);
}

/* ── Tests: el_timeout_reply_text ───────────────────────────────────── */

/* When partial stream was already sent, message mentions "cut short". */
static void test_timeout_reply_with_partial_stream(void)
{
    const char *text = el_timeout_reply_text(/*first_chunk_sent=*/1);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
    TEST_ASSERT_NOT_NULL(strstr(text, "cut short"));
}

/* When no output was sent, message still notifies the user. */
static void test_timeout_reply_no_partial_stream(void)
{
    const char *text = el_timeout_reply_text(/*first_chunk_sent=*/0);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_TRUE(strlen(text) > 0);
}

/* The two messages must be different (contextual messaging). */
static void test_timeout_reply_messages_differ(void)
{
    const char *with_stream    = el_timeout_reply_text(1);
    const char *without_stream = el_timeout_reply_text(0);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(with_stream, without_stream));
}

/* ── Tests: el_photo_llm_workspace ──────────────────────────────────── */

/* When a workspace is configured, use its resolved path. */
static void test_photo_workspace_uses_resolved_path(void)
{
    resolved_workspace_t ws;
    memset(&ws, 0, sizeof(ws));
    snprintf(ws.path, sizeof(ws.path), "/home/relay/code");
    ws.is_error = 0;

    char buf[RELAY_MAX_PATH];
    const char *result = el_photo_llm_workspace(&ws, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_STRING("/home/relay/code", result);
}

/* When no workspace is configured (is_error=1), must NOT return "." or empty. */
static void test_photo_workspace_no_workspace_never_dot(void)
{
    resolved_workspace_t ws;
    memset(&ws, 0, sizeof(ws));
    ws.is_error = 1;

    char buf[RELAY_MAX_PATH];
    const char *result = el_photo_llm_workspace(&ws, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(result));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(result, "."));
}

/* When no workspace is configured, result must be $HOME or /tmp. */
static void test_photo_workspace_no_workspace_is_home_or_tmp(void)
{
    resolved_workspace_t ws;
    memset(&ws, 0, sizeof(ws));
    ws.is_error = 1;

    char buf[RELAY_MAX_PATH];
    const char *result = el_photo_llm_workspace(&ws, buf, sizeof(buf));

    const char *home = getenv("HOME");
    int is_home = (home && home[0] != '\0' && strcmp(result, home) == 0);
    int is_tmp  = (strcmp(result, "/tmp") == 0);
    TEST_ASSERT_TRUE(is_home || is_tmp);
}

/* ── Tests: el_help_text ────────────────────────────────────────────── */

static void test_help_text_includes_agent_name(void)
{
    char buf[512];
    el_help_text("TestBot", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "TestBot"));
}

static void test_help_text_includes_all_commands(void)
{
    char buf[512];
    el_help_text("Bot", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/status"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/restart"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/reload"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/clear"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/close"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/workspace"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/session"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/help"));
}

static void test_help_text_not_empty(void)
{
    char buf[512];
    el_help_text("Bot", buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(buf));
}

/* ── Tests: el_reload_collect_required ──────────────────────────────── */

/* workspace_path must NOT be in required keys — it is optional with fallback */
static void test_reload_required_omits_workspace_path(void)
{
    const char *required[6];
    int count = el_reload_collect_required("claude", required, 6);

    TEST_ASSERT_GREATER_THAN(0, count);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0,
            strcmp(required[i], "workspace_path"),
            "workspace_path must not be a required key on reload");
    }
}

/* telegram_bot_token and telegram_user_id must always be required */
static void test_reload_required_includes_telegram_keys(void)
{
    const char *required[6];
    int count = el_reload_collect_required("claude", required, 6);

    int found_token = 0, found_uid = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(required[i], "telegram_bot_token") == 0) found_token = 1;
        if (strcmp(required[i], "telegram_user_id") == 0)   found_uid   = 1;
    }
    TEST_ASSERT_TRUE(found_token);
    TEST_ASSERT_TRUE(found_uid);
}

/* ── Tests: el_heartbeat_text ───────────────────────────────────────── */

static void test_heartbeat_text_not_null(void)
{
    char buf[128];
    el_heartbeat_text(30, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

static void test_heartbeat_text_contains_elapsed(void)
{
    char buf[128];
    el_heartbeat_text(45, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "45"));
}

static void test_heartbeat_text_contains_working(void)
{
    char buf[128];
    el_heartbeat_text(30, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "working"));
}

/* ── Tests: el_retry_notify_text ────────────────────────────────────── */

static void test_retry_notify_text_format(void)
{
    char buf[128];
    el_retry_notify_text(2, 3, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "3"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Retrying"));
}

/* ── Tests: el_unknown_command_text ─────────────────────────────────── */

static void test_unknown_command_text_includes_command(void)
{
    char buf[256];
    el_unknown_command_text("/foo", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/foo"));
}

static void test_unknown_command_text_includes_help_hint(void)
{
    char buf[256];
    el_unknown_command_text("/bar", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "/help"));
}

static void test_unknown_command_text_not_empty(void)
{
    char buf[256];
    el_unknown_command_text("/baz", buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(buf));
}

/* ── Tests: el_photo_ack_text ───────────────────────────────────────── */

static void test_photo_ack_no_caption(void)
{
    char buf[256];
    el_photo_ack_text(NULL, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "photo"));
}

static void test_photo_ack_with_caption(void)
{
    char buf[256];
    el_photo_ack_text("hello world", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "hello world"));
}

static void test_photo_ack_not_empty(void)
{
    char buf[256];
    el_photo_ack_text("", buf, sizeof(buf));
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

/* ── Tests: el_startup_ready_text ───────────────────────────────────── */

static void test_startup_ready_text_not_empty(void)
{
    const char *text = el_startup_ready_text();
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(text));
}

static void test_startup_ready_text_content(void)
{
    const char *text = el_startup_ready_text();
    /* Must contain "Ready" — the user needs to recognise what this means */
    TEST_ASSERT_NOT_NULL(strstr(text, "Ready"));
}

/* ── Tests: el_recovery_text ────────────────────────────────────────── */

static void test_recovery_text_with_context(void)
{
    char buf[512];
    el_recovery_text(1, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE(strlen(buf) > 0);
    /* Must NOT ask user to resend — context was recovered */
    TEST_ASSERT_NULL(strstr(buf, "resend"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "restarted"));
}

static void test_recovery_text_without_context(void)
{
    char buf[512];
    el_recovery_text(0, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE(strlen(buf) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "restarted"));
}

static void test_recovery_text_with_vs_without_differs(void)
{
    char with_ctx[512], without_ctx[512];
    el_recovery_text(1, with_ctx, sizeof(with_ctx));
    el_recovery_text(0, without_ctx, sizeof(without_ctx));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(with_ctx, without_ctx));
}

/* ── Suite ─────────────────────────────────────────────────────────── */
void test_event_loop_suite(void)
{
    RUN_TEST(test_empty_result_sends_fallback);
    RUN_TEST(test_nonempty_result_no_fallback);
    RUN_TEST(test_streaming_sent_no_fallback);
    RUN_TEST(test_llm_error_sends_apology);
    RUN_TEST(test_reload_ok_text);
    RUN_TEST(test_reload_error_text);
    RUN_TEST(test_status_text_sidecar_healthy);
    RUN_TEST(test_status_text_sidecar_unhealthy);
    RUN_TEST(test_status_text_is_short);
    RUN_TEST(test_restart_text_not_null);
    RUN_TEST(test_restart_text_is_short);
    RUN_TEST(test_restart_cooldown_text_not_null);
    RUN_TEST(test_restart_cooldown_text_is_short);
    RUN_TEST(test_restart_cooldown_text_differs_from_restart);
    RUN_TEST(test_spinner_has_frames);
    RUN_TEST(test_spinner_frames_not_empty);
    RUN_TEST(test_spinner_frames_cycle);
    RUN_TEST(test_spinner_adjacent_frames_differ);
    RUN_TEST(test_spinner_frames_use_visible_chars);
    RUN_TEST(test_spinner_first_frame_contains_dot);
    RUN_TEST(test_spinner_frames_dots_only);
    RUN_TEST(test_spinner_frames_accumulate_forward);
    RUN_TEST(test_placeholder_matches_first_spinner_frame);
    RUN_TEST(test_photo_path_uses_photo_dir_and_file_id);
    RUN_TEST(test_photo_path_fallback_to_workspace_when_no_photo_dir);
    RUN_TEST(test_photo_path_uses_correct_extension);
    RUN_TEST(test_timeout_reply_with_partial_stream);
    RUN_TEST(test_timeout_reply_no_partial_stream);
    RUN_TEST(test_timeout_reply_messages_differ);
    RUN_TEST(test_poll_abort_shutdown_when_not_running);
    RUN_TEST(test_poll_abort_not_shutdown_when_running);
    /* poll failure categorization */
    RUN_TEST(test_poll_failure_tag_dns);
    RUN_TEST(test_poll_failure_tag_timeout);
    RUN_TEST(test_poll_failure_tag_abort);
    RUN_TEST(test_poll_failure_tag_generic);
    RUN_TEST(test_poll_failure_tag_null);
    RUN_TEST(test_poll_failure_tag_empty);
    RUN_TEST(test_photo_workspace_uses_resolved_path);
    RUN_TEST(test_photo_workspace_no_workspace_never_dot);
    RUN_TEST(test_photo_workspace_no_workspace_is_home_or_tmp);
    RUN_TEST(test_help_text_includes_agent_name);
    RUN_TEST(test_help_text_includes_all_commands);
    RUN_TEST(test_help_text_not_empty);
    RUN_TEST(test_reload_required_omits_workspace_path);
    RUN_TEST(test_reload_required_includes_telegram_keys);
    /* heartbeat text */
    RUN_TEST(test_heartbeat_text_not_null);
    RUN_TEST(test_heartbeat_text_contains_elapsed);
    RUN_TEST(test_heartbeat_text_contains_working);
    /* retry notification text — keep above photo ack tests */
    RUN_TEST(test_retry_notify_text_format);
    /* photo receipt acknowledgment */
    RUN_TEST(test_photo_ack_no_caption);
    RUN_TEST(test_photo_ack_with_caption);
    RUN_TEST(test_photo_ack_not_empty);
    /* unknown command fallback */
    RUN_TEST(test_unknown_command_text_includes_command);
    RUN_TEST(test_unknown_command_text_includes_help_hint);
    RUN_TEST(test_unknown_command_text_not_empty);
    /* startup ready notification */
    RUN_TEST(test_startup_ready_text_not_empty);
    RUN_TEST(test_startup_ready_text_content);
    /* crash recovery notification */
    RUN_TEST(test_recovery_text_with_context);
    RUN_TEST(test_recovery_text_without_context);
    RUN_TEST(test_recovery_text_with_vs_without_differs);
}
