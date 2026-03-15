#include "workspace_resolver.h"
#include "path_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Tilde expansion ────────────────────────────────────────────────── */

static void expand_path(const char *in, char *out, size_t out_size)
{
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, out_size, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, out_size, "%s", in);
}

/* ── Internal helpers ───────────────────────────────────────────────── */

static void fill_from_def(const workspace_def_t *def, resolved_workspace_t *out)
{
    snprintf(out->name,     sizeof(out->name),     "%s", def->name);
    expand_path(def->path, out->path, sizeof(out->path));
    snprintf(out->provider, sizeof(out->provider), "%s", def->provider);
}

/* ── Public API ─────────────────────────────────────────────────────── */

void workspace_resolve(session_store_t      *sessions,
                       const config_t       *cfg,
                       const char           *chat_id,
                       const char           *config_path,
                       resolved_workspace_t *out)
{
    memset(out, 0, sizeof(*out));

    /* 1. Try explicitly set active workspace */
    const char *active = session_get_active_workspace(sessions, chat_id);
    if (active && active[0] != '\0') {
        const workspace_def_t *def = config_get_workspace(cfg, active);
        if (def) {
            fill_from_def(def, out);
            out->is_fallback = 0;
            out->is_error    = 0;
            return;
        }
        /* Active workspace no longer in config — fall through to first block */
    }

    /* 2. Use first workspace block in config */
    const workspace_def_t *first = config_get_workspace_by_index(cfg, 0);
    if (first) {
        fill_from_def(first, out);
        out->is_fallback = 1;
        out->is_error    = 0;
        /* Remember this as the active workspace for next time */
        session_set_active_workspace(sessions, chat_id, first->name);
        return;
    }

    /* 3. Fall back to global workspace_path key */
    const char *global_path = config_get(cfg, "workspace_path", NULL);
    if (global_path) {
        expand_path(global_path, out->path, sizeof(out->path));
        snprintf(out->name,     sizeof(out->name),     "");
        snprintf(out->provider, sizeof(out->provider), "%s",
                 config_get(cfg, "provider", ""));
        out->is_fallback = 1;
        out->is_error    = 0;
        return;
    }

    /* 4. Fall back to install directory (derived from config_path) */
    if (config_path) {
        char install_dir[RELAY_MAX_PATH];
        if (path_util_install_dir(config_path, install_dir,
                                   sizeof(install_dir)) == RELAY_OK) {
            snprintf(out->path, sizeof(out->path), "%s", install_dir);
            /* Use directory basename as workspace name */
            const char *slash = strrchr(install_dir, '/');
            const char *bname = slash ? slash + 1 : install_dir;
            snprintf(out->name, sizeof(out->name), "%s",
                     bname[0] ? bname : "home");
            snprintf(out->provider, sizeof(out->provider), "%s",
                     config_get(cfg, "provider", ""));
            out->is_fallback = 1;
            out->is_error    = 0;
            return;
        }
    }

    /* 5. Nothing configured */
    out->is_error = 1;
}
