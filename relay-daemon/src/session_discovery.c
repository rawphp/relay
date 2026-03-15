#include "session_discovery.h"
#include "path_util.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Max lines to scan per .jsonl file looking for first user message */
#define MAX_SCAN_LINES 20

/* Extract the first user message from a .jsonl file's content.
 * Writes into summary (up to summary_max - 1 chars). */
static void extract_summary(const char *content, char *summary, size_t summary_max)
{
    summary[0] = '\0';
    if (!content) return;

    const char *line = content;
    int lines_scanned = 0;

    while (*line && lines_scanned < MAX_SCAN_LINES) {
        /* Find end of line */
        const char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        if (line_len > 0) {
            /* Parse this line as JSON */
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
                        if (cJSON_IsString(cont)) {
                            size_t max_copy = summary_max - 1;
                            size_t src_len = strlen(cont->valuestring);
                            size_t copy_len = src_len < max_copy ? src_len : max_copy;
                            memcpy(summary, cont->valuestring, copy_len);
                            summary[copy_len] = '\0';
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

    /* Build the full directory path: <home>/.claude/projects/<encoded>/ */
    char dir_path[RELAY_MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s/.claude/projects/%s",
             home, encoded);

    /* List .jsonl files — if list_dir is NULL or dir doesn't exist, count=0 */
    if (!fs->list_dir) {
        return RELAY_OK;
    }

    char names[32][256];
    int file_count = fs->list_dir(dir_path, ".jsonl", names, 32);
    if (file_count <= 0) {
        return RELAY_OK;
    }

    int found = 0;
    for (int i = 0; i < file_count && found < max; i++) {
        /* Extract session_id from filename (strip .jsonl suffix) */
        char session_id[RELAY_MAX_SESSION_ID];
        snprintf(session_id, sizeof(session_id), "%s", names[i]);
        size_t name_len = strlen(session_id);
        if (name_len > 6) { /* ".jsonl" = 6 chars */
            session_id[name_len - 6] = '\0';
        }

        /* Read the file content to extract summary */
        char file_path[RELAY_MAX_PATH];
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, names[i]);

        char *content = fs->read_file(file_path);

        relay_cc_session_t *entry = &out[found];
        snprintf(entry->session_id, sizeof(entry->session_id),
                 "%s", session_id);
        extract_summary(content, entry->summary, sizeof(entry->summary));
        entry->last_activity = 0; /* Mock fs doesn't provide mtime */

        if (content) free(content);
        found++;
    }

    *count = found;
    return RELAY_OK;
}
