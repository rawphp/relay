#ifndef RELAY_CMD_WORKSPACE_H
#define RELAY_CMD_WORKSPACE_H

#include "config.h"
#include "session.h"

/**
 * Handle a workspace-related Telegram command.
 *
 * Inspects `text` for:
 *   /session <name>  — switch active workspace
 *   /sessions        — list all configured workspaces
 *   /workspace       — show current workspace info
 *   /close           — close current workspace session
 *   /clear           — alias for /close
 *
 * @param sessions   Session store
 * @param cfg        Config (for workspace definitions)
 * @param chat_id    Telegram chat ID of the sender
 * @param text       Raw message text (starts with '/')
 * @param reply      Output buffer populated with reply text
 * @param reply_size Size of `reply` buffer
 * @return 1 if the command was handled (caller must not forward to LLM),
 *         0 if the command is not a workspace command
 */
int cmd_workspace_handle(session_store_t *sessions,
                         const config_t  *cfg,
                         const char      *chat_id,
                         const char      *text,
                         char            *reply,
                         size_t           reply_size);

#endif /* RELAY_CMD_WORKSPACE_H */
