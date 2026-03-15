#ifndef RELAY_LLM_PROVIDER_H
#define RELAY_LLM_PROVIDER_H

#include "relay.h"
#include "config.h"
#include "memory_search.h"

typedef struct {
    char session_id[RELAY_MAX_SESSION_ID];
    char result[RELAY_MAX_RESPONSE];
    int duration_ms;
    int is_error;
} llm_response_t;

typedef struct llm_provider llm_provider_t;

/* Returns non-zero if provider name refers to OpenAI Codex
 * (accepts "openai_codex", "openai", "codex"). */
int llm_is_openai_provider(const char *provider);

/* Create a provider using cfg's llm_provider key (default: "claude").
 * proc is used to spawn the backend CLI subprocess. Returns NULL on failure. */
llm_provider_t *llm_provider_create(relay_proc_t *proc, const config_t *cfg);

/* Send message to the LLM. session_id may be NULL or empty to start a new
 * session. Fills resp on both success and error; returns RELAY_OK / RELAY_ERR. */
int llm_provider_send(llm_provider_t *llm, const char *message,
                      const char *session_id, llm_response_t *resp);

/* Like llm_provider_send but retries transient failures with backoff.
 * On retry the session_id is dropped so the backend starts a fresh session
 * (avoids replaying a corrupt session into a new attempt). */
int llm_provider_send_with_retry(llm_provider_t *llm, const char *message,
                                 const char *session_id, llm_response_t *resp);

/* Token streaming callback. Return 0 to continue, non-zero to abort. */
typedef int (*llm_token_cb)(const char *text, size_t len, void *userdata);

/* Like llm_provider_send_with_retry but streams tokens via callback as they
 * arrive. on_token is called for each text fragment from the backend.
 * Falls back to blocking send for backends that don't support streaming.
 * Memory augmentation is applied as usual.
 * workspace_path: if non-NULL, overrides the configured workspace_path for
 * this call only (passed as --add-dir to Claude Code). */
int llm_provider_send_streaming(llm_provider_t *llm, const char *message,
                                const char *session_id,
                                const char *workspace_path,
                                llm_token_cb on_token, void *userdata,
                                llm_response_t *resp);

/* Workspace-aware send. Overrides the identity workspace path per-call.
 * If provider_name is non-NULL and matches a known backend, that backend is
 * used for this request instead of the default (lazy-initialised and cached).
 * Passing NULL for workspace_path or provider_name uses the global defaults.
 * Backward-compatible wrapper over llm_provider_send_with_retry. */
int llm_provider_send_workspace(llm_provider_t *llm,
                                 const char     *message,
                                 const char     *session_id,
                                 const char     *workspace_path,
                                 const char     *provider_name,
                                 llm_response_t *resp);

/* Hot-reload tunable config fields (timeouts, retry counts, model name).
 * Safe to call while the daemon is running; takes effect on the next send. */
void llm_provider_update_config(llm_provider_t *llm, const config_t *cfg);

/* Returns a static string naming the active backend: "claude", "openai_codex",
 * or "gemini". Returns "unknown" if llm is NULL. */
const char *llm_provider_name(const llm_provider_t *llm);

/* Attach a memory searcher for automatic context injection.
 * When set, every message sent through the provider will be augmented
 * with relevant memories before reaching the backend. May be NULL. */
void llm_provider_set_memory(llm_provider_t *llm, memory_search_t *ms);

/* Attach identity file injection.
 * When set, the contents of SOUL.md, IDENTITY.md, USER.md, and
 * PRIORITIES.md are read from <workspace> and prepended to every message
 * before the backend receives it. This avoids the 3-5 tool-call API
 * round-trips that Claude would otherwise make to read those files.
 * fs may be NULL to disable injection. */
void llm_provider_set_identity(llm_provider_t *llm, relay_fs_t *fs,
                                const char *workspace);

/* Returns the number of LLM calls currently in progress.
 * Non-zero means a call is in flight.
 * Returns 0 if llm is NULL. */
int llm_provider_in_flight(const llm_provider_t *llm);

/* Free all resources. Safe to call with NULL. */
void llm_provider_free(llm_provider_t *llm);

#endif /* RELAY_LLM_PROVIDER_H */
