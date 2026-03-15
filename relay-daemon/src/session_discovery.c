#include "session_discovery.h"
#include "path_util.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Max lines to scan per .jsonl file looking for first user message */
#define MAX_SCAN_LINES 50

/* ── System prefix detection ───────────────────────────────────────── */

/* Prefixes to skip when extracting user text from content strings.
 * These are injected by Claude Code or relay, not typed by the user. */
static int is_system_line(const char *line)
{
    if (!line) return 0;
    /* Skip whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '\n') return 1; /* blank line */

    if (strncmp(line, "[Identity context", 17) == 0) return 1;
    if (strncmp(line, "<command-message>", 17) == 0) return 1;
    if (strncmp(line, "<command-name>", 14) == 0) return 1;
    if (strncmp(line, "<command-args>", 14) == 0) return 1;
    if (strncmp(line, "<ide_opened_file>", 17) == 0) return 1;
    if (strncmp(line, "<ide_selection", 14) == 0) return 1;
    if (strncmp(line, "Base directory for", 18) == 0) return 1;
    if (strncmp(line, "## SOUL.md", 10) == 0) return 1;
    if (strncmp(line, "## IDENTITY.md", 14) == 0) return 1;
    if (strncmp(line, "## USER.md", 10) == 0) return 1;
    if (strncmp(line, "## PRIORITIES.md", 16) == 0) return 1;
    if (*line == '#' && *(line + 1) == ' ') return 1; /* markdown heading */
    return 0;
}

/* Find the first non-system line in text. Returns pointer into text. */
static const char *skip_system_prefixes(const char *text)
{
    if (!text) return text;
    const char *p = text;
    while (*p) {
        /* Skip system lines */
        if (!is_system_line(p)) {
            return p;
        }
        /* Advance to next line */
        const char *eol = strchr(p, '\n');
        if (!eol) return p; /* last line, return even if system */
        p = eol + 1;
    }
    return p;
}

/* Extract text from <command-args>...\n content */
static const char *extract_from_command_args(const char *text)
{
    const char *tag = strstr(text, "<command-args>");
    if (!tag) return NULL;
    tag += 14; /* skip "<command-args>" */
    /* Skip the first token (the subcommand like "start\n") */
    const char *nl = strchr(tag, '\n');
    if (nl) return nl + 1;
    return tag;
}

/* ── Content extraction ────────────────────────────────────────────── */

/* Copy at most max_copy chars of src into dst, NUL-terminated.
 * Stops at first newline. */
static void copy_summary(const char *src, char *dst, size_t dst_max)
{
    size_t max_copy = dst_max - 1;
    size_t i = 0;
    while (i < max_copy && src[i] && src[i] != '\n') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Extract text from a content value (string or array).
 * Handles system prefix stripping and command-args extraction. */
static void extract_text_from_content(cJSON *cont, char *summary,
                                       size_t summary_max)
{
    summary[0] = '\0';

    if (cJSON_IsString(cont)) {
        const char *text = cont->valuestring;

        /* Try command-args extraction first */
        const char *args_text = extract_from_command_args(text);
        if (args_text && *args_text) {
            const char *clean = skip_system_prefixes(args_text);
            if (clean && *clean) {
                copy_summary(clean, summary, summary_max);
                return;
            }
        }

        /* Strip system prefixes from regular text */
        const char *clean = skip_system_prefixes(text);
        if (clean && *clean) {
            copy_summary(clean, summary, summary_max);
        }
        return;
    }

    if (cJSON_IsArray(cont)) {
        /* VS Code IDE format: [{type:"text", text:"..."}, ...] */
        cJSON *item;
        cJSON_ArrayForEach(item, cont) {
            cJSON *item_type = cJSON_GetObjectItem(item, "type");
            cJSON *item_text = cJSON_GetObjectItem(item, "text");
            if (cJSON_IsString(item_type) &&
                strcmp(item_type->valuestring, "text") == 0 &&
                cJSON_IsString(item_text)) {
                const char *text = item_text->valuestring;
                const char *clean = skip_system_prefixes(text);
                if (clean && *clean) {
                    copy_summary(clean, summary, summary_max);
                    return;
                }
            }
        }
    }
}

/* Extract the first user message from a .jsonl file's content. */
static void extract_summary(const char *content, char *summary,
                             size_t summary_max)
{
    summary[0] = '\0';
    if (!content) return;

    const char *line = content;
    int lines_scanned = 0;

    while (*line && lines_scanned < MAX_SCAN_LINES) {
        const char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        if (line_len > 0) {
            char *buf = malloc(line_len + 1);
            if (!buf) break;
            memcpy(buf, line, line_len);
            buf[line_len] = '\0';

            cJSON *obj = cJSON_Parse(buf);
            free(buf);

            if (obj) {
                cJSON *type = cJSON_GetObjectItem(obj, "type");
                if (cJSON_IsString(type) &&
                    strcmp(type->valuestring, "user") == 0) {
                    cJSON *msg = cJSON_GetObjectItem(obj, "message");
                    if (msg) {
                        cJSON *cont = cJSON_GetObjectItem(msg, "content");
                        extract_text_from_content(cont, summary, summary_max);
                        if (summary[0]) {
                            cJSON_Delete(obj);
                            return;
                        }
                    }
                }
                cJSON_Delete(obj);
            }
        }

        if (!eol) break;
        line = eol + 1;
        lines_scanned++;
    }
}

/* ── Summary cache ─────────────────────────────────────────────────── */

static cJSON *load_cache(relay_fs_t *fs, const char *home)
{
    char cache_path[RELAY_MAX_PATH];
    snprintf(cache_path, sizeof(cache_path),
             "%s/.relay-session-cache.json", home);

    char *data = fs->read_file(cache_path);
    if (!data) return NULL;

    cJSON *cache = cJSON_Parse(data);
    free(data);
    return cache;
}

static void save_cache(relay_fs_t *fs, const char *home, cJSON *cache)
{
    if (!cache) return;
    char cache_path[RELAY_MAX_PATH];
    snprintf(cache_path, sizeof(cache_path),
             "%s/.relay-session-cache.json", home);

    char *json = cJSON_PrintUnformatted(cache);
    if (json) {
        fs->write_file(cache_path, json);
        free(json);
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

int session_discovery_scan(relay_fs_t *fs, const char *workspace_path,
                           const char *home,
                           relay_cc_session_t *out, int max, int *count)
{
    *count = 0;

    if (!fs || !workspace_path || !home || !out || max <= 0) {
        return RELAY_OK;
    }

    /* Encode workspace path to .claude directory name */
    char encoded[RELAY_MAX_PATH];
    path_util_encode_claude_dir(workspace_path, encoded, sizeof(encoded));

    /* Build the full directory path */
    char dir_path[RELAY_MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/.claude/projects/%s",
             home, encoded);

    if (!fs->list_dir) {
        return RELAY_OK;
    }

    char names[32][256];
    int file_count = fs->list_dir(dir_path, ".jsonl", names, 32);
    if (file_count <= 0) {
        return RELAY_OK;
    }

    /* Load summary cache */
    cJSON *cache = load_cache(fs, home);
    int cache_dirty = 0;
    if (!cache) {
        cache = cJSON_CreateObject();
    }

    int found = 0;
    for (int i = 0; i < file_count && found < max; i++) {
        char session_id[RELAY_MAX_SESSION_ID];
        snprintf(session_id, sizeof(session_id), "%s", names[i]);
        size_t name_len = strlen(session_id);
        if (name_len > 6) {
            session_id[name_len - 6] = '\0';
        }

        relay_cc_session_t *entry = &out[found];
        snprintf(entry->session_id, sizeof(entry->session_id),
                 "%s", session_id);
        entry->last_activity = 0;

        /* Check cache first */
        cJSON *cached = cJSON_GetObjectItem(cache, session_id);
        if (cJSON_IsString(cached)) {
            copy_summary(cached->valuestring, entry->summary,
                         sizeof(entry->summary));
        } else {
            /* Extract from .jsonl file */
            char file_path[RELAY_MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s",
                     dir_path, names[i]);
            char *content = fs->read_file(file_path);
            extract_summary(content, entry->summary, sizeof(entry->summary));
            if (content) free(content);

            /* Cache the result */
            if (entry->summary[0]) {
                cJSON_AddStringToObject(cache, session_id, entry->summary);
                cache_dirty = 1;
            }
        }

        found++;
    }

    /* Persist cache if we added new entries */
    if (cache_dirty) {
        save_cache(fs, home, cache);
    }
    cJSON_Delete(cache);

    *count = found;
    return RELAY_OK;
}
