#include "cmd_workspace.h"

#include <stdio.h>
#include <string.h>

/* ── Internal helpers ───────────────────────────────────────────────── */

static void handle_session_switch(session_store_t *sessions,
                                  const config_t  *cfg,
                                  const char      *chat_id,
                                  const char      *arg,
                                  char            *reply,
                                  size_t           reply_size)
{
    if (!arg || arg[0] == '\0') {
        snprintf(reply, reply_size,
                 "Usage: /space <name>\n"
                 "Use /spaces to list available workspaces.");
        return;
    }

    /* Strip surrounding quotes (single or double) added by some Telegram clients */
    char name[256];
    size_t len = strlen(arg);
    if (len >= 2 &&
        ((arg[0] == '"'  && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) {
        size_t inner = len - 2;
        if (inner >= sizeof(name)) inner = sizeof(name) - 1;
        memcpy(name, arg + 1, inner);
        name[inner] = '\0';
        arg = name;
    }

    const workspace_def_t *ws = config_get_workspace(cfg, arg);
    if (!ws) {
        snprintf(reply, reply_size, "Workspace not found: %s", arg);
        return;
    }

    session_set_active_workspace(sessions, chat_id, ws->name);
    snprintf(reply, reply_size,
             "Switched to workspace: %s (%s)", ws->name, ws->path);
}

static void handle_sessions_list(session_store_t *sessions,
                                 const config_t  *cfg,
                                 const char      *chat_id,
                                 char            *reply,
                                 size_t           reply_size)
{
    int count = config_get_workspace_count(cfg);
    if (count == 0) {
        snprintf(reply, reply_size, "No workspaces configured.");
        return;
    }

    const char *active = session_get_active_workspace(sessions, chat_id);

    char buf[1024];
    if (active && active[0] != '\0') {
        snprintf(buf, sizeof(buf), "Active: %s\nWorkspaces:\n", active);
    } else {
        snprintf(buf, sizeof(buf), "Active: none\nWorkspaces:\n");
    }
    for (int i = 0; i < count; i++) {
        const workspace_def_t *ws = config_get_workspace_by_index(cfg, i);
        if (!ws) continue;

        char line[256];
        int is_active = (active && active[0] != '\0' &&
                         strcmp(active, ws->name) == 0);
        snprintf(line, sizeof(line),
                 "%s %s \342\200\224 %s (%s)\n",
                 is_active ? "*" : " ",
                 ws->name,
                 ws->path,
                 ws->provider[0] ? ws->provider : "default");
        strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
    }

    /* Append global workspace_path fallback if set and not already listed */
    const char *fallback_path = config_get(cfg, "workspace_path", NULL);
    if (fallback_path && fallback_path[0] != '\0') {
        int already_listed = 0;
        for (int i = 0; i < count; i++) {
            const workspace_def_t *ws = config_get_workspace_by_index(cfg, i);
            if (ws && strcmp(ws->path, fallback_path) == 0) {
                already_listed = 1;
                break;
            }
        }
        if (!already_listed) {
            int is_active = (!active || active[0] == '\0');
            char line[256];
            snprintf(line, sizeof(line),
                     "%s (default) \342\200\224 %s\n",
                     is_active ? "*" : " ",
                     fallback_path);
            strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
    }

    strncat(buf, "\nUse /space <name> to switch.",
            sizeof(buf) - strlen(buf) - 1);
    snprintf(reply, reply_size, "%s", buf);
}

static void handle_workspace_info(session_store_t *sessions,
                                  const config_t  *cfg,
                                  const char      *chat_id,
                                  char            *reply,
                                  size_t           reply_size)
{
    const char *active = session_get_active_workspace(sessions, chat_id);
    if (!active || active[0] == '\0') {
        snprintf(reply, reply_size,
                 "No active workspace. Use /space <name> to switch.");
        return;
    }

    const workspace_def_t *ws = config_get_workspace(cfg, active);
    if (!ws) {
        snprintf(reply, reply_size,
                 "Active workspace '%s' not found in config.", active);
        return;
    }

    snprintf(reply, reply_size,
             "Workspace: %s\nPath: %s\nProvider: %s",
             ws->name,
             ws->path,
             ws->provider[0] ? ws->provider : "default");
}

static void handle_close(session_store_t *sessions,
                         const char      *chat_id,
                         char            *reply,
                         size_t           reply_size)
{
    const char *active = session_get_active_workspace(sessions, chat_id);
    if (!active || active[0] == '\0') {
        snprintf(reply, reply_size,
                 "No active workspace session to close.");
        return;
    }

    session_close_workspace(sessions, chat_id, active);
    snprintf(reply, reply_size,
             "Session closed for workspace: %s", active);
}

/* ── Public API ─────────────────────────────────────────────────────── */

int cmd_workspace_handle(session_store_t *sessions,
                         const config_t  *cfg,
                         const char      *chat_id,
                         const char      *text,
                         char            *reply,
                         size_t           reply_size)
{
    if (!text || text[0] != '/') {
        return 0;
    }

    /* /space <name> — switch workspace */
    if (strncmp(text, "/space ", 7) == 0) {
        handle_session_switch(sessions, cfg, chat_id,
                              text + 7, reply, reply_size);
        return 1;
    }

    /* /spaces and /space (no arg) — list workspaces */
    if (strcmp(text, "/spaces") == 0 || strcmp(text, "/space") == 0) {
        handle_sessions_list(sessions, cfg, chat_id, reply, reply_size);
        return 1;
    }

    /* /workspace */
    if (strcmp(text, "/workspace") == 0) {
        handle_workspace_info(sessions, cfg, chat_id, reply, reply_size);
        return 1;
    }

    /* /close and /clear */
    if (strcmp(text, "/close") == 0 || strcmp(text, "/clear") == 0) {
        handle_close(sessions, chat_id, reply, reply_size);
        return 1;
    }

    return 0;
}
