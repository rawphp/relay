#ifndef RELAY_SESSION_H
#define RELAY_SESSION_H

#include "relay.h"

/* ── Session Manager ────────────────────────────────────────────────── */
/* Maps chat_id → {session_id, last_used}. Persists to JSON file.       */
/* Public read-only view of a session entry (workspace fields included). */
typedef struct {
    char chat_id[RELAY_MAX_USER_ID];
    char session_id[RELAY_MAX_SESSION_ID];
    char workspace_name[64];
    char workspace_path[RELAY_MAX_PATH];
    char provider[32];
    time_t last_used;
    size_t context_tokens;
    size_t compaction_count;
    time_t memory_flush_at;
    size_t memory_flush_compaction_count;
} session_entry_t;
/* Opaque session store type */
typedef struct session_store session_store_t;

/* Create session store. Loads existing sessions from persist_path
 * if the file exists. Returns NULL on error. */
session_store_t *session_create(relay_fs_t *fs, relay_clock_t *clock,
                                 const char *persist_path,
                                 int expiry_hours);

/* Get session_id for a chat_id. Returns NULL if no session exists. */
const char *session_get(session_store_t *store, const char *chat_id);

/* Set/update session_id for a chat_id. Also updates last_used time.
 * Persists to file after every update. */
int session_set(session_store_t *store, const char *chat_id,
                const char *session_id);

/* Remove session for a chat_id (e.g., /clear command). */
int session_clear(session_store_t *store, const char *chat_id);

/* Remove expired sessions (older than expiry_hours). */
int session_expire(session_store_t *store);

/* Get last_used time for a chat_id. Returns 0 if not found. */
time_t session_last_used(session_store_t *store, const char *chat_id);

/* Estimate token count for text using simple heuristic (chars / 4). */
size_t session_estimate_tokens(const char *text);

/* Add tokens to context_tokens counter for a chat_id. */
int session_add_tokens(session_store_t *store, const char *chat_id,
                       size_t tokens);

/* Get context_tokens for a chat_id. Returns 0 if not found. */
size_t session_get_context_tokens(session_store_t *store, const char *chat_id);

/* Check if memory flush needed for this session.
 * Returns 1 if context > soft_threshold AND not flushed for current compaction cycle.
 * Returns 0 otherwise. */
int session_needs_memory_flush(session_store_t *store, const char *chat_id,
                                size_t soft_threshold);

/* Mark memory flush as completed for this session.
 * Sets memory_flush_at = now, memory_flush_compaction_count = compaction_count */
int session_mark_memory_flushed(session_store_t *store, const char *chat_id);

/* Flush dirty sessions to disk. Call this periodically or on shutdown.
 * Returns RELAY_OK if clean or successfully persisted. */
int session_flush(session_store_t *store);

/* Free session store. */
void session_free(session_store_t *store);

/* ── Workspace API ──────────────────────────────────────────────────── */

/* Set workspace context for a session. */
int session_set_workspace(session_store_t *store, const char *chat_id,
                          const char *workspace_name,
                          const char *workspace_path,
                          const char *provider);

/* Get workspace name for a session. Returns "" if not set. */
const char *session_get_workspace_name(session_store_t *store,
                                        const char *chat_id);

/* Get workspace path for a session. Returns "" if not set. */
const char *session_get_workspace_path(session_store_t *store,
                                        const char *chat_id);

/* Get LLM provider for a session. Returns "" if not set. */
const char *session_get_workspace_provider(session_store_t *store,
                                            const char *chat_id);

/* ── Active workspace tracking (in-memory, resets on restart) ──────────── */

/* Get the currently active workspace name for chat_id. Returns "" if none. */
const char *session_get_active_workspace(session_store_t *store,
                                          const char *chat_id);

/* Set the active workspace for chat_id. */
void session_set_active_workspace(session_store_t *store,
                                   const char *chat_id,
                                   const char *workspace_name);

/* Get the session entry for a specific workspace. Returns NULL if not found. */
const session_entry_t *session_get_for_workspace(session_store_t *store,
                                                  const char *chat_id,
                                                  const char *workspace_name);

/* List all session entries for a chat_id.
 * Fills out[] with pointers (up to max). Returns count found. */
int session_list_for_chat(session_store_t *store, const char *chat_id,
                          const session_entry_t **out, int max);

/* Remove the session entry for a specific workspace. */
void session_close_workspace(session_store_t *store, const char *chat_id,
                              const char *workspace_name);

/* Return session_id only if the session's current workspace matches workspace_name.
 * Returns NULL if no session exists or workspace does not match (caller should
 * start a fresh Claude session rather than resuming the old one). */
const char *session_get_if_workspace_matches(session_store_t *store,
                                              const char *key,
                                              const char *workspace_name);

#endif /* RELAY_SESSION_H */
