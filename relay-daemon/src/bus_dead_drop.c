#include "bus_dead_drop.h"
#include "relay.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Ensure directory exists, creating parent if needed */
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return RELAY_OK;
    return (mkdir(path, 0700) == 0) ? RELAY_OK : RELAY_ERR;
}

int bus_dead_drop_save(const char *ad_dir, const char *target_name,
                       const agent_bus_message_t *msg)
{
    if (!ad_dir || !target_name || !msg) return RELAY_ERR;

    /* Create ~/.relay.d/inbox/{target}/ */
    char inbox_dir[RELAY_MAX_PATH];
    snprintf(inbox_dir, sizeof(inbox_dir), "%s/inbox", ad_dir);
    if (ensure_dir(inbox_dir) != RELAY_OK) return RELAY_ERR;

    char target_dir[RELAY_MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/inbox/%s", ad_dir, target_name);
    if (ensure_dir(target_dir) != RELAY_OK) return RELAY_ERR;

    /* Build JSON line */
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return RELAY_ERR;

    cJSON_AddStringToObject(obj, "from", msg->from);
    cJSON_AddStringToObject(obj, "text", msg->text);
    cJSON_AddNumberToObject(obj, "ts", (double)msg->ts);
    cJSON_AddStringToObject(obj, "msg_id", msg->msg_id);
    cJSON_AddStringToObject(obj, "from_socket", msg->from_socket);
    cJSON_AddNumberToObject(obj, "depth", msg->depth);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return RELAY_ERR;

    /* Append to pending.jsonl */
    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/inbox/%s/pending.jsonl",
             ad_dir, target_name);

    FILE *f = fopen(path, "a");
    if (!f) { free(json); return RELAY_ERR; }
    fprintf(f, "%s\n", json);
    fclose(f);
    free(json);

    return RELAY_OK;
}

int bus_dead_drop_load(const char *ad_dir, const char *self_name,
                       char *out, size_t max)
{
    if (!ad_dir || !self_name || !out || max == 0) return 0;
    out[0] = '\0';

    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/inbox/%s/pending.jsonl",
             ad_dir, self_name);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int count = 0;
    size_t off = 0;
    char line[8192];

    off += (size_t)snprintf(out + off, max - off,
        "[Pending agent bus messages received while you were offline]\n\n");

    while (fgets(line, sizeof(line), f) && off + 1 < max) {
        /* Trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        cJSON *j_from = cJSON_GetObjectItem(obj, "from");
        cJSON *j_text = cJSON_GetObjectItem(obj, "text");

        if (cJSON_IsString(j_from) && cJSON_IsString(j_text)) {
            off += (size_t)snprintf(out + off, max - off,
                "- From %s: %s\n", j_from->valuestring, j_text->valuestring);
            count++;
        }

        cJSON_Delete(obj);
    }

    fclose(f);

    if (count > 0) {
        off += (size_t)snprintf(out + off, max - off,
            "\nRespond to these messages.\n\n");
    } else {
        out[0] = '\0';
    }

    return count;
}

int bus_dead_drop_clear(const char *ad_dir, const char *self_name)
{
    if (!ad_dir || !self_name) return RELAY_ERR;

    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/inbox/%s/pending.jsonl",
             ad_dir, self_name);

    if (unlink(path) != 0) return RELAY_ERR_NOTFOUND;
    return RELAY_OK;
}
