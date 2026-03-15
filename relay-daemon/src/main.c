#include "relay.h"
#include "config.h"
#include "config_validator.h"
#include "log.h"
#include "event_loop.h"
#include "telegram.h"
#include "llm_provider.h"
#include "session.h"
#include "health.h"
#include "transcript.h"
#include "vision.h"
#include "interruption.h"
#include "memory_search.h"
#include "memory_sidecar.h"
#include "memory_curator.h"
#include "agent_bus.h"
#include "profiler.h"
#include "proc_log_partial.h"
#include "stream_timeout.h"
#include "path_util.h"
#include "pid_file.h"

#include <cJSON/cJSON.h>
#include <curl/curl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <termios.h>
#include <util.h>      /* openpty() — macOS */

/* ── Globals (minimal — only for signal handling) ───────────────────── */

static volatile sig_atomic_t g_stop = 0;
static event_loop_t *g_loop = NULL;
static relay_log_t   *g_log  = NULL; /* set once in main(); used by proc_spawn_streaming */

static void signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT) {
        g_stop = 1;
        if (g_loop) {
            event_loop_stop(g_loop);
        }
    } else if (sig == SIGHUP) {
        if (g_loop) {
            event_loop_request_reload(g_loop);
        }
    }
}

/* ── libcurl HTTP implementation ────────────────────────────────────── */

/* Thread-local CURL handle for connection reuse (saves TCP+TLS handshake) */
static pthread_key_t tls_curl_key;
static pthread_once_t tls_curl_key_once = PTHREAD_ONCE_INIT;

static void tls_curl_destructor(void *arg)
{
    if (arg) {
        curl_easy_cleanup((CURL *)arg);
    }
}

static void tls_curl_key_init(void)
{
    pthread_key_create(&tls_curl_key, tls_curl_destructor);
}

static CURL *get_thread_curl(void)
{
    pthread_once(&tls_curl_key_once, tls_curl_key_init);
    CURL *h = pthread_getspecific(tls_curl_key);
    if (!h) {
        h = curl_easy_init();
        if (h) {
            pthread_setspecific(tls_curl_key, h);
        }
    }
    return h;
}

struct curl_buffer {
    char *data;
    size_t size;
    size_t max;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb,
                            void *userdata)
{
    struct curl_buffer *buf = userdata;
    size_t total = size * nmemb;
    if (buf->size + total >= buf->max) {
        total = buf->max - buf->size - 1;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return size * nmemb;
}

/* Progress callback to check for shutdown signal */
static int curl_progress_cb(void *clientp, curl_off_t dltotal,
                            curl_off_t dlnow, curl_off_t ultotal,
                            curl_off_t ulnow)
{
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    (void)clientp;

    /* Check shutdown flag - abort transfer if set */
    if (g_stop) {
        return 1;  /* Non-zero aborts transfer */
    }
    return 0;  /* Continue transfer */
}

static int http_get(const char *url, char *resp, size_t max)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return RELAY_ERR;
    }

    struct curl_buffer buf = { .data = resp, .size = 0, .max = max };
    resp[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(resp, max, "curl: %s", curl_easy_strerror(res));
        return RELAY_ERR;
    }
    if (http_code >= 400) {
        char body[RELAY_MAX_MSG];
        snprintf(body, sizeof(body), "%s", resp);
        snprintf(resp, max, "http %ld: %.200s", http_code, body);
        return RELAY_ERR;
    }
    return RELAY_OK;
}

static int http_post(const char *url, const char *body,
                     char *resp, size_t max)
{
    CURL *curl = get_thread_curl();
    if (!curl) {
        return RELAY_ERR;
    }

    /* Reset all options from previous use of this handle */
    curl_easy_reset(curl);

    struct curl_buffer buf = { .data = resp, .size = 0, .max = max };
    resp[0] = '\0';

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    /* NOTE: do NOT curl_easy_cleanup() — handle is reused via thread-local */

    if (res != CURLE_OK) {
        snprintf(resp, max, "curl: %s", curl_easy_strerror(res));
        return RELAY_ERR;
    }
    if (http_code >= 400) {
        char errbody[RELAY_MAX_MSG];
        snprintf(errbody, sizeof(errbody), "%s", resp);
        snprintf(resp, max, "http %ld: %.200s", http_code, errbody);
        return RELAY_ERR;
    }
    return RELAY_OK;
}

static int http_get_to_file(const char *url, const char *local_path)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return RELAY_ERR;
    }

    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return RELAY_ERR_IO;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        unlink(local_path);
        return RELAY_ERR;
    }

    return RELAY_OK;
}

static int http_post_file(const char *url, const char *file_path,
                          const char *field_name, const char **form_fields,
                          char *resp, size_t max)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return RELAY_ERR;
    }

    struct curl_buffer buf = { .data = resp, .size = 0, .max = max };
    resp[0] = '\0';

    /* Build multipart form */
    curl_mime *form = curl_mime_init(curl);
    curl_mimepart *field;

    /* Add file */
    field = curl_mime_addpart(form);
    curl_mime_name(field, field_name);
    CURLcode rc = curl_mime_filedata(field, file_path);
    if (rc != CURLE_OK) {
        fprintf(stderr, "curl_mime_filedata failed: %s\n", curl_easy_strerror(rc));
        curl_mime_free(form);
        curl_easy_cleanup(curl);
        return RELAY_ERR;
    }

    /* Add additional form fields (key=value pairs, NULL-terminated) */
    if (form_fields) {
        for (int i = 0; form_fields[i] != NULL; i += 2) {
            const char *key = form_fields[i];
            const char *value = form_fields[i + 1];
            if (value == NULL) {
                break;
            }
            field = curl_mime_addpart(form);
            curl_mime_name(field, key);
            curl_mime_data(field, value, CURL_ZERO_TERMINATED);
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform failed: %s\n", curl_easy_strerror(res));
    }

    curl_mime_free(form);
    curl_easy_cleanup(curl);

    return res == CURLE_OK ? RELAY_OK : RELAY_ERR;
}

static relay_http_t real_http = {
    .get = http_get,
    .post = http_post,
    .get_to_file = http_get_to_file,
    .post_file = http_post_file
};

/* ── Process spawner (fork/exec) ────────────────────────────────────── */

/* External session registry for interruption support */
extern active_sessions_t *g_active_sessions;
extern pthread_mutex_t g_sessions_mutex;

/* Thread-local storage for current chat ID */
static __thread char tls_current_chat_id[RELAY_MAX_USER_ID] = {0};

void proc_set_current_chat_id(const char *chat_id)
{
    if (chat_id) {
        snprintf(tls_current_chat_id, RELAY_MAX_USER_ID, "%s", chat_id);
    } else {
        tls_current_chat_id[0] = '\0';
    }
}

/* Thread-local storage for current workspace path */
static __thread char tls_current_workspace_path[RELAY_MAX_PATH] = {0};

void proc_set_current_workspace_path(const char *path)
{
    if (path) {
        snprintf(tls_current_workspace_path, RELAY_MAX_PATH, "%s", path);
    } else {
        tls_current_workspace_path[0] = '\0';
    }
}

static int proc_spawn(const char *bin, const char **args,
                      const char *input, char *output, size_t max,
                      int timeout_sec)
{
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return RELAY_ERR;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return RELAY_ERR;
    }

    if (pid == 0) {
        /* Child */
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        /* Prevent "nested session" error when relay is started from Claude Code */
        unsetenv("CLAUDECODE");

        if (tls_current_workspace_path[0] != '\0') {
            if (chdir(tls_current_workspace_path) != 0) {
                log_write(g_log, LOG_WARN,
                          "chdir to workspace '%s' failed: %s",
                          tls_current_workspace_path, strerror(errno));
            }
        }

        execvp(bin, (char *const *)args);
        _exit(127);
    }

    /* Parent - register this PID for interruption support */
    if (g_active_sessions && tls_current_chat_id[0]) {
        pthread_mutex_lock(&g_sessions_mutex);
        active_sessions_register(g_active_sessions, tls_current_chat_id, pid);
        pthread_mutex_unlock(&g_sessions_mutex);
    }

    /* Parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    /* Write input */
    if (input && input[0] != '\0') {
        size_t len = strlen(input);
        ssize_t written = write(stdin_pipe[1], input, len);
        (void)written;
    }
    close(stdin_pipe[1]);

    /* Read output with select() timeout — safe for daemons */
    output[0] = '\0';
    size_t total = 0;
    time_t deadline = time(NULL) + timeout_sec;
    int timed_out = 0;

    while (total < max - 1) {
        int remaining = (int)(deadline - time(NULL));
        if (remaining <= 0) {
            timed_out = 1;
            break;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(stdout_pipe[0], &fds);
        struct timeval tv = { .tv_sec = remaining, .tv_usec = 0 };

        int ready = select(stdout_pipe[0] + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) {
            timed_out = (ready == 0);
            break;
        }

        ssize_t n = read(stdout_pipe[0], output + total, max - total - 1);
        if (n <= 0) {
            break; /* EOF or error */
        }
        total += (size_t)n;
    }
    output[total] = '\0';
    close(stdout_pipe[0]);

    if (timed_out) {
        kill(pid, SIGTERM);
        usleep(100000); /* 100ms grace */
        kill(pid, SIGKILL);
    }

    int status;
    waitpid(pid, &status, 0);

    if (timed_out) {
        return RELAY_ERR_TIMEOUT;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        int code = WEXITSTATUS(status);
        if (g_log) {
            log_write(g_log, LOG_WARN,
                      "[claude] exit code=%d output_empty=%d",
                      code, output[0] == '\0');
        }
        return RELAY_ERR;
    }

    return RELAY_OK;
}

/* ── Streaming process spawner ──────────────────────────────────────── */
/* Like proc_spawn() but reads JSONL line-by-line from stdout,
 * calling on_token for text deltas and capturing the result line. */

static void process_stream_line(const char *line,
                                relay_stream_token_cb on_token, void *userdata,
                                char *result_line, size_t result_max,
                                int *got_result)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "stream_event") == 0) {
        /* Extract text from: event.delta.type=="text_delta" → event.delta.text */
        cJSON *event = cJSON_GetObjectItem(root, "event");
        if (event) {
            cJSON *etype = cJSON_GetObjectItem(event, "type");
            if (cJSON_IsString(etype) &&
                strcmp(etype->valuestring, "content_block_delta") == 0) {
                cJSON *delta = cJSON_GetObjectItem(event, "delta");
                if (delta) {
                    cJSON *dtype = cJSON_GetObjectItem(delta, "type");
                    cJSON *text = cJSON_GetObjectItem(delta, "text");
                    if (cJSON_IsString(dtype) &&
                        strcmp(dtype->valuestring, "text_delta") == 0 &&
                        cJSON_IsString(text)) {
                        on_token(text->valuestring, strlen(text->valuestring),
                                 userdata);
                    }
                }
            }
        }
    } else if (strcmp(type->valuestring, "result") == 0) {
        /* Capture the raw result line for later parsing */
        snprintf(result_line, result_max, "%s", line);
        *got_result = 1;
    }

    cJSON_Delete(root);
}

static int proc_spawn_streaming(const char *bin, const char **args,
                                const char *input,
                                relay_stream_token_cb on_token, void *userdata,
                                char *result_line, size_t result_max,
                                int timeout_sec)
{
    /* Use a PTY for the child's stdout so Node.js (Claude CLI) sees a TTY
     * and flushes each token immediately rather than buffering until exit. */
    int stdin_pipe[2];
    if (pipe(stdin_pipe) < 0) {
        return RELAY_ERR;
    }

    /* Separate pipe for stderr — lets us capture Claude's error output
     * without mixing it into the JSONL stdout stream. */
    int stderr_pipe[2];
    if (pipe(stderr_pipe) < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return RELAY_ERR;
    }

    int master_fd = -1, slave_fd = -1;
    struct termios tios;
    cfmakeraw(&tios);
    if (openpty(&master_fd, &slave_fd, NULL, &tios, NULL) < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return RELAY_ERR;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        close(master_fd);
        close(slave_fd);
        return RELAY_ERR;
    }

    if (pid == 0) {
        /* Child */
        close(stdin_pipe[1]);
        close(master_fd);
        close(stderr_pipe[0]); /* child only writes stderr */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO); /* stderr → pipe, not PTY */
        close(stdin_pipe[0]);
        close(slave_fd);
        close(stderr_pipe[1]);

        /* Prevent "nested session" error when relay is started from Claude Code */
        unsetenv("CLAUDECODE");

        if (tls_current_workspace_path[0] != '\0') {
            if (chdir(tls_current_workspace_path) != 0) {
                log_write(g_log, LOG_WARN,
                          "chdir to workspace '%s' failed: %s",
                          tls_current_workspace_path, strerror(errno));
            }
        }

        execvp(bin, (char *const *)args);
        _exit(127);
    }

    /* Parent — register PID for interruption support */
    if (g_active_sessions && tls_current_chat_id[0]) {
        pthread_mutex_lock(&g_sessions_mutex);
        active_sessions_register(g_active_sessions, tls_current_chat_id, pid);
        pthread_mutex_unlock(&g_sessions_mutex);
    }

    close(stdin_pipe[0]);
    close(slave_fd);        /* Parent does not need the slave end */
    close(stderr_pipe[1]);  /* Parent only reads stderr */

    /* Write input to child's stdin */
    if (input && input[0] != '\0') {
        size_t len = strlen(input);
        ssize_t written = write(stdin_pipe[1], input, len);
        (void)written;
    }
    close(stdin_pipe[1]);

    /* Read JSONL line-by-line from PTY master with select() timeout.
     * cfmakeraw() disables output processing so lines arrive as plain \n.
     * Two timeout windows run in parallel:
     *   - wall-clock deadline: session must finish within timeout_sec total
     *   - idle sub-timeout:    no data received for RELAY_STREAM_IDLE_TIMEOUT_SEC
     *                          consecutive seconds → stalled session, abort */
    result_line[0] = '\0';
    int got_result = 0;
    int timed_out = 0;
    time_t now_start = time(NULL);
    time_t deadline = now_start + timeout_sec;
    time_t last_data_time = now_start; /* updated on every successful read */

    /* Heap-allocated to handle large result JSON lines (up to RELAY_MAX_RESPONSE).
     * The result event from Claude CLI with --verbose can exceed 16KB when
     * context is large, causing read(fd, buf, 0) → silent EOF → !got_result. */
    char *linebuf = malloc(RELAY_MAX_RESPONSE);
    if (!linebuf) {
        close(master_fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return RELAY_ERR;
    }
    size_t linebuf_pos = 0;

    while (!got_result) {
        time_t now = time(NULL);
        int sel_sec = compute_select_timeout(deadline, last_data_time,
                                             RELAY_STREAM_IDLE_TIMEOUT_SEC, now);
        if (sel_sec <= 0) {
            timed_out = 1;
            break;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(master_fd, &fds);
        struct timeval tv = { .tv_sec = sel_sec, .tv_usec = 0 };

        int ready = select(master_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) {
            timed_out = 1; /* wall or idle timeout expired */
            break;
        }

        /* Safety: if buffer is completely full with no newline, the line is
         * unparseable (e.g. verbose noise). Drain it and keep reading. */
        if (linebuf_pos >= RELAY_MAX_RESPONSE - 1) {
            linebuf_pos = 0;
        }

        ssize_t n = read(master_fd, linebuf + linebuf_pos,
                         RELAY_MAX_RESPONSE - linebuf_pos - 1);
        if (n <= 0) {
            break; /* EOF or EIO (child exited, slave closed) */
        }
        last_data_time = time(NULL); /* reset idle clock on data received */
        linebuf_pos += (size_t)n;
        linebuf[linebuf_pos] = '\0';

        /* Process all complete newline-terminated lines */
        char *start = linebuf;
        char *nl;
        while ((nl = memchr(start, '\n',
                            linebuf_pos - (size_t)(start - linebuf))) != NULL) {
            *nl = '\0';
            /* Strip trailing \r in case PTY output processing adds CR */
            if (nl > start && *(nl - 1) == '\r') {
                *(nl - 1) = '\0';
            }
            if (start[0] != '\0') {
                process_stream_line(start, on_token, userdata,
                                    result_line, result_max, &got_result);
            }
            start = nl + 1;
            if (got_result) {
                break;
            }
        }

        /* Shift remaining partial line to front of buffer */
        size_t remaining_bytes = linebuf_pos - (size_t)(start - linebuf);
        if (remaining_bytes > 0 && start != linebuf) {
            memmove(linebuf, start, remaining_bytes);
        }
        linebuf_pos = remaining_bytes;
    }

    /* Save tail of linebuf before freeing — used below for crash diagnostics. */
    char partial_tail[PROC_LOG_PARTIAL_MAX + 1];
    size_t partial_len = linebuf_pos < PROC_LOG_PARTIAL_MAX
                         ? linebuf_pos
                         : PROC_LOG_PARTIAL_MAX;
    if (partial_len > 0) {
        memcpy(partial_tail, linebuf + linebuf_pos - partial_len, partial_len);
    }
    partial_tail[partial_len] = '\0';

    free(linebuf);
    close(master_fd);

    if (timed_out) {
        kill(pid, SIGTERM);
        usleep(100000); /* 100ms grace */
        kill(pid, SIGKILL);
    }

    int status;
    waitpid(pid, &status, 0);

    /* Log exit status for diagnostics (Fix: was silently discarded) */
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if ((code != 0 || !got_result) && g_log) {
            log_write(g_log, LOG_WARN,
                      "[claude] exit code=%d got_result=%d timed_out=%d",
                      code, got_result, timed_out);
        }
    } else if (WIFSIGNALED(status)) {
        if (g_log) {
            log_write(g_log, LOG_WARN,
                      "[claude] killed by signal %d got_result=%d",
                      WTERMSIG(status), got_result);
        }
    }

    /* Drain and log Claude's stderr (Fix: was mixed into PTY, now separated) */
    {
        char stderr_buf[4096];
        /* Non-blocking: child has exited so pipe should be at EOF,
         * but set O_NONBLOCK as a safety net. */
        int flags = fcntl(stderr_pipe[0], F_GETFL, 0);
        if (flags >= 0) {
            fcntl(stderr_pipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        ssize_t n = read(stderr_pipe[0], stderr_buf, sizeof(stderr_buf) - 1);
        if (n > 0) {
            stderr_buf[n] = '\0';
            /* Strip trailing whitespace */
            while (n > 0 && (stderr_buf[n - 1] == '\n' ||
                              stderr_buf[n - 1] == '\r' ||
                              stderr_buf[n - 1] == ' ')) {
                stderr_buf[--n] = '\0';
            }
            if (stderr_buf[0] != '\0' && g_log) {
                log_write(g_log, LOG_WARN,
                          "[claude] stderr: %.2000s", stderr_buf);
            }
        }
        close(stderr_pipe[0]);
    }

    if (timed_out) {
        return RELAY_ERR_TIMEOUT;
    }

    if (!got_result) {
        /* Log last bytes received before the crash — aids post-mortem. */
        if (partial_len > 0) {
            proc_log_partial(g_log, partial_tail, partial_len);
        }
        /* Distinguish external signal kills (e.g. user interruption) from
         * plain process failures so callers can skip retrying stale messages. */
        if (WIFSIGNALED(status)) {
            return RELAY_ERR_SIGNAL;
        }
        return RELAY_ERR;
    }

    return RELAY_OK;
}

static relay_proc_t real_proc = {
    .spawn = proc_spawn,
    .spawn_streaming = proc_spawn_streaming
};

/* ── Real clock ─────────────────────────────────────────────────────── */

static time_t real_now(void)
{
    return time(NULL);
}

static struct tm *real_localtime_r(const time_t *t, struct tm *result)
{
    return localtime_r(t, result);
}

static relay_clock_t real_clock = {
    .now = real_now,
    .localtime_r = real_localtime_r
};

/* ── Real filesystem ────────────────────────────────────────────────── */

static char *real_read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static int real_write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return RELAY_ERR_IO;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return RELAY_ERR_IO;
    }
    fputs(content, f);
    fclose(f);
    return RELAY_OK;
}

static int real_file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static int real_append_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        return RELAY_ERR_IO;
    }
    FILE *f = fdopen(fd, "a");
    if (!f) {
        close(fd);
        return RELAY_ERR_IO;
    }
    fputs(content, f);
    fclose(f);
    return RELAY_OK;
}

static int real_delete_file(const char *path)
{
    return (unlink(path) == 0) ? RELAY_OK : RELAY_ERR_IO;
}

static int real_list_dir(const char *dir, const char *suffix,
                         char names[][256], int max)
{
    DIR *d = opendir(dir);
    if (!d) return 0;

    int count = 0;
    size_t suf_len = suffix ? strlen(suffix) : 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max) {
        if (ent->d_name[0] == '.') continue;
        if (suffix && suf_len > 0) {
            size_t nlen = strlen(ent->d_name);
            if (nlen < suf_len) continue;
            if (strcmp(ent->d_name + nlen - suf_len, suffix) != 0) continue;
        }
        snprintf(names[count], 256, "%s", ent->d_name);
        count++;
    }
    closedir(d);
    return count;
}

static relay_fs_t real_fs = {
    .read_file = real_read_file,
    .write_file = real_write_file,
    .file_exists = real_file_exists,
    .append_file = real_append_file,
    .delete_file = real_delete_file,
    .list_dir = real_list_dir
};

/* ── PID file (helpers used by stop/status/restart subcommands) ─────── */

static void remove_pid_file(const char *path)
{
    unlink(path);
}

static int read_pid_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) {
        pid = -1;
    }
    fclose(f);
    return pid;
}

static void get_pid_path_from_config(const char *config_path,
                                     char *pid_path, size_t max)
{
    config_t *cfg = config_load(config_path);
    const char *explicit = cfg ? config_get(cfg, "pid_file", NULL) : NULL;
    if (explicit) {
        snprintf(pid_path, max, "%s", explicit);
    } else if (pid_default_path(config_path, pid_path, max) != RELAY_OK) {
        snprintf(pid_path, max, "/tmp/relay.pid"); /* last resort */
    }
    config_free(cfg);
}

static int stop_daemon_by_pidfile(const char *pid_path, int wait_ms)
{
    int pid = read_pid_file(pid_path);
    if (pid <= 0) {
        return 0; /* Not running */
    }

    if (kill(pid, 0) != 0) {
        remove_pid_file(pid_path); /* stale */
        return 0;
    }

    kill(pid, SIGTERM);

    int polls = wait_ms / 100;
    for (int i = 0; i < polls; i++) {
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            remove_pid_file(pid_path);
            return 1; /* Stopped */
        }
        usleep(100000);
    }

    kill(pid, SIGKILL);
    usleep(200000);

    if (kill(pid, 0) != 0 && errno == ESRCH) {
        remove_pid_file(pid_path);
        return 1;
    }

    return -1; /* Could not stop */
}

/* ── Daemonize ──────────────────────────────────────────────────────── */

static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        return RELAY_ERR;
    }
    if (pid > 0) {
        _exit(0); /* Parent exits */
    }

    setsid();

    /* Redirect stdio to /dev/null */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }

    return RELAY_OK;
}

/* ── Usage ──────────────────────────────────────────────────────────── */

static void usage(void)
{
    fprintf(stderr, "relay " RELAY_VERSION " — relay daemon\n\n"
                    "Usage: relay <command> [options]\n\n"
                    "Commands:\n"
                    "  start [-f] [-c path]           Start daemon (background by default)\n"
                    "  stop [-c path]                 Stop running daemon\n"
                    "  restart [-f] [-c path]         Restart daemon\n"
                    "  refresh [-c path]              Reload config without restart\n"
                    "  status [-c path]               Show daemon status\n"
                    "  log [N] [--watch] [-c path]    Show recent log entries (default: 20)\n"
                    "  send-file <path> [caption]     Send file via Telegram\n"
                    "  config [-c path]               Validate config file\n"
                    "  version                        Show version\n"
                    "  help                           Show this help\n");
}

/* ── Main ───────────────────────────────────────────────────────────── */
/*
 * CLI surface:
 *   relay start [-f] [-c config]     Start daemon (background by default)
 *   relay stop [-c config]           Send SIGTERM to running daemon
 *   relay restart [-f] [-c config]   Stop then start daemon
 *   relay refresh [-c config]        Send SIGHUP to trigger config reload
 *   relay status [-c config]         Print running/stopped + PID
 *   relay log [N] [--watch]          Tail last N lines of daemon log
 *   relay send-file <path> [caption] Send a document via Telegram
 *   relay version                    Show version
 *   relay help                       Show this help
 */

int main(int argc, char *argv[])
{
    /* Restrict default file permissions: owner-only (files 0600, dirs 0700) */
    umask(0077);

    /* No args → show usage */
    if (argc < 2) {
        usage();
        return 0;
    }

    const char *cmd = argv[1];

    /* help / version shortcuts (also accept -h/--help/--version) */
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
        strcmp(cmd, "--help") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("relay %s\n", RELAY_VERSION);
        return 0;
    }

    /* Parse -c and -f from subcommand args */
    int foreground = 0;
    int explicit_config = 0;
    char config_path_buf[RELAY_MAX_PATH];
    snprintf(config_path_buf, sizeof(config_path_buf), "%s", "config/relay.conf");
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            snprintf(config_path_buf, sizeof(config_path_buf), "%s", argv[++i]);
            explicit_config = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            foreground = 1;
        }
    }

    /* When no -c was given, resolve config path relative to binary location.
     * This allows `relay restart` to work from any directory, not just RELAY_HOME. */
    if (!explicit_config) {
        resolve_default_config_path(argv[0], config_path_buf, sizeof(config_path_buf));
    }
    const char *config_path = config_path_buf;

    /* ── stop ─────────────────────────────────────────────────────── */
    if (strcmp(cmd, "stop") == 0) {
        char pid_path[RELAY_MAX_PATH];
        get_pid_path_from_config(config_path, pid_path, sizeof(pid_path));
        int pid = read_pid_file(pid_path);
        int rc = stop_daemon_by_pidfile(pid_path, 8000);
        if (rc == 1) {
            fprintf(stderr, "Stopped relay (pid %d)\n", pid);
        } else if (rc == 0) {
            fprintf(stderr, "relay is not running\n");
        } else {
            fprintf(stderr, "Failed to stop relay (pid %d)\n", pid);
            return 1;
        }
        return 0;

    /* ── status ───────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "status") == 0) {
        char pid_path[RELAY_MAX_PATH];
        get_pid_path_from_config(config_path, pid_path, sizeof(pid_path));
        int pid = read_pid_file(pid_path);
        if (pid > 0 && kill(pid, 0) == 0) {
            fprintf(stderr, "relay is running (pid %d)\n", pid);
        } else {
            fprintf(stderr, "relay is not running\n");
        }
        return 0;

    /* ── refresh (reload config) ──────────────────────────────────── */
    } else if (strcmp(cmd, "refresh") == 0) {
        char pid_path[RELAY_MAX_PATH];
        get_pid_path_from_config(config_path, pid_path, sizeof(pid_path));
        int pid = read_pid_file(pid_path);
        if (pid > 0 && kill(pid, 0) == 0) {
            kill(pid, SIGHUP);
            fprintf(stderr, "Sent reload signal to relay (pid %d)\n", pid);
        } else {
            fprintf(stderr, "relay is not running\n");
        }
        return 0;

    /* ── log ──────────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "log") == 0) {
        /* Load config just to get log path */
        char log_path[RELAY_MAX_PATH];
        config_t *c = config_load(config_path);
        snprintf(log_path, sizeof(log_path), "%s",
                 c ? config_get(c, "log_file", "logs/relay.log") : "logs/relay.log");
        config_free(c);

        /* Check for --watch/-w and line count in remaining args */
        int watch = 0;
        int lines = 20;
        for (int j = 2; j < argc; j++) {
            if (strcmp(argv[j], "--watch") == 0 ||
                strcmp(argv[j], "-w") == 0) {
                watch = 1;
            } else if (strcmp(argv[j], "-c") == 0 && j + 1 < argc) {
                j++; /* skip -c value */
            } else {
                int n = atoi(argv[j]);
                if (n > 0) {
                    lines = n;
                }
            }
        }

        if (watch) {
            execlp("tail", "tail", "-f", log_path, (char *)NULL);
        } else {
            char line_arg[32];
            snprintf(line_arg, sizeof(line_arg), "-%d", lines);
            execlp("tail", "tail", line_arg, log_path, (char *)NULL);
        }
        perror("tail");
        return 1;

    /* ── send-file ────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "send-file") == 0) {
        /* Positional args: relay send-file <file_path> [caption] [-c path] */
        const char *file_path = NULL;
        const char *caption = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                i++; /* skip -c value, already parsed */
            } else if (!file_path) {
                file_path = argv[i];
            } else {
                caption = argv[i];
            }
        }

        if (!file_path) {
            fprintf(stderr, "Usage: relay send-file <file_path> [caption] [-c path]\n");
            return 1;
        }

        /* Init curl (required for HTTP) */
        curl_global_init(CURL_GLOBAL_ALL);

        /* Load minimal config to get Telegram credentials */
        config_t *tmp_cfg = config_load(config_path);
        if (!tmp_cfg) {
            fprintf(stderr, "Failed to load config\n");
            curl_global_cleanup();
            return 1;
        }

        /* Create temporary Telegram instance */
        telegram_t *tmp_tg = telegram_create(&real_http, tmp_cfg);
        if (!tmp_tg) {
            fprintf(stderr, "Failed to initialize Telegram client\n");
            config_free(tmp_cfg);
            curl_global_cleanup();
            return 1;
        }

        /* Get authorized user ID from config */
        const char *chat_id = config_get(tmp_cfg, "telegram_user_id", NULL);
        if (!chat_id) {
            fprintf(stderr, "No telegram_user_id in config\n");
            telegram_free(tmp_tg);
            config_free(tmp_cfg);
            curl_global_cleanup();
            return 1;
        }

        /* Send document */
        int rc = telegram_send_document(tmp_tg, chat_id, file_path, caption);
        if (rc == RELAY_OK) {
            fprintf(stderr, "Document sent successfully\n");
        } else {
            fprintf(stderr, "Failed to send document\n");
        }

        telegram_free(tmp_tg);
        config_free(tmp_cfg);
        curl_global_cleanup();
        return rc == RELAY_OK ? 0 : 1;

    /* ── memory flush ─────────────────────────────────────────────── */
    } else if (strcmp(cmd, "memory") == 0) {
        const char *subcmd = (argc > 2) ? argv[2] : "";
        if (strcmp(subcmd, "flush") != 0) {
            fprintf(stderr, "Usage: relay memory flush\n");
            return 1;
        }

        config_t *mc = config_load(config_path);
        if (!mc) {
            fprintf(stderr, "Failed to load config: %s\n", config_path);
            return 1;
        }

        const char *svc_url = config_get(mc, "memory_service_url",
                                         "http://localhost:8765");

        /* Derive agent_home: config is at {agent_home}/config/relay.conf */
        char agent_home[RELAY_MAX_PATH];
        snprintf(agent_home, sizeof(agent_home), "%s", config_path);
        {
            char *p = strrchr(agent_home, '/');
            if (p) *p = '\0';  /* strip filename */
            p = strrchr(agent_home, '/');
            if (p) *p = '\0';  /* strip config/ dir */
        }

        char ingest_url[RELAY_MAX_URL];
        char body[RELAY_MAX_PATH + 32];
        char resp_buf[512];
        int ok = 1;

        snprintf(body, sizeof(body), "{\"agent_home\":\"%s\"}", agent_home);

        snprintf(ingest_url, sizeof(ingest_url), "%s/ingest_daily_logs", svc_url);
        if (real_http.post(ingest_url, body, resp_buf, sizeof(resp_buf))
                != RELAY_OK) {
            fprintf(stderr, "ingest_daily_logs failed\n");
            ok = 0;
        }

        snprintf(ingest_url, sizeof(ingest_url), "%s/ingest_transcripts", svc_url);
        if (real_http.post(ingest_url, body, resp_buf, sizeof(resp_buf))
                != RELAY_OK) {
            fprintf(stderr, "ingest_transcripts failed\n");
            ok = 0;
        }

        if (ok) {
            fprintf(stderr, "Memory flush triggered\n");
        }
        config_free(mc);
        return ok ? 0 : 1;

    /* ── config (validate) ────────────────────────────────────────── */
    } else if (strcmp(cmd, "config") == 0) {
        config_t *c = config_load(config_path);
        if (!c) {
            fprintf(stderr, "Failed to load config: %s\n", config_path);
            return 1;
        }
        char errs[12][RELAY_MAX_VALUE];
        int nerr = config_validate_options(c, errs, 12);
        if (nerr > 0) {
            fprintf(stderr, "Configuration error(s) in %s:\n", config_path);
            for (int i = 0; i < nerr; i++) {
                fprintf(stderr, "  - %s\n", errs[i]);
            }
            config_free(c);
            return 1;
        }
        config_free(c);
        fprintf(stderr, "Config OK: %s\n", config_path);
        return 0;

    /* ── restart ──────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "restart") == 0) {
        char pid_path[RELAY_MAX_PATH];
        get_pid_path_from_config(config_path, pid_path, sizeof(pid_path));
        int pid = read_pid_file(pid_path);
        int rc = stop_daemon_by_pidfile(pid_path, 8000);
        if (rc == 1) {
            fprintf(stderr, "Restart: stopped existing relay (pid %d)\n", pid);
        } else if (rc == 0) {
            fprintf(stderr, "Restart: no running relay found\n");
        } else {
            fprintf(stderr, "Restart failed: could not stop relay (pid %d)\n", pid);
            return 1;
        }
        /* fall through to start daemon */

    /* ── start ────────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "start") == 0) {
        /* fall through to start daemon */

    /* ── unknown command ──────────────────────────────────────────── */
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        usage();
        return 1;
    }

    /* Load config */
    config_t *cfg = config_load(config_path);
    if (!cfg) {
        fprintf(stderr, "Failed to load config: %s\n", config_path);
        return 1;
    }

    char validation_errors[12][RELAY_MAX_VALUE];
    int validation_count = config_validate_options(cfg, validation_errors, 12);
    if (validation_count > 0) {
        fprintf(stderr, "Configuration error(s) in %s:\n", config_path);
        for (int i = 0; i < validation_count; i++) {
            fprintf(stderr, "  - %s\n", validation_errors[i]);
        }
        config_free(cfg);
        return 1;
    }

    /* Set process timezone from config (IANA name, e.g. "Australia/Brisbane") */
    const char *tz = config_get(cfg, "timezone", NULL);
    if (tz) {
        setenv("TZ", tz, 1);
        tzset();
    }

    /* Check config file permissions */
    struct stat st;
    if (stat(config_path, &st) == 0 && (st.st_mode & 077)) {
        fprintf(stderr, "WARNING: %s is readable by others. "
                "Run: chmod 600 %s\n", config_path, config_path);
    }

    /* Init curl */
    curl_global_init(CURL_GLOBAL_ALL);

    /* Create logger */
    relay_log_t *log = log_create(
        config_get(cfg, "log_file", "logs/relay.log"),
        LOG_INFO, &real_clock);
    if (!log) {
        fprintf(stderr, "Failed to create logger\n");
        config_free(cfg);
        return 1;
    }
    g_log = log; /* expose to proc_spawn_streaming for exit-code/stderr logging */

    /* Daemonize if not foreground */
    if (!foreground) {
        if (daemonize() != RELAY_OK) {
            log_write(log, LOG_ERROR, "Failed to daemonize");
            log_close(log);
            config_free(cfg);
            return 1;
        }
    }

    /* Write PID file — handles stale cleanup and live-process detection */
    char pid_path[RELAY_MAX_PATH];
    const char *explicit_pid = config_get(cfg, "pid_file", NULL);
    if (explicit_pid) {
        snprintf(pid_path, sizeof(pid_path), "%s", explicit_pid);
    } else if (pid_default_path(config_path, pid_path, sizeof(pid_path)) != RELAY_OK) {
        snprintf(pid_path, sizeof(pid_path), "/tmp/relay.pid");
    }

    int pid_rc = pid_startup_check(pid_path, (int)getpid(),
                                   &PID_PROC_REAL, &real_fs, log);
    if (pid_rc != RELAY_OK) {
        log_close(log);
        config_free(cfg);
        return 1;
    }

    /* Signal handlers */
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Create all components */
    telegram_t *tg = telegram_create(&real_http, cfg);

    llm_provider_t *llm = llm_provider_create(&real_proc, cfg);
    memory_search_t *memory = memory_search_create(&real_proc, &real_http, cfg);
    memory_sidecar_t *sidecar = memory_sidecar_create(&real_http,
                                                       &MEMORY_SIDECAR_REAL_PROC,
                                                       cfg);
    memory_curator_t *curator = memory_curator_create(llm, &real_http, &real_fs, cfg);
    llm_provider_set_memory(llm, memory);
    llm_provider_set_identity(llm, &real_fs,
                               config_get(cfg, "workspace_path", "."));
    session_store_t *sessions = session_create(
        &real_fs, &real_clock,
        config_get(cfg, "session_file", "config/sessions.json"),
        config_get_int(cfg, "session_expiry_hours", 24));
    health_t *health = health_create(5);
    transcript_t *tx = transcript_create(
        &real_fs, &real_clock,
        config_get(cfg, "transcript_dir", "data/transcripts"));
    vision_t *vision = vision_create(&real_http, cfg);
    int agent_bus_enabled = 0;  /* Set below after component checks */

    if (!tg || !llm || !sessions || !health || !tx) {
        log_write(log, LOG_ERROR, "Failed to initialize components");
        goto cleanup;
    }

    /* Register bot commands with Telegram so the "/" picker is populated.
     * Non-fatal: log the error but continue if it fails. */
    if (telegram_register_commands(tg) != RELAY_OK) {
        log_write(log, LOG_WARN,
                  "Failed to register bot commands with Telegram "
                  "(setMyCommands) — command picker may not appear");
    } else {
        log_write(log, LOG_INFO, "Telegram bot commands registered");
    }

    log_write(log, LOG_INFO, "relay " RELAY_VERSION " initialized");
    log_write(log, LOG_INFO, "Telegram user: %s",
              config_get(cfg, "telegram_user_id", "(not set)"));
    const char *provider_name = llm_provider_name(llm);
    const char *provider_binary = config_get(cfg, "claude_binary", "(not set)");
    if (strcmp(provider_name, "openai_codex") == 0) {
        provider_binary = config_get(cfg, "openai_binary", "(not set)");
    } else if (strcmp(provider_name, "gemini") == 0) {
        provider_binary = config_get(cfg, "gemini_binary", "(not set)");
    }
    log_write(log, LOG_INFO, "LLM provider: %s", provider_name);
    log_write(log, LOG_INFO, "LLM binary: %s", provider_binary);
    log_write(log, LOG_INFO, "Workspace: %s",
              config_get(cfg, "workspace_path", "(not set)"));
    log_write(log, LOG_INFO, "Memory search: %s",
              memory ? "enabled" : "disabled");
    if (vision_is_enabled(vision)) {
        log_write(log, LOG_INFO, "Vision: enabled (model: %s, url: %s)",
                  config_get(cfg, "vision_model", "moondream"),
                  config_get(cfg, "vision_ollama_url", "http://localhost:11434"));
    } else {
        log_write(log, LOG_INFO, "Vision: disabled (set vision_model to enable)");
    }

    /* Agent Bus (inter-agent communication via Unix domain socket) */
    agent_bus_enabled = config_get_int(cfg, "agent_bus_enabled", 0);
    if (agent_bus_enabled) {
        char agent_bus_path[RELAY_MAX_PATH];
        const char *bus_sock = config_get(cfg, "agent_bus_socket", NULL);
        if (bus_sock) {
            snprintf(agent_bus_path, sizeof(agent_bus_path), "%s", bus_sock);
        } else {
            const char *ws = config_get(cfg, "workspace_path", ".");
            snprintf(agent_bus_path, sizeof(agent_bus_path), "%s/relay.sock", ws);
        }
        int bus_rate = config_get_int(cfg, "agent_bus_rate_limit", 10);
        agent_bus_set_rate_limit(bus_rate);
        if (agent_bus_init(agent_bus_path) == RELAY_OK) {
            log_write(log, LOG_INFO, "Agent bus: listening on %s", agent_bus_path);
        } else {
            log_write(log, LOG_WARN, "Agent bus: init failed — inter-agent chat disabled");
            agent_bus_enabled = 0;
        }
    }

    /* Create and run event loop */
    event_loop_deps_t deps = {
        .telegram = tg,
        .llm = llm,
        .sessions = sessions,
        .health = health,
        .transcript = tx,
        .log = log,
        .http = &real_http,
        .vision = vision,
        .sidecar = sidecar,
        .curator = curator,
        .cfg = cfg,
        .proc = &real_proc,
        .fs = &real_fs,
        .config_path = config_path
    };

    g_loop = event_loop_create(&deps);
    if (!g_loop) {
        log_write(log, LOG_ERROR, "Failed to create event loop");
        goto cleanup;
    }

    event_loop_run(g_loop);

cleanup:
    log_write(log, LOG_INFO, "relay shutting down");
    if (agent_bus_enabled) agent_bus_destroy();

    if (g_loop) {
        event_loop_free(g_loop);  /* Also frees config */
    } else {
        config_free(cfg);  /* Event loop never created — free here */
    }
    telegram_free(tg);
    llm_provider_free(llm);
    memory_search_free(memory);
    memory_sidecar_free(sidecar);
    memory_curator_free(curator);
    session_free(sessions);
    health_free(health);
    transcript_free(tx);
    vision_free(vision);

    pid_file_remove(pid_path, &real_fs);
    profiler_close();
    log_close(log);
    curl_global_cleanup();

    return 0;
}
