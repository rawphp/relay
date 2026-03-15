#ifndef RELAY_OPENAI_CODEX_H
#define RELAY_OPENAI_CODEX_H

#include "relay.h"
#include "config.h"

typedef struct {
    char session_id[RELAY_MAX_SESSION_ID];
    char result[RELAY_MAX_RESPONSE];
    int duration_ms;
    int is_error;
} openai_codex_response_t;

typedef struct openai_codex openai_codex_t;

/* Create an OpenAI Codex client. Uses cfg keys: openai_binary, openai_timeout,
 * openai_model, openai_sandbox. Returns NULL on failure. */
openai_codex_t *openai_codex_create(relay_proc_t *proc, const config_t *cfg);

/* Send message to Codex CLI subprocess. session_id may be NULL/empty to start
 * a new session; a non-empty session_id is passed via --session. The CLI
 * outputs JSONL (one JSON object per line); the last assistant line is used.
 * workspace_path is accepted but currently ignored (stub).
 * Returns RELAY_OK / RELAY_ERR. */
int openai_codex_send(openai_codex_t *oa, const char *message,
                      const char *session_id, const char *workspace_path,
                      openai_codex_response_t *resp);

/* Like openai_codex_send but retries transient failures with backoff.
 * The session_id is NOT passed on retry to avoid replaying a broken session.
 * workspace_path is accepted but currently ignored (stub). */
int openai_codex_send_with_retry(openai_codex_t *oa, const char *message,
                                 const char *session_id,
                                 const char *workspace_path,
                                 openai_codex_response_t *resp);

/* Parse a JSONL string (newline-separated JSON objects) from Codex CLI stdout.
 * Extracts the assistant message, session_id, and timing from the last relevant
 * object. Returns RELAY_OK on success, RELAY_ERR if no usable object is found. */
int openai_codex_parse_response(const char *jsonl, openai_codex_response_t *resp);

/* Hot-reload tunable config fields (timeout, model, sandbox).
 * Safe to call while the daemon is running. */
void openai_codex_update_config(openai_codex_t *oa, const config_t *cfg);

/* Free all resources. Safe to call with NULL. */
void openai_codex_free(openai_codex_t *oa);

#endif /* RELAY_OPENAI_CODEX_H */
