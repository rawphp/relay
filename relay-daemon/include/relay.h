#ifndef RELAY_H
#define RELAY_H

#include <stddef.h>
#include <time.h>

/* ── Version ────────────────────────────────────────────────────────── */
/* RELAY_VERSION is set via -D flag in the Makefile from git describe.
 * Fallback if not defined (e.g. compiling outside Make). */
#ifndef RELAY_VERSION
#define RELAY_VERSION "unknown"
#endif

/* ── Buffer Sizes ───────────────────────────────────────────────────── */

#define RELAY_MAX_LINE        4096
#define RELAY_MAX_PATH        1024
#define RELAY_MAX_VALUE       2048
#define RELAY_MAX_MSG         65536   /* max message buffer (64KB) */
#define RELAY_MAX_RESPONSE    262144  /* max Claude response (256KB) */
#define RELAY_MAX_USER_ID     64
#define RELAY_MAX_SESSION_ID  128
#define RELAY_MAX_URL         2048
#define RELAY_MAX_TOKEN       256
#define RELAY_TELEGRAM_CHUNK  4096    /* Telegram message size limit */

/* ── Error Codes ────────────────────────────────────────────────────── */

#define RELAY_OK              0
#define RELAY_ERR            -1
#define RELAY_ERR_NOTFOUND   -2
#define RELAY_ERR_PARSE      -3
#define RELAY_ERR_TIMEOUT    -4
#define RELAY_ERR_AUTH       -5
#define RELAY_ERR_IO         -6
#define RELAY_ERR_NOMEM      -7
#define RELAY_ERR_INVALID    -8
#define RELAY_ERR_NETWORK    -9
#define RELAY_ERR_FULL       -10
#define RELAY_ERR_SIGNAL     -11  /* process killed by external signal (e.g. user interruption) */

/* ── Dependency Injection Structs ───────────────────────────────────── */

/* HTTP client abstraction */
typedef struct {
    int (*get)(const char *url, char *resp, size_t max);
    int (*post)(const char *url, const char *body, char *resp, size_t max);
    int (*get_to_file)(const char *url, const char *local_path);
    int (*post_file)(const char *url, const char *file_path,
                     const char *field_name, const char **form_fields,
                     char *resp, size_t max);
} relay_http_t;

/* Streaming token callback. Return 0 to continue, non-zero to abort. */
typedef int (*relay_stream_token_cb)(const char *text, size_t len, void *userdata);

/* Process spawner abstraction */
typedef struct {
    int (*spawn)(const char *bin, const char **args,
                 const char *input, char *output, size_t max,
                 int timeout_sec);
    /* Streaming variant — may be NULL if not supported.
     * Calls on_token for each text chunk as it arrives.
     * Copies the final result JSON line into result_line. */
    int (*spawn_streaming)(const char *bin, const char **args,
                           const char *input,
                           relay_stream_token_cb on_token, void *userdata,
                           char *result_line, size_t result_max,
                           int timeout_sec);
} relay_proc_t;

/* Clock abstraction */
typedef struct {
    time_t (*now)(void);
    struct tm *(*localtime_r)(const time_t *t, struct tm *result);
} relay_clock_t;

/* Filesystem abstraction */
typedef struct {
    char *(*read_file)(const char *path);
    int (*write_file)(const char *path, const char *content);
    int (*file_exists)(const char *path);
    int (*append_file)(const char *path, const char *content);
    int (*delete_file)(const char *path);
    /* List files in a directory matching a suffix (e.g. ".jsonl").
     * Fills names[] with filenames (not full paths), up to max entries.
     * Returns the number of entries found, or -1 on error.
     * May be NULL — callers must check before use. */
    int (*list_dir)(const char *dir, const char *suffix,
                    char names[][256], int max);
} relay_fs_t;

#endif /* RELAY_H */
