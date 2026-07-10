#ifndef RELAY_MOCKS_H
#define RELAY_MOCKS_H

/* Suppress unused-function warnings — not all test files use all mocks */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include "relay.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Mock Clock ─────────────────────────────────────────────────────── */

static time_t g_mock_time = 1708070400; /* 2024-02-16 12:00:00 UTC */

static time_t mock_clock_now(void)
{
    return g_mock_time;
}

static struct tm *mock_clock_localtime_r(const time_t *t, struct tm *result)
{
    (void)t;
    result->tm_year = 124;
    result->tm_mon  = 1;
    result->tm_mday = 16;
    result->tm_hour = 12;
    result->tm_min  = 0;
    result->tm_sec  = 0;
    result->tm_isdst = 0;
    return result;
}

__attribute__((unused))
static relay_clock_t g_mock_clock = {
    .now = mock_clock_now,
    .localtime_r = mock_clock_localtime_r
};

/* ── Mock Filesystem ────────────────────────────────────────────────── */

#define MOCK_FS_MAX_FILES 16
#define MOCK_FS_MAX_SIZE  65536

typedef struct {
    char path[RELAY_MAX_PATH];
    char content[MOCK_FS_MAX_SIZE];
    int exists;
} mock_file_t;

static mock_file_t g_mock_files[MOCK_FS_MAX_FILES];
static int g_mock_file_count = 0;

static void mock_fs_reset(void)
{
    memset(g_mock_files, 0, sizeof(g_mock_files));
    g_mock_file_count = 0;
}

static void mock_fs_set(const char *path, const char *content)
{
    /* Check if file already exists */
    for (int i = 0; i < g_mock_file_count; i++) {
        if (strcmp(g_mock_files[i].path, path) == 0) {
            snprintf(g_mock_files[i].content, MOCK_FS_MAX_SIZE, "%s", content);
            g_mock_files[i].exists = 1;
            return;
        }
    }
    /* Add new file */
    if (g_mock_file_count < MOCK_FS_MAX_FILES) {
        snprintf(g_mock_files[g_mock_file_count].path, RELAY_MAX_PATH,
                 "%s", path);
        snprintf(g_mock_files[g_mock_file_count].content, MOCK_FS_MAX_SIZE,
                 "%s", content);
        g_mock_files[g_mock_file_count].exists = 1;
        g_mock_file_count++;
    }
}

static mock_file_t *mock_fs_find(const char *path)
{
    for (int i = 0; i < g_mock_file_count; i++) {
        if (strcmp(g_mock_files[i].path, path) == 0 &&
            g_mock_files[i].exists) {
            return &g_mock_files[i];
        }
    }
    return NULL;
}

static char *mock_fs_read_file(const char *path)
{
    mock_file_t *f = mock_fs_find(path);
    if (!f) {
        return NULL;
    }
    char *copy = malloc(strlen(f->content) + 1);
    if (copy) {
        strcpy(copy, f->content);
    }
    return copy;
}

static int mock_fs_write_file(const char *path, const char *content)
{
    mock_fs_set(path, content);
    return RELAY_OK;
}

static int mock_fs_file_exists(const char *path)
{
    return mock_fs_find(path) != NULL;
}

static int mock_fs_append_file(const char *path, const char *content)
{
    mock_file_t *f = mock_fs_find(path);
    if (!f) {
        /* Create new file with content */
        mock_fs_set(path, content);
        return RELAY_OK;
    }
    /* Append */
    size_t existing = strlen(f->content);
    size_t adding = strlen(content);
    if (existing + adding < MOCK_FS_MAX_SIZE) {
        memcpy(f->content + existing, content, adding + 1);
    }
    return RELAY_OK;
}

static int mock_fs_delete_file(const char *path)
{
    for (int i = 0; i < g_mock_file_count; i++) {
        if (strcmp(g_mock_files[i].path, path) == 0) {
            g_mock_files[i].exists = 0;
            return RELAY_OK;
        }
    }
    return RELAY_ERR_NOTFOUND;
}

/* Mock list_dir: scans mock files for paths starting with dir/ and ending with
 * suffix. Returns filenames (basename only) up to max. */
static int mock_fs_list_dir(const char *dir, const char *suffix,
                            char names[][256], int max)
{
    int count = 0;
    size_t dir_len = strlen(dir);
    size_t suf_len = suffix ? strlen(suffix) : 0;

    for (int i = 0; i < g_mock_file_count && count < max; i++) {
        if (!g_mock_files[i].exists) continue;
        const char *p = g_mock_files[i].path;
        /* Check dir prefix (dir + '/') */
        if (strncmp(p, dir, dir_len) != 0) continue;
        if (p[dir_len] != '/') continue;
        const char *fname = p + dir_len + 1;
        /* Skip if contains further slashes (subdirectory) */
        if (strchr(fname, '/')) continue;
        /* Check suffix */
        if (suffix && suf_len > 0) {
            size_t flen = strlen(fname);
            if (flen < suf_len) continue;
            if (strcmp(fname + flen - suf_len, suffix) != 0) continue;
        }
        snprintf(names[count], 256, "%s", fname);
        count++;
    }
    return count;
}

__attribute__((unused))
static relay_fs_t g_mock_fs = {
    .read_file = mock_fs_read_file,
    .write_file = mock_fs_write_file,
    .file_exists = mock_fs_file_exists,
    .append_file = mock_fs_append_file,
    .delete_file = mock_fs_delete_file,
    .list_dir = mock_fs_list_dir
};

/* ── Mock HTTP ──────────────────────────────────────────────────────── */

static char g_mock_http_last_url[RELAY_MAX_URL];
static char g_mock_http_last_body[RELAY_MAX_MSG];
static char g_mock_http_response[RELAY_MAX_MSG];
static char g_mock_http_last_file_path[RELAY_MAX_PATH];
static int g_mock_http_status = 0; /* 0 = success */

static void mock_http_reset(void)
{
    memset(g_mock_http_last_url, 0, sizeof(g_mock_http_last_url));
    memset(g_mock_http_last_body, 0, sizeof(g_mock_http_last_body));
    memset(g_mock_http_response, 0, sizeof(g_mock_http_response));
    memset(g_mock_http_last_file_path, 0, sizeof(g_mock_http_last_file_path));
    g_mock_http_status = 0;
}

static void mock_http_set_response(const char *response)
{
    snprintf(g_mock_http_response, sizeof(g_mock_http_response),
             "%s", response);
}

static int mock_http_get(const char *url, char *resp, size_t max)
{
    snprintf(g_mock_http_last_url, sizeof(g_mock_http_last_url), "%s", url);
    if (g_mock_http_status != 0) {
        return g_mock_http_status;
    }
    snprintf(resp, max, "%s", g_mock_http_response);
    return RELAY_OK;
}

static int mock_http_post(const char *url, const char *body,
                          char *resp, size_t max)
{
    snprintf(g_mock_http_last_url, sizeof(g_mock_http_last_url), "%s", url);
    snprintf(g_mock_http_last_body, sizeof(g_mock_http_last_body), "%s", body);
    if (g_mock_http_status != 0) {
        return g_mock_http_status;
    }
    snprintf(resp, max, "%s", g_mock_http_response);
    return RELAY_OK;
}

static int mock_http_get_to_file(const char *url, const char *local_path)
{
    snprintf(g_mock_http_last_url, sizeof(g_mock_http_last_url), "%s", url);
    snprintf(g_mock_http_last_file_path, sizeof(g_mock_http_last_file_path),
             "%s", local_path);
    if (g_mock_http_status != 0) {
        return g_mock_http_status;
    }
    /* Write mock response data to the file */
    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        return RELAY_ERR_IO;
    }
    fputs(g_mock_http_response, fp);
    fclose(fp);
    return RELAY_OK;
}

__attribute__((unused))
static relay_http_t g_mock_http = {
    .get = mock_http_get,
    .post = mock_http_post,
    .get_to_file = mock_http_get_to_file
};

/* ── Mock Process ───────────────────────────────────────────────────── */

static char g_mock_proc_output[RELAY_MAX_RESPONSE];
static int g_mock_proc_status = 0;
static const char **g_mock_proc_last_args = NULL;
static int g_mock_proc_call_count_val = 0;
static int g_mock_proc_succeed_after_n = -1;
static char g_mock_proc_success_output[RELAY_MAX_RESPONSE];
static char g_mock_proc_last_input[RELAY_MAX_MSG];
static char g_mock_proc_last_workspace_path[RELAY_MAX_PATH];
/* stderr reported by last_stderr() — simulates Claude CLI error output */
static char g_mock_proc_stderr_buf[4096];
/* All args of the most recent spawn call joined with '\n' — unlike
 * g_mock_proc_last_args (a pointer into the caller's stack), this copy
 * stays valid after the call returns. */
static char g_mock_proc_last_args_joined[32768];

/* Streaming state — declared here so mock_proc_reset() can reach them */
static char g_mock_proc_stream_result[RELAY_MAX_RESPONSE];
static char g_mock_proc_stream_tokens[RELAY_MAX_MSG];
static int g_mock_proc_stream_status = 0;
static int g_mock_proc_stream_succeed_after_n = -1;
static char g_mock_proc_stream_success_tokens[RELAY_MAX_MSG];
static char g_mock_proc_stream_success_result[RELAY_MAX_RESPONSE];
/* If set: deliver tokens via callback THEN return g_mock_proc_stream_status */
static int g_mock_proc_stream_tokens_before_fail = 0;

static void mock_proc_reset(void)
{
    memset(g_mock_proc_output, 0, sizeof(g_mock_proc_output));
    memset(g_mock_proc_success_output, 0, sizeof(g_mock_proc_success_output));
    memset(g_mock_proc_last_input, 0, sizeof(g_mock_proc_last_input));
    memset(g_mock_proc_last_workspace_path, 0, sizeof(g_mock_proc_last_workspace_path));
    g_mock_proc_status = 0;
    g_mock_proc_last_args = NULL;
    g_mock_proc_call_count_val = 0;
    g_mock_proc_succeed_after_n = -1;
    memset(g_mock_proc_stream_result, 0, sizeof(g_mock_proc_stream_result));
    memset(g_mock_proc_stream_tokens, 0, sizeof(g_mock_proc_stream_tokens));
    g_mock_proc_stream_status = 0;
    g_mock_proc_stream_succeed_after_n = -1;
    memset(g_mock_proc_stream_success_tokens, 0, sizeof(g_mock_proc_stream_success_tokens));
    memset(g_mock_proc_stream_success_result, 0, sizeof(g_mock_proc_stream_success_result));
    g_mock_proc_stream_tokens_before_fail = 0;
    memset(g_mock_proc_stderr_buf, 0, sizeof(g_mock_proc_stderr_buf));
    memset(g_mock_proc_last_args_joined, 0, sizeof(g_mock_proc_last_args_joined));
}

static void mock_proc_set_stderr(const char *text)
{
    snprintf(g_mock_proc_stderr_buf, sizeof(g_mock_proc_stderr_buf),
             "%s", text ? text : "");
}

static const char *mock_proc_last_stderr(void)
{
    return g_mock_proc_stderr_buf;
}

static void mock_proc_record_args(const char **args)
{
    g_mock_proc_last_args = args;
    g_mock_proc_last_args_joined[0] = '\0';
    size_t used = 0;
    for (int i = 0; args && args[i]; i++) {
        int w = snprintf(g_mock_proc_last_args_joined + used,
                         sizeof(g_mock_proc_last_args_joined) - used,
                         "%s%s", i > 0 ? "\n" : "", args[i]);
        if (w < 0 ||
            (size_t)w >= sizeof(g_mock_proc_last_args_joined) - used) {
            break; /* truncated — keep what fits */
        }
        used += (size_t)w;
    }
}

static void mock_proc_set_output(const char *output)
{
    snprintf(g_mock_proc_output, sizeof(g_mock_proc_output), "%s", output);
}

static void mock_proc_set_output_after_n_calls(int n, const char *output)
{
    g_mock_proc_succeed_after_n = n;
    snprintf(g_mock_proc_success_output, sizeof(g_mock_proc_success_output),
             "%s", output);
}

static int mock_proc_call_count(void)
{
    return g_mock_proc_call_count_val;
}

static int g_mock_proc_last_timeout = 0;

static int mock_proc_spawn(const char *bin, const char **args,
                           const char *input, char *output, size_t max,
                           int timeout_sec)
{
    (void)bin;
    if (input) {
        snprintf(g_mock_proc_last_input, sizeof(g_mock_proc_last_input), "%s", input);
    } else {
        g_mock_proc_last_input[0] = '\0';
    }
    mock_proc_record_args(args);
    g_mock_proc_last_timeout = timeout_sec;
    g_mock_proc_call_count_val++;

    /* Check if we should succeed after N calls */
    if (g_mock_proc_succeed_after_n >= 0 &&
        g_mock_proc_call_count_val > g_mock_proc_succeed_after_n) {
        snprintf(output, max, "%s", g_mock_proc_success_output);
        g_mock_proc_stderr_buf[0] = '\0'; /* successful spawn: no stderr */
        return RELAY_OK;
    }

    if (g_mock_proc_status != 0) {
        return g_mock_proc_status;
    }
    snprintf(output, max, "%s", g_mock_proc_output);
    return RELAY_OK;
}

/* ── Mock Process: Streaming variant ────────────────────────────────── */

static void mock_proc_set_stream_output(const char *tokens_text,
                                        const char *result_json_line)
{
    snprintf(g_mock_proc_stream_tokens, sizeof(g_mock_proc_stream_tokens),
             "%s", tokens_text ? tokens_text : "");
    snprintf(g_mock_proc_stream_result, sizeof(g_mock_proc_stream_result),
             "%s", result_json_line ? result_json_line : "");
    g_mock_proc_stream_status = 0;
    g_mock_proc_stream_succeed_after_n = -1;
}

/* Fail the first N streaming calls, then succeed with the given output. */
static void mock_proc_set_stream_output_after_n_calls(int n,
                                                      const char *tokens_text,
                                                      const char *result_json_line)
{
    g_mock_proc_stream_succeed_after_n = n;
    snprintf(g_mock_proc_stream_success_tokens,
             sizeof(g_mock_proc_stream_success_tokens),
             "%s", tokens_text ? tokens_text : "");
    snprintf(g_mock_proc_stream_success_result,
             sizeof(g_mock_proc_stream_success_result),
             "%s", result_json_line ? result_json_line : "");
}

static int mock_proc_spawn_streaming(const char *bin, const char **args,
                                     const char *input,
                                     relay_stream_token_cb on_token, void *userdata,
                                     char *result_line, size_t result_max,
                                     int timeout_sec)
{
    (void)bin;
    (void)input;
    (void)timeout_sec;
    mock_proc_record_args(args);
    g_mock_proc_call_count_val++;

    /* Check if we should succeed after N calls */
    if (g_mock_proc_stream_succeed_after_n >= 0 &&
        g_mock_proc_call_count_val > g_mock_proc_stream_succeed_after_n) {
        g_mock_proc_stderr_buf[0] = '\0'; /* successful spawn: no stderr */
        if (on_token && g_mock_proc_stream_success_tokens[0] != '\0') {
            on_token(g_mock_proc_stream_success_tokens,
                     strlen(g_mock_proc_stream_success_tokens), userdata);
        }
        if (g_mock_proc_stream_success_result[0] != '\0') {
            snprintf(result_line, result_max, "%s",
                     g_mock_proc_stream_success_result);
        }
        return RELAY_OK;
    }

    /* tokens-then-fail: deliver tokens, then return error */
    if (g_mock_proc_stream_tokens_before_fail && g_mock_proc_stream_status != 0) {
        if (on_token && g_mock_proc_stream_tokens[0] != '\0') {
            on_token(g_mock_proc_stream_tokens,
                     strlen(g_mock_proc_stream_tokens), userdata);
        }
        return g_mock_proc_stream_status;
    }

    if (g_mock_proc_stream_status != 0) {
        return g_mock_proc_stream_status;
    }

    /* Deliver token text via callback */
    if (on_token && g_mock_proc_stream_tokens[0] != '\0') {
        on_token(g_mock_proc_stream_tokens,
                 strlen(g_mock_proc_stream_tokens), userdata);
    }

    /* Copy result line */
    if (g_mock_proc_stream_result[0] != '\0') {
        snprintf(result_line, result_max, "%s", g_mock_proc_stream_result);
    }

    return RELAY_OK;
}

__attribute__((unused))
static relay_proc_t g_mock_proc = {
    .spawn = mock_proc_spawn,
    .spawn_streaming = mock_proc_spawn_streaming,
    .last_stderr = mock_proc_last_stderr
};

#pragma GCC diagnostic pop

#endif /* RELAY_MOCKS_H */
