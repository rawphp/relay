#ifndef RELAY_CMD_SESSIONS_H
#define RELAY_CMD_SESSIONS_H

#include "relay.h"
#include "config.h"
#include "session.h"

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

/**
 * Handle the /sessions Telegram command.
 *
 * Lists resumable Claude Code sessions for the current workspace.
 * Checks provider support first; returns unsupported message for non-Claude.
 *
 * @param fs         Filesystem (for session discovery)
 * @param sessions   Session store (for workspace resolution and session selection)
 * @param cfg        Config (for workspace definitions)
 * @param chat_id    Telegram chat ID
 * @param text       Raw message text
 * @param reply      Output buffer
 * @param reply_size Size of reply buffer
 * @return 1 if the command was handled, 0 if not a /sessions command
 */
int cmd_sessions_handle(relay_fs_t *fs,
                        session_store_t *sessions,
                        const config_t *cfg,
                        const char *chat_id,
                        const char *text,
                        char *reply,
                        size_t reply_size);

#endif /* RELAY_CMD_SESSIONS_H */
