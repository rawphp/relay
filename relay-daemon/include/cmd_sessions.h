#ifndef RELAY_CMD_SESSIONS_H
#define RELAY_CMD_SESSIONS_H

#include <stddef.h>

/**
 * Check if the given LLM provider supports session browsing.
 *
 * @param provider   Provider name (e.g. "claude", "openai_codex", "gemini")
 * @param reply      Output buffer — filled with unsupported message if not supported
 * @param reply_size Size of reply buffer
 * @return 1 if supported (reply not modified), 0 if not supported (reply filled)
 */
int cmd_sessions_provider_supported(const char *provider,
                                    char *reply, size_t reply_size);

#endif /* RELAY_CMD_SESSIONS_H */
