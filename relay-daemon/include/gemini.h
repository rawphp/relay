#ifndef RELAY_GEMINI_H
#define RELAY_GEMINI_H

#include "relay.h"
#include "config.h"

typedef struct {
    char session_id[RELAY_MAX_SESSION_ID];
    char result[RELAY_MAX_RESPONSE];
    int duration_ms;
    int is_error;
} gemini_response_t;

typedef struct gemini gemini_t;

/* Create a Gemini client. Uses cfg keys: gemini_binary, gemini_timeout,
 * gemini_model, gemini_enable_sandbox, gemini_approval_mode. Returns NULL on
 * failure. */
gemini_t *gemini_create(relay_proc_t *proc, const config_t *cfg);

/* Send message to Gemini CLI subprocess. session_id may be NULL/empty to start
 * a new conversation; a non-empty session_id is passed via --resume. The CLI
 * outputs a JSON object; gemini_send strips any preamble text before the first
 * '{' to tolerate model-loading noise on stdout.
 * workspace_path is accepted but currently ignored (stub).
 * Returns RELAY_OK / RELAY_ERR. */
int gemini_send(gemini_t *gm, const char *message,
                const char *session_id, const char *workspace_path,
                gemini_response_t *resp);

/* Like gemini_send but retries transient failures with backoff.
 * The session_id is NOT passed on retry to avoid replaying a broken session.
 * workspace_path is accepted but currently ignored (stub). */
int gemini_send_with_retry(gemini_t *gm, const char *message,
                           const char *session_id, const char *workspace_path,
                           gemini_response_t *resp);

/* Hot-reload tunable config fields (timeout, model, sandbox, approval_mode).
 * Safe to call while the daemon is running. */
void gemini_update_config(gemini_t *gm, const config_t *cfg);

/* Free all resources. Safe to call with NULL. */
void gemini_free(gemini_t *gm);

#endif /* RELAY_GEMINI_H */
