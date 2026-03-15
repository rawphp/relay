#ifndef RELAY_CONFIG_H
#define RELAY_CONFIG_H

#include "relay.h"

/* ── Config ─────────────────────────────────────────────────────────── */
/* Key-value config file parser. Format: key = value (one per line).    */
/* Lines starting with # are comments. Blank lines are ignored.         */

/* ── Workspace definition ───────────────────────────────────────────── */

#define RELAY_MAX_WORKSPACES 16

typedef struct {
    char name[64];
    char path[RELAY_MAX_PATH];
    char provider[32];       /* e.g. "claude", "openai", "gemini" */
} workspace_def_t;

/* Opaque config type */
typedef struct config config_t;

/* Load config from file. Returns NULL on error. */
config_t *config_load(const char *path);

/* Load config from string (for testing). Returns NULL on error. */
config_t *config_load_string(const char *text);

/* Get string value. Returns fallback if key not found. */
const char *config_get(const config_t *cfg, const char *key,
                       const char *fallback);

/* Get integer value. Returns fallback if key not found or not a number. */
int config_get_int(const config_t *cfg, const char *key, int fallback);

/* Validate that required keys are present.
 * Returns number of missing keys (0 = all present).
 * Returns -1 if cfg is NULL.
 * Missing key names written to errors[] (up to max_errors). */
int config_validate(const config_t *cfg, const char *required[],
                    int num_required, char errors[][2048],
                    int max_errors);

/* Free config. */
void config_free(config_t *cfg);

/* ── Workspace API ──────────────────────────────────────────────────── */

/* Returns the number of workspace definitions loaded from config. */
int config_get_workspace_count(const config_t *cfg);

/* Returns a pointer to the workspace with the given name, or NULL. */
const workspace_def_t *config_get_workspace(const config_t *cfg,
                                             const char *name);

/* Returns a pointer to the workspace at index i, or NULL if out of range. */
const workspace_def_t *config_get_workspace_by_index(const config_t *cfg,
                                                      int i);

#endif /* RELAY_CONFIG_H */
