#include "agent_advertise.h"
#include "relay.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int agent_advertise_publish(const char *dir, const char *name,
                            const char *socket_path)
{
    if (!dir || !name || !socket_path) return RELAY_ERR;

    /* Create directory if needed (mode 0700 — owner only) */
    struct stat st;
    if (stat(dir, &st) != 0) {
        if (mkdir(dir, 0700) != 0) return RELAY_ERR;
    }

    /* Build JSON */
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return RELAY_ERR;

    cJSON_AddStringToObject(obj, "name", name);
    cJSON_AddNumberToObject(obj, "pid", (double)getpid());
    cJSON_AddStringToObject(obj, "socket", socket_path);
    cJSON_AddNumberToObject(obj, "started", (double)time(NULL));

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return RELAY_ERR;

    /* Write to {dir}/{name}.json */
    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.json", dir, name);

    FILE *f = fopen(path, "w");
    if (!f) {
        free(json);
        return RELAY_ERR;
    }
    fputs(json, f);
    fclose(f);
    free(json);

    return RELAY_OK;
}

int agent_advertise_withdraw(const char *dir, const char *name)
{
    if (!dir || !name) return RELAY_ERR;

    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.json", dir, name);

    if (unlink(path) != 0) return RELAY_ERR_NOTFOUND;
    return RELAY_OK;
}
