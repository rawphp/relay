#include "profiler.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int enabled;
    FILE *file;
    char path[RELAY_MAX_PATH];
    pthread_mutex_t mutex;
} profiler_state_t;

typedef struct {
    char request_id[96];
    char chat_id[RELAY_MAX_USER_ID];
    char provider[64];
} profiler_context_t;

static profiler_state_t g_profiler = {
    .enabled = 0,
    .file = NULL,
    .path = "",
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static __thread profiler_context_t g_ctx = {{0}, {0}, {0}};

static long long now_epoch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static void json_escape(const char *src, char *dst, size_t max)
{
    size_t j = 0;
    if (!dst || max == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    for (size_t i = 0; src[i] != '\0' && j + 2 < max; i++) {
        char c = src[i];
        if (c == '\"' || c == '\\') {
            if (j + 2 >= max) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= max) break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            if (j + 2 >= max) break;
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t') {
            if (j + 2 >= max) break;
            dst[j++] = '\\';
            dst[j++] = 't';
        } else if ((unsigned char)c < 0x20) {
            continue;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static int ensure_parent_dirs(const char *path)
{
    char tmp[RELAY_MAX_PATH];
    if (!path || path[0] == '\0') {
        return RELAY_ERR_INVALID;
    }

    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        return RELAY_OK;
    }

    *slash = '\0';
    if (tmp[0] == '\0') {
        return RELAY_OK;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return RELAY_ERR_IO;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return RELAY_ERR_IO;
    }
    return RELAY_OK;
}

int profiler_init(const char *path, int enabled)
{
    pthread_mutex_lock(&g_profiler.mutex);

    g_profiler.enabled = 0;
    if (g_profiler.file) {
        fclose(g_profiler.file);
        g_profiler.file = NULL;
    }
    g_profiler.path[0] = '\0';

    if (!enabled) {
        pthread_mutex_unlock(&g_profiler.mutex);
        return RELAY_OK;
    }
    if (!path || path[0] == '\0') {
        pthread_mutex_unlock(&g_profiler.mutex);
        return RELAY_ERR_INVALID;
    }

    if (ensure_parent_dirs(path) != RELAY_OK) {
        pthread_mutex_unlock(&g_profiler.mutex);
        return RELAY_ERR_IO;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        pthread_mutex_unlock(&g_profiler.mutex);
        return RELAY_ERR_IO;
    }

    g_profiler.file = f;
    g_profiler.enabled = 1;
    snprintf(g_profiler.path, sizeof(g_profiler.path), "%s", path);
    pthread_mutex_unlock(&g_profiler.mutex);
    return RELAY_OK;
}

void profiler_close(void)
{
    pthread_mutex_lock(&g_profiler.mutex);
    if (g_profiler.file) {
        fclose(g_profiler.file);
        g_profiler.file = NULL;
    }
    g_profiler.enabled = 0;
    g_profiler.path[0] = '\0';
    pthread_mutex_unlock(&g_profiler.mutex);
}

int profiler_enabled(void)
{
    return g_profiler.enabled;
}

void profiler_set_context(const char *request_id, const char *chat_id,
                          const char *provider)
{
    snprintf(g_ctx.request_id, sizeof(g_ctx.request_id), "%s",
             request_id ? request_id : "");
    snprintf(g_ctx.chat_id, sizeof(g_ctx.chat_id), "%s",
             chat_id ? chat_id : "");
    snprintf(g_ctx.provider, sizeof(g_ctx.provider), "%s",
             provider ? provider : "");
}

void profiler_clear_context(void)
{
    g_ctx.request_id[0] = '\0';
    g_ctx.chat_id[0] = '\0';
    g_ctx.provider[0] = '\0';
}

void profiler_timer_start(profiler_timer_t *timer)
{
    if (!timer) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
}

long profiler_timer_elapsed_ms(const profiler_timer_t *timer)
{
    if (!timer) {
        return 0;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - timer->start.tv_sec) * 1000L +
           (now.tv_nsec - timer->start.tv_nsec) / 1000000L;
}

void profiler_emit_event_ctx(const char *request_id, const char *chat_id,
                             const char *provider, const char *stage,
                             long duration_ms, const char *status,
                             const char *detail)
{
    if (!profiler_enabled() || !stage || stage[0] == '\0') {
        return;
    }

    char req[192], chat[192], prov[192], stg[192], stat[64], det[512];
    json_escape(request_id ? request_id : "", req, sizeof(req));
    json_escape(chat_id ? chat_id : "", chat, sizeof(chat));
    json_escape(provider ? provider : "", prov, sizeof(prov));
    json_escape(stage, stg, sizeof(stg));
    json_escape(status ? status : "ok", stat, sizeof(stat));
    json_escape(detail ? detail : "", det, sizeof(det));

    long long ts_ms = now_epoch_ms();

    pthread_mutex_lock(&g_profiler.mutex);
    if (g_profiler.file) {
        fprintf(g_profiler.file,
                "{\"ts_ms\":%lld,\"request_id\":\"%s\",\"chat_id\":\"%s\","
                "\"provider\":\"%s\",\"stage\":\"%s\",\"duration_ms\":%ld,"
                "\"status\":\"%s\",\"detail\":\"%s\"}\n",
                ts_ms, req, chat, prov, stg, duration_ms, stat, det);
        fflush(g_profiler.file);
    }
    pthread_mutex_unlock(&g_profiler.mutex);
}

void profiler_emit_event(const char *stage, long duration_ms,
                         const char *status, const char *detail)
{
    profiler_emit_event_ctx(g_ctx.request_id, g_ctx.chat_id, g_ctx.provider,
                            stage, duration_ms, status, detail);
}
