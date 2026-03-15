#include "pending_bus_messages.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PENDING_MAX_ENTRIES 20
#define PENDING_FILE "data/pending_bus_messages.jsonl"

static void pending_path(const char *workspace, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", workspace, PENDING_FILE);
}

/* ── save ──────────────────────────────────────────────────────────────────── */

void pending_bus_save(const char *workspace, const agent_bus_message_t *msg)
{
    if (!workspace || !msg) return;

    char path[512];
    pending_path(workspace, path, sizeof(path));

    /* Append the new entry */
    FILE *f = fopen(path, "a");
    if (!f) return;
    /* Escape text: replace " with \", newlines with \n */
    char escaped[sizeof(msg->text) * 2 + 4];
    size_t ei = 0;
    for (size_t i = 0; msg->text[i] && ei < sizeof(escaped) - 2; i++) {
        if (msg->text[i] == '"') {
            escaped[ei++] = '\\';
            escaped[ei++] = '"';
        } else if (msg->text[i] == '\n') {
            escaped[ei++] = '\\';
            escaped[ei++] = 'n';
        } else if (msg->text[i] == '\\') {
            escaped[ei++] = '\\';
            escaped[ei++] = '\\';
        } else {
            escaped[ei++] = msg->text[i];
        }
    }
    escaped[ei] = '\0';
    fprintf(f, "{\"from\":\"%s\",\"ts\":%lld,\"text\":\"%s\"}\n",
            msg->from, msg->ts, escaped);
    fclose(f);

    /* Trim to PENDING_MAX_ENTRIES: count lines; if over, rewrite keeping last N */
    f = fopen(path, "r");
    if (!f) return;
    /* Read all lines into a temporary buffer */
    char *lines[PENDING_MAX_ENTRIES * 2];
    int n = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f) && n < PENDING_MAX_ENTRIES * 2) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        lines[n] = strdup(line);
        if (lines[n]) n++;
    }
    fclose(f);

    if (n > PENDING_MAX_ENTRIES) {
        /* Rewrite keeping only last PENDING_MAX_ENTRIES */
        FILE *fw = fopen(path, "w");
        if (fw) {
            int start = n - PENDING_MAX_ENTRIES;
            for (int i = start; i < n; i++) {
                fputs(lines[i], fw);
            }
            fclose(fw);
        }
    }
    for (int i = 0; i < n; i++) free(lines[i]);
}

/* ── load ──────────────────────────────────────────────────────────────────── */

int pending_bus_load(const char *workspace, char *buf, size_t buf_len)
{
    if (!workspace || !buf || buf_len == 0) return 0;
    buf[0] = '\0';

    char path[512];
    pending_path(workspace, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    /* Build the catch-up block */
    char header[] = "[Catch-up: you missed these group chat messages due to a temporary error]\n\n";
    size_t pos = 0;
    if (strlen(header) < buf_len - 1) {
        memcpy(buf, header, strlen(header));
        pos = strlen(header);
    }

    char line[8192];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue; /* skip corrupt lines */

        cJSON *jfrom = cJSON_GetObjectItem(obj, "from");
        cJSON *jts   = cJSON_GetObjectItem(obj, "ts");
        cJSON *jtext = cJSON_GetObjectItem(obj, "text");

        const char *from = (jfrom && cJSON_IsString(jfrom)) ? jfrom->valuestring : "?";
        long long ts     = (jts   && cJSON_IsNumber(jts))   ? (long long)jts->valuedouble : 0;
        const char *text = (jtext && cJSON_IsString(jtext)) ? jtext->valuestring : "";

        /* Format timestamp */
        char ts_str[16] = "";
        if (ts > 0) {
            time_t t = (time_t)ts;
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            strftime(ts_str, sizeof(ts_str), "%H:%M", &tm_local);
        }

        /* Append formatted entry to buf */
        char entry[8192];
        int entry_len = snprintf(entry, sizeof(entry),
                                 "[From %s at %s]:\n%s\n\n", from, ts_str, text);
        if (entry_len > 0 && pos + (size_t)entry_len < buf_len - 1) {
            memcpy(buf + pos, entry, (size_t)entry_len);
            pos += (size_t)entry_len;
        }
        buf[pos] = '\0';
        count++;
        cJSON_Delete(obj);
    }
    fclose(f);

    return (count > 0) ? 1 : 0;
}

/* ── clear ─────────────────────────────────────────────────────────────────── */

void pending_bus_clear(const char *workspace)
{
    if (!workspace) return;
    char path[512];
    pending_path(workspace, path, sizeof(path));
    remove(path); /* safe to call even if file doesn't exist */
}
