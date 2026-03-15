#include "session_discovery.h"
#include "path_util.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Max JSONL lines to scan per file */
#define MAX_SCAN_LINES 80

/* Cache format version — bump when extraction logic changes */
#define CACHE_VERSION 3

/* ── System content detection ──────────────────────────────────────── */

/* Returns 1 if the entire text is system-injected content (not user-typed) */
static int is_system_text(const char *text)
{
    if (!text || !*text) return 1;

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n') text++;
    if (!*text) return 1;

    /* XML-style tags injected by Claude Code or IDE */
    if (*text == '<') {
        if (strncmp(text, "<ide_opened_file>", 17) == 0) return 1;
        if (strncmp(text, "<ide_selection", 14) == 0) return 1;
        if (strncmp(text, "<command-message>", 17) == 0) return 1;
        if (strncmp(text, "<command-name>", 14) == 0) return 1;
        if (strncmp(text, "<local-command-caveat>", 21) == 0) return 1;
        if (strncmp(text, "<system-reminder>", 17) == 0) return 1;
    }

    /* Identity injection */
    if (strncmp(text, "[Identity context", 17) == 0) return 1;

    /* SOUL.md / identity file content (markdown emphasis) */
    if (text[0] == '*' && text[1] >= 'A' && text[1] <= 'Z') return 1;

    /* Markdown headings from identity files */
    if (text[0] == '#') return 1;

    /* Config/system metadata */
    if (strncmp(text, "Base directory for", 18) == 0) return 1;
    if (strncmp(text, "ARGUMENTS:", 10) == 0) return 1;
    if (strncmp(text, "TRIGGER", 7) == 0) return 1;
    if (strncmp(text, "DO NOT TRIGGER", 14) == 0) return 1;

    return 0;
}

/* ── Content extraction ────────────────────────────────────────────── */

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

/* Extract text from <command-args> content — skip subcommand line */
static const char *extract_from_command_args(const char *text)
{
    const char *tag = strstr(text, "<command-args>");
    if (!tag) return NULL;
    tag += 14;
    const char *nl = strchr(tag, '\n');
    if (nl) return nl + 1;
    return tag;
}

/* Try to find a non-system line within a text block.
 * Returns pointer to first usable line, or NULL if all system. */
static const char *find_usable_line(const char *text)
{
    if (!text) return NULL;
    const char *p = text;
    int lines = 0;
    while (*p && lines < 30) {
        /* Skip whitespace at line start */
        const char *line_start = p;
        while (*p == ' ' || *p == '\t') p++;

        if (*p && *p != '\n' && !is_system_text(p)) {
            return line_start;
        }

        /* Advance to next line */
        const char *eol = strchr(p, '\n');
        if (!eol) break;
        p = eol + 1;
        lines++;
    }
    return NULL;
}

/* Extract usable text from a content value (string or array).
 * Returns 1 if a usable summary was found, 0 otherwise. */
static int extract_text_from_content(cJSON *cont, char *summary,
                                      size_t summary_max)
{
    summary[0] = '\0';

    if (cJSON_IsString(cont)) {
        const char *text = cont->valuestring;

        /* Skip leading whitespace for prefix checks */
        const char *trimmed = text;
        while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\n')
            trimmed++;

        /* If the entire message is identity injection, reject it wholesale.
         * Don't try line-by-line — everything after this prefix is identity
         * file content (SOUL.md, IDENTITY.md, USER.md, PRIORITIES.md). */
        if (strncmp(trimmed, "[Identity context", 17) == 0) return 0;

        /* Try command-args extraction first */
        const char *args_text = extract_from_command_args(text);
        if (args_text) {
            const char *clean = find_usable_line(args_text);
            if (clean) {
                copy_summary(clean, summary, summary_max);
                return 1;
            }
        }

        /* Find usable line in regular text */
        const char *clean = find_usable_line(text);
        if (clean) {
            copy_summary(clean, summary, summary_max);
            return 1;
        }
        return 0;
    }

    if (cJSON_IsArray(cont)) {
        cJSON *item;
        cJSON_ArrayForEach(item, cont) {
            cJSON *item_type = cJSON_GetObjectItem(item, "type");
            cJSON *item_text = cJSON_GetObjectItem(item, "text");
            if (cJSON_IsString(item_type) &&
                strcmp(item_type->valuestring, "text") == 0 &&
                cJSON_IsString(item_text)) {
                const char *text = item_text->valuestring;
                /* Skip identity injection blocks entirely */
                const char *t = text;
                while (*t == ' ' || *t == '\t' || *t == '\n') t++;
                if (strncmp(t, "[Identity context", 17) == 0) continue;
                const char *clean = find_usable_line(text);
                if (clean) {
                    copy_summary(clean, summary, summary_max);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Extract a useful summary from .jsonl content.
 * Scans ALL user messages (not just the first) looking for real user text.
 * Falls back to first assistant response if no user text found. */
static void extract_summary(const char *content, char *summary,
                             size_t summary_max)
{
    summary[0] = '\0';
    if (!content) return;

    char assistant_fallback[81] = {0};
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
                if (cJSON_IsString(type)) {
                    cJSON *msg = cJSON_GetObjectItem(obj, "message");
                    if (msg) {
                        cJSON *cont = cJSON_GetObjectItem(msg, "content");

                        if (strcmp(type->valuestring, "user") == 0) {
                            if (extract_text_from_content(cont, summary,
                                                          summary_max)) {
                                cJSON_Delete(obj);
                                return;
                            }
                        } else if (strcmp(type->valuestring, "assistant") == 0
                                   && !assistant_fallback[0]) {
                            /* Capture first assistant response as fallback */
                            if (cJSON_IsString(cont) &&
                                cont->valuestring[0]) {
                                const char *clean =
                                    find_usable_line(cont->valuestring);
                                if (clean) {
                                    copy_summary(clean, assistant_fallback,
                                                 sizeof(assistant_fallback));
                                }
                            }
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

    /* No user text found — use assistant fallback */
    if (assistant_fallback[0]) {
        copy_summary(assistant_fallback, summary, summary_max);
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
    if (!cache) return NULL;

    /* Check cache version — if missing or wrong, discard */
    cJSON *ver = cJSON_GetObjectItem(cache, "_version");
    if (!cJSON_IsNumber(ver) || (int)ver->valuedouble != CACHE_VERSION) {
        cJSON_Delete(cache);
        return NULL;
    }

    return cache;
}

static void save_cache(relay_fs_t *fs, const char *home, cJSON *cache)
{
    if (!cache) return;

    /* Ensure version is set */
    cJSON *ver = cJSON_GetObjectItem(cache, "_version");
    if (!ver) {
        cJSON_AddNumberToObject(cache, "_version", CACHE_VERSION);
    }

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

    char encoded[RELAY_MAX_PATH];
    path_util_encode_claude_dir(workspace_path, encoded, sizeof(encoded));

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

    cJSON *cache = load_cache(fs, home);
    int cache_dirty = 0;
    if (!cache) {
        cache = cJSON_CreateObject();
        cache_dirty = 1; /* Will need version written */
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
            char file_path[RELAY_MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s",
                     dir_path, names[i]);
            char *content = fs->read_file(file_path);
            extract_summary(content, entry->summary, sizeof(entry->summary));
            if (content) free(content);

            if (entry->summary[0]) {
                cJSON_AddStringToObject(cache, session_id, entry->summary);
                cache_dirty = 1;
            }
        }

        found++;
    }

    if (cache_dirty) {
        save_cache(fs, home, cache);
    }
    cJSON_Delete(cache);

    *count = found;
    return RELAY_OK;
}
