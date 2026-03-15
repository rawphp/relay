#ifndef RELAY_EL_REPLY_H
#define RELAY_EL_REPLY_H

#include <stddef.h>
#include "workspace_resolver.h"

/* ── Reply-text selection helpers (event loop) ──────────────────────── */

/* Returns the text to send to the user in the LLM success branch.
 * If first_chunk_sent is non-zero, streaming already delivered content;
 * llm_result is returned unchanged (for logging).
 * If first_chunk_sent is zero and llm_result is empty, returns the
 * fallback string "(no response)".
 * Otherwise returns llm_result. */
const char *el_pick_reply_text(int first_chunk_sent, const char *llm_result);

/* Apology string sent to the user when the LLM error path triggers. */
const char *el_llm_error_text(void);

/* Message sent when Claude times out or is interrupted mid-response.
 * first_chunk_sent: 1 if partial output was already streamed to the user,
 * 0 if no output was sent at all. Returns distinct messages for each case. */
const char *el_timeout_reply_text(int first_chunk_sent);

/* Reply sent to the user when /reload succeeds. */
const char *el_reload_ok_text(void);

/* Reply sent to the user when /reload fails. */
const char *el_reload_error_text(void);

/* Reply sent to the user for /status.
 * sidecar_healthy: 1 if the memory sidecar last probe succeeded, 0 otherwise. */
const char *el_status_text(int sidecar_healthy);

/* Reply sent to the user when /restart is acknowledged (before stopping). */
const char *el_restart_text(void);

/* Reply sent when /restart is denied because the daemon just started (cooldown). */
const char *el_restart_cooldown_text(void);

/* Reply sent to the user for /start and /help commands.
 * agent_name: the daemon's configured name (e.g. "Kai").
 * Writes the formatted help message into buf (NUL-terminated, max bytes). */
void el_help_text(const char *agent_name, char *buf, size_t buf_size);

/* Returns 1 when an HTTP failure during Telegram polling should be treated as
 * an intentional shutdown abort: suppress the WARN log and skip backoff sleep.
 * loop_running: current value of loop->running (0 means shutdown was requested). */
int el_poll_abort_is_shutdown(int loop_running);

/* Categorize a Telegram poll failure by its curl error detail string.
 * Returns a short tag: "dns", "timeout", "abort", or "network" (default).
 * detail: the resp buffer from the HTTP layer (may be NULL). */
const char *el_poll_failure_tag(const char *detail);

/* Build the local save path for a downloaded Telegram photo.
 * If photo_dir is non-NULL and non-empty, uses {photo_dir}/{file_id}.{ext}.
 * Otherwise falls back to {workspace}/data/.telegram-photos/{file_id}.{ext}.
 * Writes result into out (NUL-terminated, max bytes including NUL). */
void el_build_photo_path(const char *photo_dir, const char *workspace,
                         const char *file_id, const char *ext,
                         char *out, size_t max);

/* Returns the workspace path to use when spawning Claude for a photo message.
 * If ws is valid (is_error == 0 and path non-empty), returns ws->path.
 * Otherwise falls back to $HOME, then "/tmp" — photos are conversational and
 * do not require a configured code workspace.
 * fallback_buf / buf_size: caller-supplied buffer used only for the fallback
 * case; not written when ws->path is returned directly.
 * Never returns "." or an empty string. */
const char *el_photo_llm_workspace(const resolved_workspace_t *ws,
                                    char *fallback_buf, size_t buf_size);

/* Build the required-keys array used to validate a config on /reload.
 * provider: the llm_provider value from the new config (e.g. "claude").
 * required: caller-supplied array of const char* to populate.
 * max_count: size of required[].
 * Returns the number of keys written.
 * workspace_path is intentionally excluded — it is optional (has fallbacks). */
int el_reload_collect_required(const char *provider,
                                const char *required[], int max_count);

/* Format health alert message for persistent LLM failures.
 * failures: number of consecutive failures.
 * last_error: detail string (may be NULL or empty).
 * buf/buf_size: caller-supplied buffer for the formatted message. */
void el_health_alert_text(int failures, const char *last_error,
                          char *buf, size_t buf_size);

/* Format diagnostic env vars for logging on spawn failure.
 * Writes CLAUDECODE, HOME, and PATH (truncated) to buf. */
void el_spawn_diag_text(char *buf, size_t buf_size);

/* Format heartbeat text for long-running LLM calls.
 * elapsed_sec: seconds since last output was sent to the user.
 * buf/buf_size: caller-supplied buffer for the formatted message. */
void el_heartbeat_text(int elapsed_sec, char *buf, size_t buf_size);

/* Format retry notification text for display to the user.
 * attempt: current attempt number (1-based, e.g. 2 = second attempt).
 * max_retries: total configured retry count.
 * buf/buf_size: caller-supplied buffer for the formatted message. */
void el_retry_notify_text(int attempt, int max_retries,
                           char *buf, size_t buf_size);

/* Acknowledgment sent to the user immediately after a photo is downloaded,
 * before vision processing or the LLM call.
 * caption: the photo's Telegram caption (may be NULL or empty).
 * Writes the formatted message into buf (NUL-terminated, max bytes). */
void el_photo_ack_text(const char *caption, char *buf, size_t buf_size);

/* Reply sent when the user sends a slash command that is not recognised.
 * cmd: the full command text (e.g. "/foo").
 * Writes the formatted message into buf (NUL-terminated, max bytes). */
void el_unknown_command_text(const char *cmd, char *buf, size_t buf_size);

/* Returns the placeholder text sent to Telegram before the spinner thread
 * starts.  Always returns el_spinner_frame(0) so the transition is seamless. */
const char *el_placeholder_text(void);

/* Message sent to the authorized user when the daemon starts successfully.
 * Sent once per startup, before the first Telegram poll.
 * Returns a static string — no buffer needed. */
const char *el_startup_ready_text(void);

/* Message sent to the user after crash recovery.
 * has_context: 1 if recent transcript was recovered, 0 if not.
 * Writes the formatted message into buf (NUL-terminated, max bytes). */
void el_recovery_text(int has_context, char *buf, size_t buf_size);

#endif /* RELAY_EL_REPLY_H */
