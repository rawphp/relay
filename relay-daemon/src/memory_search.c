#include "memory_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ─────────────────────────────────────────────────── */

struct memory_search {
    relay_proc_t *proc;
    relay_http_t *http;             /* optional — NULL if not provided */
    char search_url[RELAY_MAX_URL]; /* HTTP sidecar URL; empty = disabled */
    int top_k;
    int timeout_sec;
    char threshold_str[16]; /* "0.50" — pre-formatted for JSON body */
    char top_k_str[8];     /* "3" — pre-formatted for JSON body */
};

/* ── Internal helpers ───────────────────────────────────────────────── */

/* Escape a string for embedding in a JSON value (handles " and \).
 * dst receives the escaped bytes; max includes the NUL terminator. */
static void json_escape_str(char *dst, size_t max, const char *src)
{
    size_t w = 0;
    for (const char *p = src; *p && w + 2 < max; p++) {
        if (*p == '"' || *p == '\\') {
            if (w + 3 >= max) break;
            dst[w++] = '\\';
        } else if (*p == '\n') {
            if (w + 3 >= max) break;
            dst[w++] = '\\';
            dst[w++] = 'n';
            continue;
        } else if (*p == '\r') {
            if (w + 3 >= max) break;
            dst[w++] = '\\';
            dst[w++] = 'r';
            continue;
        }
        dst[w++] = *p;
    }
    dst[w] = '\0';
}

/* Count [N] result markers in a memory search output buffer. */
static int count_result_markers(const char *output)
{
    int count = 0;
    const char *p = output;
    while ((p = strstr(p, "[")) != NULL) {
        if (p[1] >= '1' && p[1] <= '9' && p[2] == ']') {
            count++;
        }
        p++;
    }
    return count;
}

/* ── Public API ─────────────────────────────────────────────────────── */

memory_search_t *memory_search_create(relay_proc_t *proc, relay_http_t *http,
                                       const config_t *cfg)
{
    if (!proc || !cfg) {
        return NULL;
    }

    if (!config_get_int(cfg, "memory_search_enabled", 1)) {
        return NULL;
    }

    memory_search_t *ms = calloc(1, sizeof(memory_search_t));
    if (!ms) {
        return NULL;
    }

    ms->proc = proc;
    ms->http = http; /* may be NULL */

    /* HTTP search URL (sidecar endpoint) — empty string = disabled. */
    const char *url = config_get(cfg, "memory_search_url",
                                 "http://localhost:8765");
    snprintf(ms->search_url, sizeof(ms->search_url), "%s", url);

    ms->top_k = config_get_int(cfg, "memory_search_top_k", 3);
    ms->timeout_sec = config_get_int(cfg, "memory_search_timeout", 5);

    double threshold = 0.5;
    const char *thresh_str = config_get(cfg, "memory_search_threshold", "0.5");
    if (thresh_str) {
        threshold = atof(thresh_str);
    }

    snprintf(ms->threshold_str, sizeof(ms->threshold_str), "%.2f", threshold);
    snprintf(ms->top_k_str, sizeof(ms->top_k_str), "%d", ms->top_k);

    return ms;
}

int memory_search_query(memory_search_t *ms, const char *query,
                        char *context, size_t context_size)
{
    if (!ms || !query || !context || context_size == 0) {
        if (context && context_size > 0) {
            context[0] = '\0';
        }
        return 0;
    }

    /* Skip search for very short messages */
    if (strlen(query) < 3) {
        context[0] = '\0';
        return 0;
    }

    char output[RELAY_MAX_MEMORY_CONTEXT];

    /* HTTP path — sidecar must be reachable at memory_search_url.
     * If no http client or URL is empty, fail-open (return 0). */
    if (!ms->http || ms->search_url[0] == '\0') {
        context[0] = '\0';
        return 0;
    }

    char escaped_query[RELAY_MAX_MSG];
    json_escape_str(escaped_query, sizeof(escaped_query), query);

    char body[RELAY_MAX_MSG];
    snprintf(body, sizeof(body),
             "{\"query\":\"%s\",\"top_k\":%s,\"min_score\":%s}",
             escaped_query, ms->top_k_str, ms->threshold_str);

    int rc = ms->http->post(ms->search_url, body,
                            output, sizeof(output));
    if (rc != RELAY_OK || output[0] == '\0' ||
        strstr(output, "No results found") != NULL) {
        context[0] = '\0';
        return 0;
    }

    snprintf(context, context_size, "%s", output);
    int count = count_result_markers(output);
    return count > 0 ? count : 1;
}

void memory_search_update_config(memory_search_t *ms, const config_t *cfg)
{
    if (!ms || !cfg) {
        return;
    }

    ms->top_k = config_get_int(cfg, "memory_search_top_k", ms->top_k);
    ms->timeout_sec = config_get_int(cfg, "memory_search_timeout",
                                      ms->timeout_sec);

    const char *thresh_str = config_get(cfg, "memory_search_threshold", NULL);
    if (thresh_str) {
        snprintf(ms->threshold_str, sizeof(ms->threshold_str),
                 "%.2f", atof(thresh_str));
    }

    const char *url = config_get(cfg, "memory_search_url", NULL);
    if (url) {
        snprintf(ms->search_url, sizeof(ms->search_url), "%s", url);
    }

    snprintf(ms->top_k_str, sizeof(ms->top_k_str), "%d", ms->top_k);
}

void memory_search_free(memory_search_t *ms)
{
    free(ms);
}
