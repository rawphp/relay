#ifndef INTERRUPTION_H
#define INTERRUPTION_H

#include "relay.h"
#include <sys/types.h>

/**
 * Interruption Mechanism
 *
 * Tracks active Claude processes per chat and allows new messages to
 * interrupt running work. Simple rule: ALL messages while working are
 * interruptions - no trigger detection needed.
 */

/* Opaque session registry type */
typedef struct active_sessions active_sessions_t;

/**
 * Create session registry
 *
 * Returns: New session registry or NULL on error
 */
active_sessions_t *active_sessions_create(void);

/**
 * Register active Claude process for a chat
 *
 * Args:
 *   sessions: Session registry
 *   chat_id: Chat ID (e.g., "123456789")
 *   pid: Process ID of spawned Claude instance
 *
 * If a process is already registered for this chat, it will be killed
 * (SIGTERM then SIGKILL after grace period).
 */
void active_sessions_register(active_sessions_t *sessions,
                               const char *chat_id, pid_t pid);

/**
 * Check if a session is active for a chat
 *
 * Args:
 *   sessions: Session registry
 *   chat_id: Chat ID
 *
 * Returns: 1 if active session exists, 0 otherwise
 */
int active_sessions_has(active_sessions_t *sessions, const char *chat_id);

/**
 * Interrupt and remove session for a chat
 *
 * Sends SIGTERM to the process, waits 100ms, then SIGKILL if needed.
 * Removes from registry.
 *
 * Args:
 *   sessions: Session registry
 *   chat_id: Chat ID
 *
 * Returns: 1 if session was interrupted, 0 if no session found
 */
int active_sessions_interrupt(active_sessions_t *sessions, const char *chat_id);

/**
 * Clean up session after completion
 *
 * Call this after Claude process finishes normally.
 *
 * Args:
 *   sessions: Session registry
 *   chat_id: Chat ID
 */
void active_sessions_cleanup(active_sessions_t *sessions, const char *chat_id);

/**
 * Free session registry
 */
void active_sessions_free(active_sessions_t *sessions);

#endif /* INTERRUPTION_H */
