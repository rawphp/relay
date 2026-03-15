#include "cmd_sessions.h"
#include "session_discovery.h"
#include "workspace_resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Static cache of last listing (for /session <N> selection) ─────── */

#define MAX_CACHED_SESSIONS 10

static relay_cc_session_t s_last_listing[MAX_CACHED_SESSIONS];
static int s_last_listing_count = 0;

/* ── Provider gating ───────────────────────────────────────────────── */

int cmd_sessions_provider_supported(const char *provider,
                                    char *reply, size_t reply_size)
{
    if (!provider || provider[0] == '\0' ||
        strcmp(provider, "claude") == 0) {
        return 1;
    }

    const char *display_name = provider;
    if (strcmp(provider, "openai_codex") == 0 ||
        strcmp(provider, "openai") == 0 ||
        strcmp(provider, "codex") == 0) {
        display_name = "OpenAI Codex";
    } else if (strcmp(provider, "gemini") == 0) {
        display_name = "Gemini";
    }

    snprintf(reply, reply_size,
             "Session browsing is not supported with %s.", display_name);
    return 0;
}

/* ── /session <N> — select from last listing ───────────────────────── */

static int handle_session_select(session_store_t *sessions,
                                 const char *chat_id,
                                 const char *arg,
                                 char *reply, size_t reply_size)
{
    if (!arg || arg[0] == '\0') {
        snprintf(reply, reply_size,
                 "Usage: /session <number>\n"
                 "Use /sessions to list available sessions.");
        return 1;
    }

    int index = atoi(arg);

    if (index == 0) {
        /* Start new session — clear current session_id */
        session_clear(sessions, chat_id);
        snprintf(reply, reply_size, "Starting new session.");
        return 1;
    }

    if (index < 1 || index > s_last_listing_count) {
        snprintf(reply, reply_size,
                 "Invalid session number. Use /sessions to see available sessions.");
        return 1;
    }

    const char *sid = s_last_listing[index - 1].session_id;
    session_set(sessions, chat_id, sid);
    snprintf(reply, reply_size, "Resumed session: %s", sid);
    return 1;
}

/* ── /sessions — list sessions for current workspace ───────────────── */

static int handle_sessions_list(relay_fs_t *fs,
                                session_store_t *sessions,
                                const config_t *cfg,
                                const char *chat_id,
                                char *reply, size_t reply_size)
{
    resolved_workspace_t ws;
    workspace_resolve(sessions, cfg, chat_id, NULL, &ws);

    if (ws.is_error) {
        snprintf(reply, reply_size,
                 "No workspace configured. Add a [workspace] block to relay.conf.");
        return 1;
    }

    if (!cmd_sessions_provider_supported(ws.provider, reply, reply_size)) {
        return 1;
    }

    const char *home = getenv("HOME");
    if (!home) home = "/root";

    relay_cc_session_t found[MAX_CACHED_SESSIONS];
    int count = 0;
    session_discovery_scan(fs, ws.path, home, found, MAX_CACHED_SESSIONS,
                           &count);

    /* Cache the listing for /session <N> selection */
    s_last_listing_count = count;
    if (count > 0) {
        memcpy(s_last_listing, found,
               (size_t)count * sizeof(relay_cc_session_t));
    }

    if (count == 0) {
        snprintf(reply, reply_size,
                 "No previous sessions. Next message starts a new one.");
        return 1;
    }

    char buf[2048];
    snprintf(buf, sizeof(buf), "Sessions in %s:\n",
             ws.name[0] ? ws.name : ws.path);

    for (int i = 0; i < count; i++) {
        char line[256];
        if (found[i].summary[0]) {
            snprintf(line, sizeof(line), "%d. \"%s\"\n",
                     i + 1, found[i].summary);
        } else {
            snprintf(line, sizeof(line), "%d. (no summary)\n", i + 1);
        }
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, "0. Start new session",
            sizeof(buf) - strlen(buf) - 1);

    snprintf(reply, reply_size, "%s", buf);
    return 1;
}

/* ── Public API ────────────────────────────────────────────────────── */

int cmd_sessions_handle(relay_fs_t *fs,
                        session_store_t *sessions,
                        const config_t *cfg,
                        const char *chat_id,
                        const char *text,
                        char *reply,
                        size_t reply_size)
{
    if (!text || text[0] != '/') {
        return 0;
    }

    /* /sessions — list */
    if (strcmp(text, "/sessions") == 0) {
        return handle_sessions_list(fs, sessions, cfg, chat_id,
                                    reply, reply_size);
    }

    /* /session <N> — select */
    if (strncmp(text, "/session ", 9) == 0) {
        return handle_session_select(sessions, chat_id, text + 9,
                                     reply, reply_size);
    }

    /* /session (no arg) — usage */
    if (strcmp(text, "/session") == 0) {
        snprintf(reply, reply_size,
                 "Usage: /session <number>\n"
                 "Use /sessions to list available sessions.");
        return 1;
    }

    return 0;
}
