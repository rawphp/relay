#include "cmd_sessions.h"
#include "session_discovery.h"
#include "workspace_resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_sessions_provider_supported(const char *provider,
                                    char *reply, size_t reply_size)
{
    /* Empty/NULL provider = default = claude = supported */
    if (!provider || provider[0] == '\0' ||
        strcmp(provider, "claude") == 0) {
        return 1;
    }

    /* Known unsupported providers — use friendly names */
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

int cmd_sessions_handle(relay_fs_t *fs,
                        session_store_t *sessions,
                        const config_t *cfg,
                        const char *chat_id,
                        const char *text,
                        char *reply,
                        size_t reply_size)
{
    if (!text || strcmp(text, "/sessions") != 0) {
        return 0;
    }

    /* Resolve the active workspace */
    resolved_workspace_t ws;
    workspace_resolve(sessions, cfg, chat_id, NULL, &ws);

    if (ws.is_error) {
        snprintf(reply, reply_size,
                 "No workspace configured. Add a [workspace] block to relay.conf.");
        return 1;
    }

    /* Check provider support */
    if (!cmd_sessions_provider_supported(ws.provider, reply, reply_size)) {
        return 1;
    }

    /* Get home directory */
    const char *home = getenv("HOME");
    if (!home) home = "/root";

    /* Scan for sessions */
    relay_cc_session_t found[10];
    int count = 0;
    session_discovery_scan(fs, ws.path, home, found, 10, &count);

    if (count == 0) {
        snprintf(reply, reply_size,
                 "No previous sessions. Next message starts a new one.");
        return 1;
    }

    /* Format the list */
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
