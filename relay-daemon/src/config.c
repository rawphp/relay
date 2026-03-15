#include "config.h"
#include "relay.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Internal types ─────────────────────────────────────────────────── */

typedef struct config_entry {
    char key[RELAY_MAX_VALUE];
    char value[RELAY_MAX_VALUE];
} config_entry_t;

struct config {
    config_entry_t *entries;
    int count;
    int capacity;
    workspace_def_t workspaces[RELAY_MAX_WORKSPACES];
    int workspace_count;
};

/* ── Helpers ────────────────────────────────────────────────────────── */

/* Trim leading and trailing whitespace in-place. Returns pointer into buf. */
static char *trim(char *buf)
{
    /* Leading */
    while (*buf && isspace((unsigned char)*buf)) {
        buf++;
    }
    /* Trailing */
    char *end = buf + strlen(buf) - 1;
    while (end > buf && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return buf;
}

/* Expand a leading ~ to $HOME in a path value.
 * Writes result into buf (size buf_size). Returns buf. */
static const char *expand_tilde(const char *value, char *buf, size_t buf_size)
{
    if (value[0] == '~' && (value[1] == '/' || value[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(buf, buf_size, "%s%s", home, value + 1);
            return buf;
        }
    }
    return value;
}

static int config_add(config_t *cfg, const char *key, const char *value)
{
    if (cfg->count >= cfg->capacity) {
        int new_cap = cfg->capacity == 0 ? 16 : cfg->capacity * 2;
        config_entry_t *new_entries = realloc(cfg->entries,
            (size_t)new_cap * sizeof(config_entry_t));
        if (!new_entries) {
            return RELAY_ERR_NOMEM;
        }
        cfg->entries = new_entries;
        cfg->capacity = new_cap;
    }

    char expanded[RELAY_MAX_VALUE];
    const char *stored = expand_tilde(value, expanded, sizeof(expanded));

    snprintf(cfg->entries[cfg->count].key, RELAY_MAX_VALUE, "%s", key);
    snprintf(cfg->entries[cfg->count].value, RELAY_MAX_VALUE, "%s", stored);
    cfg->count++;
    return RELAY_OK;
}

static int config_parse(config_t *cfg, const char *text)
{
    /* Work on a mutable copy */
    size_t len = strlen(text);
    char *buf = malloc(len + 1);
    if (!buf) {
        return RELAY_ERR_NOMEM;
    }
    memcpy(buf, text, len + 1);

    /* Current workspace being built; NULL when not in a workspace block */
    workspace_def_t *cur_ws = NULL;

    char *line = buf;
    while (line && *line) {
        /* Find end of line */
        char *eol = strchr(line, '\n');
        if (eol) {
            *eol = '\0';
        }

        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            line = eol ? eol + 1 : NULL;
            continue;
        }

        /* Section header — check for [workspace "name"] */
        if (trimmed[0] == '[') {
            /* End current workspace block if any */
            cur_ws = NULL;

            /* Match [workspace "name"] */
            const char *prefix = "[workspace \"";
            size_t plen = strlen(prefix);
            if (strncmp(trimmed, prefix, plen) == 0) {
                char *name_start = trimmed + plen;
                char *name_end = strchr(name_start, '"');
                if (name_end && name_end > name_start &&
                    cfg->workspace_count < RELAY_MAX_WORKSPACES) {
                    workspace_def_t *ws =
                        &cfg->workspaces[cfg->workspace_count++];
                    memset(ws, 0, sizeof(*ws));
                    size_t name_len = (size_t)(name_end - name_start);
                    if (name_len >= sizeof(ws->name)) {
                        name_len = sizeof(ws->name) - 1;
                    }
                    memcpy(ws->name, name_start, name_len);
                    ws->name[name_len] = '\0';
                    cur_ws = ws;
                }
                /* else: too many workspaces — silently skip */
            }

            line = eol ? eol + 1 : NULL;
            continue;
        }

        /* Find the = separator */
        char *eq = strchr(trimmed, '=');
        if (!eq) {
            /* Malformed line — skip */
            line = eol ? eol + 1 : NULL;
            continue;
        }

        /* Split into key and value */
        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);

        if (*key == '\0') {
            line = eol ? eol + 1 : NULL;
            continue;
        }

        if (cur_ws) {
            /* Inside a workspace block: populate path or provider */
            char expanded[RELAY_MAX_VALUE];
            const char *stored = expand_tilde(value, expanded,
                                              sizeof(expanded));
            if (strcmp(key, "path") == 0) {
                snprintf(cur_ws->path, sizeof(cur_ws->path), "%s", stored);
            } else if (strcmp(key, "provider") == 0) {
                snprintf(cur_ws->provider, sizeof(cur_ws->provider),
                         "%s", value);
            }
        } else {
            config_add(cfg, key, value);
        }

        line = eol ? eol + 1 : NULL;
    }

    free(buf);
    return RELAY_OK;
}

/* ── Public API ─────────────────────────────────────────────────────── */

config_t *config_load_string(const char *text)
{
    if (!text) {
        return NULL;
    }

    config_t *cfg = calloc(1, sizeof(config_t));
    if (!cfg) {
        return NULL;
    }

    if (config_parse(cfg, text) != RELAY_OK) {
        config_free(cfg);
        return NULL;
    }

    return cfg;
}

config_t *config_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);

    config_t *cfg = config_load_string(buf);
    free(buf);
    return cfg;
}

const char *config_get(const config_t *cfg, const char *key,
                       const char *fallback)
{
    if (!cfg || !key) {
        return fallback;
    }

    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            return cfg->entries[i].value;
        }
    }

    return fallback;
}

int config_get_int(const config_t *cfg, const char *key, int fallback)
{
    const char *val = config_get(cfg, key, NULL);
    if (!val) {
        return fallback;
    }

    char *endptr;
    long result = strtol(val, &endptr, 10);

    /* If endptr didn't advance past the string, it's not a valid number */
    if (endptr == val || *endptr != '\0') {
        return fallback;
    }

    return (int)result;
}

int config_validate(const config_t *cfg, const char *required[],
                    int num_required, char errors[][RELAY_MAX_VALUE],
                    int max_errors)
{
    if (!cfg) {
        return -1;
    }

    int missing = 0;
    for (int i = 0; i < num_required; i++) {
        const char *val = config_get(cfg, required[i], NULL);
        if (!val || val[0] == '\0') {
            if (missing < max_errors) {
                snprintf(errors[missing], RELAY_MAX_VALUE,
                         "%s", required[i]);
            }
            missing++;
        }
    }
    return missing;
}

void config_free(config_t *cfg)
{
    if (!cfg) {
        return;
    }
    free(cfg->entries);
    free(cfg);
}

/* ── Workspace API ──────────────────────────────────────────────────── */

int config_get_workspace_count(const config_t *cfg)
{
    if (!cfg) {
        return 0;
    }
    return cfg->workspace_count;
}

const workspace_def_t *config_get_workspace(const config_t *cfg,
                                             const char *name)
{
    if (!cfg || !name) {
        return NULL;
    }
    for (int i = 0; i < cfg->workspace_count; i++) {
        if (strcmp(cfg->workspaces[i].name, name) == 0) {
            return &cfg->workspaces[i];
        }
    }
    return NULL;
}

const workspace_def_t *config_get_workspace_by_index(const config_t *cfg,
                                                      int i)
{
    if (!cfg || i < 0 || i >= cfg->workspace_count) {
        return NULL;
    }
    return &cfg->workspaces[i];
}
