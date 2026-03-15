#include "interruption.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * Interruption Mechanism Implementation
 *
 * Simple in-memory session tracking with PID management.
 * No state files, no Python, just C.
 */

#define MAX_SESSIONS 64  /* Max concurrent chats */

typedef struct {
    char chat_id[RELAY_MAX_USER_ID];
    pid_t pid;
    int active;  /* 1 if slot is in use */
} session_entry_t;

struct active_sessions {
    session_entry_t entries[MAX_SESSIONS];
};

active_sessions_t *active_sessions_create(void)
{
    active_sessions_t *sessions = calloc(1, sizeof(active_sessions_t));
    if (!sessions) {
        return NULL;
    }

    /* calloc zeros memory, so all entries start inactive */
    return sessions;
}

static int find_session_index(active_sessions_t *sessions, const char *chat_id)
{
    if (!sessions || !chat_id || chat_id[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions->entries[i].active &&
            strcmp(sessions->entries[i].chat_id, chat_id) == 0) {
            return i;
        }
    }

    return -1;
}

static int find_free_slot(active_sessions_t *sessions)
{
    if (!sessions) {
        return -1;
    }

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions->entries[i].active) {
            return i;
        }
    }

    return -1;
}

static void kill_process(pid_t pid)
{
    if (pid <= 0) {
        return;
    }

    /* Send SIGTERM for graceful shutdown */
    kill(pid, SIGTERM);

    /* Wait 100ms */
    usleep(100000);

    /* Force kill if still running */
    kill(pid, SIGKILL);

    /* Reap zombie */
    waitpid(pid, NULL, WNOHANG);
}

void active_sessions_register(active_sessions_t *sessions,
                               const char *chat_id, pid_t pid)
{
    if (!sessions || !chat_id || chat_id[0] == '\0') {
        return;
    }

    /* Check if session already exists - if so, interrupt it */
    int existing = find_session_index(sessions, chat_id);
    if (existing >= 0) {
        /* Kill old process if valid */
        if (sessions->entries[existing].pid > 0) {
            kill_process(sessions->entries[existing].pid);
        }

        /* Reuse slot */
        sessions->entries[existing].pid = pid;
        return;
    }

    /* Find free slot */
    int slot = find_free_slot(sessions);
    if (slot < 0) {
        /* Registry full - replace oldest (slot 0) */
        slot = 0;
        if (sessions->entries[slot].pid > 0) {
            kill_process(sessions->entries[slot].pid);
        }
    }

    /* Register new session */
    snprintf(sessions->entries[slot].chat_id, RELAY_MAX_USER_ID, "%s", chat_id);
    sessions->entries[slot].pid = pid;
    sessions->entries[slot].active = 1;
}

int active_sessions_has(active_sessions_t *sessions, const char *chat_id)
{
    return find_session_index(sessions, chat_id) >= 0;
}

int active_sessions_interrupt(active_sessions_t *sessions, const char *chat_id)
{
    if (!sessions || !chat_id || chat_id[0] == '\0') {
        return 0;
    }

    int idx = find_session_index(sessions, chat_id);
    if (idx < 0) {
        return 0;
    }

    /* Kill the process if valid PID */
    if (sessions->entries[idx].pid > 0) {
        kill_process(sessions->entries[idx].pid);
    }

    /* Mark slot as inactive */
    sessions->entries[idx].active = 0;
    sessions->entries[idx].pid = 0;
    sessions->entries[idx].chat_id[0] = '\0';

    return 1;
}

void active_sessions_cleanup(active_sessions_t *sessions, const char *chat_id)
{
    if (!sessions || !chat_id || chat_id[0] == '\0') {
        return;
    }

    int idx = find_session_index(sessions, chat_id);
    if (idx < 0) {
        return;
    }

    /* Just mark as inactive - process already finished */
    sessions->entries[idx].active = 0;
    sessions->entries[idx].pid = 0;
    sessions->entries[idx].chat_id[0] = '\0';
}

void active_sessions_free(active_sessions_t *sessions)
{
    if (!sessions) {
        return;
    }

    /* Kill any remaining active processes */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions->entries[i].active && sessions->entries[i].pid > 0) {
            kill_process(sessions->entries[i].pid);
        }
    }

    free(sessions);
}
