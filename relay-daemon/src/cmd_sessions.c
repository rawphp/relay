#include "cmd_sessions.h"

#include <stdio.h>
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
