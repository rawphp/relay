#ifndef RELAY_CLAUDE_H
#define RELAY_CLAUDE_H

#include "relay.h"
#include "config.h"

/* ── Claude Spawner ─────────────────────────────────────────────────── */
/* Spawns Claude CLI, pipes message via stdin, captures JSON response.   */

/* Response from Claude CLI */
typedef struct {
    char session_id[RELAY_MAX_SESSION_ID];
    char result[RELAY_MAX_RESPONSE];
    int duration_ms;
    int is_error;
} claude_response_t;

/* Opaque claude type */
typedef struct claude claude_t;

/* Create Claude spawner with injected process abstraction. */
claude_t *claude_create(relay_proc_t *proc, const config_t *cfg);

/* Send message to Claude. If session_id is non-NULL, uses --resume.
 * If workspace_path is non-NULL, overrides cl->workspace for --add-dir.
 * Tilde in workspace_path is expanded to $HOME.
 * Fills response struct. Returns RELAY_OK on success. */
int claude_send(claude_t *cl, const char *message,
                const char *session_id, const char *workspace_path,
                claude_response_t *resp);

/* Token streaming callback. Return 0 to continue, non-zero to abort. */
typedef int (*claude_token_cb)(const char *text, size_t len, void *userdata);

/* Like claude_send but streams tokens via callback as they arrive.
 * on_token is called for each text fragment from Claude.
 * resp is populated from the final "result" event.
 * workspace_path overrides cl->workspace for --add-dir when non-NULL.
 * Falls back to non-streaming claude_send if proc->spawn_streaming is NULL.
 * Returns RELAY_OK on success. */
int claude_send_streaming(claude_t *cl, const char *message,
                           const char *session_id, const char *workspace_path,
                           claude_token_cb on_token, void *userdata,
                           claude_response_t *resp);

/* Send message to Claude with retry logic on transient errors.
 * Retries on timeout/network errors with exponential backoff.
 * workspace_path overrides cl->workspace for --add-dir when non-NULL.
 * Does NOT retry on parse errors (permanent failures).
 * Returns RELAY_OK on success. */
int claude_send_with_retry(claude_t *cl, const char *message,
                            const char *session_id, const char *workspace_path,
                            claude_response_t *resp);

/* Like claude_send_streaming but with retry on transient errors.
 * Retries only when no tokens have been delivered yet — if the token callback
 * has already fired, aborting would have already sent partial output to the
 * user, so we do NOT retry in that case to avoid duplicates.
 * workspace_path overrides cl->workspace for --add-dir when non-NULL.
 * Returns RELAY_OK on success. */
int claude_send_streaming_with_retry(claude_t *cl, const char *message,
                                      const char *session_id,
                                      const char *workspace_path,
                                      claude_token_cb on_token, void *userdata,
                                      claude_response_t *resp);

/* Parse a JSON response string from Claude CLI.
 * Returns RELAY_OK on success. */
int claude_parse_response(const char *json, claude_response_t *resp);

/* Build the system prompt injected via --system-prompt on each call.
 * out must be at least CLAUDE_SYSTEM_PROMPT_MAX bytes.
 * agent_name/user_name come from config; date/time injected automatically. */
#define CLAUDE_SYSTEM_PROMPT_MAX 1024
void claude_build_system_prompt(char *out, size_t max,
                                 const char *agent_name,
                                 const char *user_name,
                                 const char *workspace_name,
                                 const char *workspace_path);

/* Retry notification callback type.
 * Called before each retry attempt when no tokens were delivered.
 * attempt: current attempt number (1-based, e.g. 2 = second attempt).
 * max_retries: total configured retry count.
 * userdata: opaque pointer passed to claude_set_retry_notify. */
typedef void (*claude_retry_notify_cb)(int attempt, int max_retries,
                                        void *userdata);

/* Set a callback to be invoked before each retry on transient errors.
 * Only fires when no tokens have been delivered (zero-output case).
 * Pass NULL to disable notifications. */
void claude_set_retry_notify(claude_t *cl, claude_retry_notify_cb cb,
                              void *userdata);

/* Update mutable config values (e.g. timeout) without restart. */
void claude_update_config(claude_t *cl, const config_t *cfg);

/* Returns the number of LLM calls currently in progress for this spawner.
 * Thread-safe (atomic read). Returns 0 if cl is NULL. */
int claude_in_flight(const claude_t *cl);

/* Free Claude spawner. */
void claude_free(claude_t *cl);

#endif /* RELAY_CLAUDE_H */
