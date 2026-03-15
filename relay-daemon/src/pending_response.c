#include "pending_response.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PENDING_RESPONSE_FILE "data/state/pending-response.json"

static void pending_response_path(const char *workspace, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", workspace, PENDING_RESPONSE_FILE);
}

/* ── write ─────────────────────────────────────────────────────────────────── */

void pending_response_write(const char *workspace,
                            const char *chat_id,
                            const char *text)
{
    if (!workspace || !chat_id || !text) return;

    char path[512];
    pending_response_path(workspace, path, sizeof(path));

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;

    /* Ensure parent directory exists (e.g. data/state/) */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/data/state", workspace);
    mkdir(dir, 0700);

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(json, f);
        fclose(f);
    }
    free(json);
}

/* ── delete ─────────────────────────────────────────────────────────────────── */

void pending_response_delete(const char *workspace)
{
    if (!workspace) return;
    char path[512];
    pending_response_path(workspace, path, sizeof(path));
    remove(path);
}

/* ── load ───────────────────────────────────────────────────────────────────── */

int pending_response_load(const char *workspace,
                          char *chat_id_out, size_t chat_id_len,
                          char *text_out,    size_t text_len)
{
    if (!workspace) return 0;

    char path[512];
    pending_response_path(workspace, path, sizeof(path));

    if (access(path, F_OK) != 0) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) { fclose(f); return 0; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return 0;

    cJSON *chat_id_j = cJSON_GetObjectItemCaseSensitive(root, "chat_id");
    cJSON *text_j    = cJSON_GetObjectItemCaseSensitive(root, "text");

    int ok = 0;
    if (cJSON_IsString(chat_id_j) && cJSON_IsString(text_j)) {
        snprintf(chat_id_out, chat_id_len, "%s", chat_id_j->valuestring);
        snprintf(text_out,    text_len,    "%s", text_j->valuestring);
        ok = 1;
    }

    cJSON_Delete(root);
    return ok;
}
