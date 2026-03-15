#include "transcript.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ─────────────────────────────────────────────────── */

struct transcript {
    relay_fs_t *fs;
    relay_clock_t *clock;
    char dir[RELAY_MAX_PATH];
};

/* ── Helpers ────────────────────────────────────────────────────────── */

static void get_daily_path(transcript_t *tx, char *buf, size_t max)
{
    time_t now = tx->clock->now();
    struct tm tm;
    tx->clock->localtime_r(&now, &tm);

    snprintf(buf, max, "%s/%04d-%02d-%02d.jsonl",
             tx->dir,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static int log_entry(transcript_t *tx, cJSON *obj)
{
    if (!obj) {
        return RELAY_ERR_NOMEM;
    }

    /* Add timestamp */
    time_t now = tx->clock->now();
    struct tm tm;
    tx->clock->localtime_r(&now, &tm);

    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    cJSON_AddStringToObject(obj, "ts", ts);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!json) {
        return RELAY_ERR_NOMEM;
    }

    /* Append newline */
    size_t len = strlen(json);
    char *line = malloc(len + 2);
    if (!line) {
        free(json);
        return RELAY_ERR_NOMEM;
    }
    memcpy(line, json, len);
    line[len] = '\n';
    line[len + 1] = '\0';
    free(json);

    char path[RELAY_MAX_PATH];
    get_daily_path(tx, path, sizeof(path));

    int rc = tx->fs->append_file(path, line);
    free(line);
    return rc;
}

/* ── Public API ─────────────────────────────────────────────────────── */

transcript_t *transcript_create(relay_fs_t *fs, relay_clock_t *clock,
                                 const char *dir)
{
    if (!fs || !clock || !dir) {
        return NULL;
    }

    transcript_t *tx = calloc(1, sizeof(transcript_t));
    if (!tx) {
        return NULL;
    }

    tx->fs = fs;
    tx->clock = clock;
    snprintf(tx->dir, RELAY_MAX_PATH, "%s", dir);

    return tx;
}

int transcript_log_inbound(transcript_t *tx, const char *chat_id,
                           const char *text)
{
    if (!tx || !chat_id || !text) {
        return RELAY_ERR;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "direction", "in");
    cJSON_AddStringToObject(obj, "chat_id", chat_id);
    cJSON_AddStringToObject(obj, "text", text);

    return log_entry(tx, obj);
}

int transcript_log_outbound(transcript_t *tx, const char *chat_id,
                            const char *text, const char *session_id,
                            int duration_ms, int message_id)
{
    if (!tx || !chat_id || !text) {
        return RELAY_ERR;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "direction", "out");
    cJSON_AddStringToObject(obj, "chat_id", chat_id);
    cJSON_AddStringToObject(obj, "text", text);
    if (session_id) {
        cJSON_AddStringToObject(obj, "session_id", session_id);
    }
    cJSON_AddNumberToObject(obj, "duration_ms", duration_ms);
    if (message_id > 0) {
        cJSON_AddNumberToObject(obj, "message_id", message_id);
    }

    return log_entry(tx, obj);
}

int transcript_log_reaction(transcript_t *tx, const char *chat_id,
                            const char *emoji, const char *reacted_to)
{
    if (!tx || !chat_id || !emoji) {
        return RELAY_ERR;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "direction", "reaction");
    cJSON_AddStringToObject(obj, "chat_id", chat_id);
    cJSON_AddStringToObject(obj, "emoji", emoji);
    if (reacted_to && reacted_to[0] != '\0') {
        cJSON_AddStringToObject(obj, "reacted_to", reacted_to);
    }

    return log_entry(tx, obj);
}

int transcript_log_probe_outcome(transcript_t *tx, const char *chat_id,
                                  const char *thread_ts, const char *probe_ts,
                                  const char *outcome, int response_time_sec)
{
    if (!tx || !chat_id || !thread_ts || !probe_ts || !outcome)
        return RELAY_ERR;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "direction", "probe_outcome");
    cJSON_AddStringToObject(obj, "chat_id", chat_id);
    cJSON_AddStringToObject(obj, "thread_ts", thread_ts);
    cJSON_AddStringToObject(obj, "probe_ts", probe_ts);
    cJSON_AddStringToObject(obj, "outcome", outcome);
    if (response_time_sec >= 0) {
        cJSON_AddNumberToObject(obj, "response_time_sec", response_time_sec);
    }

    return log_entry(tx, obj);
}

static int find_in_file(transcript_t *tx, const char *path, int message_id,
                         char *text_out, size_t max)
{
    char *content = tx->fs->read_file(path);
    if (!content) {
        return 0;
    }

    int found = 0;
    char *line = content;
    while (*line) {
        /* Find end of line */
        char *eol = strchr(line, '\n');
        if (eol) {
            *eol = '\0';
        }

        if (*line != '\0') {
            cJSON *obj = cJSON_Parse(line);
            if (obj) {
                cJSON *dir = cJSON_GetObjectItem(obj, "direction");
                cJSON *mid = cJSON_GetObjectItem(obj, "message_id");
                cJSON *txt = cJSON_GetObjectItem(obj, "text");

                if (cJSON_IsString(dir) &&
                    strcmp(dir->valuestring, "out") == 0 &&
                    cJSON_IsNumber(mid) &&
                    (int)mid->valuedouble == message_id &&
                    cJSON_IsString(txt)) {
                    snprintf(text_out, max, "%s", txt->valuestring);
                    found = 1;
                }
                cJSON_Delete(obj);
            }
        }

        if (!eol) {
            break;
        }
        line = eol + 1;
        if (found) {
            break;
        }
    }

    free(content);
    return found;
}

int transcript_find_by_message_id(transcript_t *tx, int message_id,
                                  char *text_out, size_t max)
{
    if (!tx || message_id <= 0 || !text_out || max == 0) {
        return 0;
    }

    /* Try today's file first */
    char path[RELAY_MAX_PATH];
    get_daily_path(tx, path, sizeof(path));
    if (find_in_file(tx, path, message_id, text_out, max)) {
        return 1;
    }

    /* Try yesterday's file */
    time_t yesterday = tx->clock->now() - 86400;
    struct tm tm;
    tx->clock->localtime_r(&yesterday, &tm);
    snprintf(path, sizeof(path), "%s/%04d-%02d-%02d.jsonl",
             tx->dir,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return find_in_file(tx, path, message_id, text_out, max);
}

int transcript_read_recent(transcript_t *tx, const char *chat_id,
                            int max_entries, char *buf, size_t buf_max)
{
    if (!tx || !chat_id || !buf || buf_max == 0) {
        return RELAY_ERR;
    }

    buf[0] = '\0';

    char path[RELAY_MAX_PATH];
    get_daily_path(tx, path, sizeof(path));

    char *content = tx->fs->read_file(path);
    if (!content) {
        return RELAY_OK; /* No transcript file — empty result */
    }

    /* First pass: count matching entries and collect pointers.
     * Use a fixed-size ring buffer of line start/end pointers for the
     * most recent max_entries entries matching chat_id. */
    typedef struct { const char *start; size_t len; } span_t;
    int cap = max_entries > 0 ? max_entries : 1;
    span_t *ring = calloc((size_t)cap, sizeof(span_t));
    if (!ring) {
        free(content);
        return RELAY_ERR_NOMEM;
    }
    int ring_count = 0;
    int ring_idx = 0;

    char *line = content;
    while (*line) {
        char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        if (line_len > 0) {
            /* Temporarily null-terminate for JSON parsing */
            char saved = line[line_len];
            line[line_len] = '\0';

            cJSON *obj = cJSON_Parse(line);
            line[line_len] = saved;

            if (obj) {
                cJSON *cid = cJSON_GetObjectItem(obj, "chat_id");
                cJSON *dir = cJSON_GetObjectItem(obj, "direction");
                if (cJSON_IsString(cid) &&
                    strcmp(cid->valuestring, chat_id) == 0 &&
                    cJSON_IsString(dir) &&
                    (strcmp(dir->valuestring, "in") == 0 ||
                     strcmp(dir->valuestring, "out") == 0)) {
                    ring[ring_idx].start = line;
                    ring[ring_idx].len = line_len;
                    ring_idx = (ring_idx + 1) % cap;
                    if (ring_count < cap) ring_count++;
                }
                cJSON_Delete(obj);
            }
        }

        if (!eol) break;
        line = eol + 1;
    }

    /* Second pass: format the ring entries into buf */
    size_t pos = 0;
    int start = (ring_count < cap) ? 0 : ring_idx;
    for (int i = 0; i < ring_count; i++) {
        int idx = (start + i) % cap;
        char saved = ((char *)ring[idx].start)[ring[idx].len];
        ((char *)ring[idx].start)[ring[idx].len] = '\0';

        cJSON *obj = cJSON_Parse(ring[idx].start);
        ((char *)ring[idx].start)[ring[idx].len] = saved;

        if (obj) {
            cJSON *dir = cJSON_GetObjectItem(obj, "direction");
            cJSON *txt = cJSON_GetObjectItem(obj, "text");
            if (cJSON_IsString(dir) && cJSON_IsString(txt)) {
                const char *label = (strcmp(dir->valuestring, "in") == 0)
                                    ? "User" : "Agent";
                int written = snprintf(buf + pos, buf_max - pos,
                                       "%s: %s\n", label, txt->valuestring);
                if (written > 0 && (size_t)written < buf_max - pos) {
                    pos += (size_t)written;
                }
            }
            cJSON_Delete(obj);
        }
    }

    free(ring);
    free(content);
    return RELAY_OK;
}

void transcript_free(transcript_t *tx)
{
    free(tx);
}
