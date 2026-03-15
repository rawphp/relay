#ifndef RELAY_WORKSPACE_RESOLVER_H
#define RELAY_WORKSPACE_RESOLVER_H

#include "config.h"
#include "session.h"
#include "relay.h"

/**
 * Result of resolving the active workspace for a chat session.
 */
typedef struct {
    char name[64];              /* Workspace name ("" if global fallback) */
    char path[RELAY_MAX_PATH];  /* Workspace path */
    char provider[32];          /* Provider override ("" means use default) */
    int  is_fallback;           /* 1 = defaulted to first workspace or global */
    int  is_error;              /* 1 = no workspace configured at all */
} resolved_workspace_t;

/**
 * Resolve the active workspace for a chat session.
 *
 * Resolution order:
 *   1. Explicit active workspace (session_get_active_workspace)
 *   2. First [workspace] block in config (sets as active for next time)
 *   3. Global `workspace_path` key in config
 *   4. Install directory (derived from config_path)
 *   5. Error (is_error = 1)
 *
 * @param sessions    Session store (used to get/set active workspace)
 * @param cfg         Configuration
 * @param chat_id     Telegram chat ID
 * @param config_path Path to relay.conf (for install dir derivation; may be NULL)
 * @param out         Output struct, always populated
 */
void workspace_resolve(session_store_t      *sessions,
                       const config_t       *cfg,
                       const char           *chat_id,
                       const char           *config_path,
                       resolved_workspace_t *out);

#endif /* RELAY_WORKSPACE_RESOLVER_H */
