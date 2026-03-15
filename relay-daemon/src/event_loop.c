#include "event_loop.h"
#include "el_reply.h"
#include "el_spinner.h"
#include "cmd_workspace.h"
#include "workspace_resolver.h"
#include "config_validator.h"
#include "interruption.h"
#include "llm_prompt.h"
#include "agent_bus.h"
#include "group_chat_context.h"
#include "pending_bus_messages.h"
#include "pending_response.h"
#include "telegram_offset.h"
#include "transcript.h"
#include "profiler.h"
#include "voice_pipeline.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

/* ── Forward declarations ────────────────────────────────────────────── */

static int handle_reload(event_loop_t *loop);

/* ── Global session registry for interruption support ──────────────── */

active_sessions_t *g_active_sessions = NULL;
pthread_mutex_t g_sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Sent message ring buffer (for reaction lookup) ─────────────────── */

#define SENT_MESSAGES_CAPACITY 50
#define SENT_MESSAGES_TEXT_MAX 512

typedef struct {
    int  message_id;
    char text[SENT_MESSAGES_TEXT_MAX];
} sent_message_t;

static sent_message_t g_sent_messages[SENT_MESSAGES_CAPACITY];
static int g_sent_messages_head = 0;
static pthread_mutex_t g_sent_messages_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Internal types ─────────────────────────────────────────────────── */

struct event_loop {
    event_loop_deps_t deps;
    int running;
    volatile int reload_requested;
    time_t start_time;          /* For restart cooldown */
    time_t last_message_time;   /* For auto-memory flush */
    int memory_flushed;         /* Already flushed for this idle period */
    int flush_idle_sec;
    size_t context_window;
    size_t compaction_reserve;
    size_t compaction_soft_threshold;
    char authorized_user[RELAY_MAX_USER_ID];
    char parent_alert_path[RELAY_MAX_PATH];  /* data/state/parent-alert.json */
    char config_path[RELAY_MAX_PATH];
    long long telegram_offset;  /* Next update_id to fetch */

    /* Agent bus state */
    int  agent_bus_enabled;
    char agent_bus_log_dir[RELAY_MAX_PATH];

    /* Crash recovery: set on startup if pending-response.json was found */
    char pending_recovery_chat_id[RELAY_MAX_USER_ID];
    char recovery_context[4096];  /* Recent transcript for context injection */

    /* Health alert: tracks whether persistent-failure alert was already sent */
    int llm_alert_sent;
};

/* ── Forward declarations ───────────────────────────────────────────── */

static void handle_context_memory_flush(event_loop_t *loop, const char *chat_id);
static void get_session_key(event_loop_t *loop, const char *chat_id,
                            char *out, size_t out_size);

/* ── Helpers ────────────────────────────────────────────────────────── */

static void track_response_tokens(event_loop_t *loop, const char *session_key,
                                   const char *prompt, const char *response)
{
    /* Estimate tokens for both user message and assistant response */
    size_t prompt_tokens = session_estimate_tokens(prompt);
    size_t response_tokens = session_estimate_tokens(response);
    size_t total_tokens = prompt_tokens + response_tokens;

    /* Add to session context */
    session_add_tokens(loop->deps.sessions, session_key, total_tokens);

    /* Get current total and calculate percentage of context window */
    size_t context_tokens = session_get_context_tokens(loop->deps.sessions, session_key);
    size_t context_window = loop->context_window > 0 ? loop->context_window : 200000;
    int percent = (int)((context_tokens * 100) / context_window);

    log_write(loop->deps.log, LOG_INFO,
              "[Session %s] Context: %zu tokens (%d%%) | +%zu this turn",
              session_key, context_tokens, percent, total_tokens);
}

static void get_session_key(event_loop_t *loop, const char *chat_id,
                            char *out, size_t out_size)
{
    const char *provider = llm_provider_name(loop->deps.llm);
    snprintf(out, out_size, "%s:%s", provider, chat_id);
}

static void sent_messages_record(int message_id, const char *text)
{
    if (message_id <= 0 || !text) return;

    pthread_mutex_lock(&g_sent_messages_mutex);

    /* Update existing entry if message_id already present */
    for (int i = 0; i < SENT_MESSAGES_CAPACITY; i++) {
        if (g_sent_messages[i].message_id == message_id) {
            snprintf(g_sent_messages[i].text, SENT_MESSAGES_TEXT_MAX, "%s", text);
            pthread_mutex_unlock(&g_sent_messages_mutex);
            return;
        }
    }

    /* New entry — ring buffer insert */
    g_sent_messages[g_sent_messages_head].message_id = message_id;
    snprintf(g_sent_messages[g_sent_messages_head].text, SENT_MESSAGES_TEXT_MAX,
             "%s", text);
    g_sent_messages_head = (g_sent_messages_head + 1) % SENT_MESSAGES_CAPACITY;

    pthread_mutex_unlock(&g_sent_messages_mutex);
}

static int sent_messages_lookup(int message_id, char *buf, size_t max)
{
    if (message_id <= 0 || !buf || max == 0) return 0;

    pthread_mutex_lock(&g_sent_messages_mutex);
    for (int i = 0; i < SENT_MESSAGES_CAPACITY; i++) {
        if (g_sent_messages[i].message_id == message_id) {
            snprintf(buf, max, "%s", g_sent_messages[i].text);
            pthread_mutex_unlock(&g_sent_messages_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_sent_messages_mutex);
    return 0;
}

static int send_telegram_with_id(event_loop_t *loop, const char *chat_id,
                                  const char *text)
{
    char url[RELAY_MAX_URL];
    telegram_api_url(loop->deps.telegram, "sendMessage", url, sizeof(url));

    /* Build JSON body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    int message_id = -1;
    if (json) {
        char resp[RELAY_MAX_MSG];
        if (loop->deps.http->post(url, json, resp, sizeof(resp)) == RELAY_OK) {
            /* Parse message_id from response */
            cJSON *root = cJSON_Parse(resp);
            if (root) {
                cJSON *result = cJSON_GetObjectItem(root, "result");
                if (result) {
                    cJSON *msg_id = cJSON_GetObjectItem(result, "message_id");
                    if (msg_id && cJSON_IsNumber(msg_id)) {
                        message_id = msg_id->valueint;
                    }
                }
                cJSON_Delete(root);
            }
        }
        free(json);
    }
    sent_messages_record(message_id, text);
    return message_id;
}

static void send_telegram(event_loop_t *loop, const char *chat_id,
                          const char *text)
{
    send_telegram_with_id(loop, chat_id, text);
}

/* Check if LLM failures have become persistent and send a one-time alert */
static void check_llm_health_alert(event_loop_t *loop, const char *last_error)
{
    if (loop->llm_alert_sent) {
        return; /* Already alerted for this failure episode */
    }
    if (!health_is_persistent(loop->deps.health, HEALTH_LLM)) {
        return; /* Not yet at threshold */
    }
    loop->llm_alert_sent = 1;

    char alert[512];
    el_health_alert_text(health_failures(loop->deps.health, HEALTH_LLM),
                         last_error, alert, sizeof(alert));
    send_telegram(loop, loop->authorized_user, alert);
    log_write(loop->deps.log, LOG_WARN, "%s", alert);
}

static void send_typing(event_loop_t *loop, const char *chat_id)
{
    char url[RELAY_MAX_URL];
    telegram_api_url(loop->deps.telegram, "sendChatAction", url, sizeof(url));

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "action", "typing");
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    if (json) {
        char resp[RELAY_MAX_MSG];
        loop->deps.http->post(url, json, resp, sizeof(resp));
        free(json);
    }
}

/* Send a successful LLM response: health update, chunk+send, session persist,
 * token tracking, transcript logging. media_type is used only for the log line. */
static void dispatch_llm_response(event_loop_t *loop, const char *chat_id,
                                  const char *session_key, const char *prompt,
                                  const llm_response_t *resp,
                                  const char *media_type)
{
    health_success(loop->deps.health, HEALTH_LLM);
    loop->llm_alert_sent = 0; /* Reset so alert can fire again */

    char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
    int n = telegram_chunk_message(resp->result, chunks, 8);
    for (int i = 0; i < n; i++) {
        send_telegram(loop, chat_id, chunks[i]);
    }

    if (resp->session_id[0] != '\0') {
        session_set(loop->deps.sessions, session_key, resp->session_id);
    }

    track_response_tokens(loop, session_key, prompt, resp->result);
    transcript_log_outbound(loop->deps.transcript, chat_id,
                           resp->result, resp->session_id, resp->duration_ms, 0);
    log_write(loop->deps.log, LOG_INFO,
              "%s from %s processed in %dms",
              media_type, chat_id, resp->duration_ms);
}

static void handle_document_message(event_loop_t *loop, telegram_message_t *msg)
{
    /* Check if context-based memory flush needed */
    handle_context_memory_flush(loop, msg->chat_id);

    /* 1. Get file path from Telegram */
    char file_path[RELAY_MAX_VALUE];
    int rc = telegram_get_file_path(loop->deps.telegram,
                                     msg->document_file_id,
                                     file_path, sizeof(file_path));
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't retrieve the document from Telegram.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to get file path for document %s: %d",
                  msg->document_file_id, rc);
        return;
    }

    /* 2. Extract extension from file_path */
    const char *ext = "file";
    const char *dot = strrchr(file_path, '.');
    if (dot && dot[1] != '\0') {
        ext = dot + 1;
    }

    /* 3. Build persistent download path (from config, fallback to workspace/data/downloads) */
    char download_path[RELAY_MAX_PATH];
    const char *dl_dir = config_get(loop->deps.cfg, "download_dir", NULL);
    if (dl_dir) {
        snprintf(download_path, sizeof(download_path),
                 "%s/%ld_%s.%s", dl_dir, time(NULL), msg->chat_id, ext);
    } else {
        const char *workspace = config_get(loop->deps.cfg, "workspace_path", ".");
        snprintf(download_path, sizeof(download_path),
                 "%s/data/downloads/%ld_%s.%s",
                 workspace, time(NULL), msg->chat_id, ext);
    }

    /* 4. Download file */
    rc = telegram_download_file(loop->deps.telegram, file_path, download_path);
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't download the document.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to download document to %s: %d", download_path, rc);
        return;
    }

    /* 5. Build prompt */
    const char *user_name = config_get(loop->deps.cfg, "user_name", "The user");
    char prompt[RELAY_MAX_MSG];
    if (msg->caption[0] != '\0') {
        snprintf(prompt, sizeof(prompt),
                 "%s sent you a document (saved to %s): \"%s\"\n\n"
                 "The file is now available for you to read and process.",
                 user_name, download_path, msg->caption);
    } else {
        snprintf(prompt, sizeof(prompt),
                 "%s sent you a document: %s\n\n"
                 "The file is now available. Would you like me to read it?",
                 user_name, download_path);
    }

    /* 8. Log inbound, send typing, get session */
    log_write(loop->deps.log, LOG_INFO,
              "Document from %s (file_id=%s, saved=%s)",
              msg->chat_id, msg->document_file_id, download_path);
    send_typing(loop, msg->chat_id);
    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, msg->chat_id, session_key, sizeof(session_key));
    const char *session_id = session_get(loop->deps.sessions, session_key);

    /* 7. Send to LLM */
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    rc = llm_provider_send_with_retry(loop->deps.llm, prompt, session_id, &resp);

    /* 8. Handle response */
    if (rc != RELAY_OK || resp.is_error) {
        health_failure(loop->deps.health, HEALTH_LLM);
        check_llm_health_alert(loop, resp.result);
        send_telegram(loop, msg->chat_id,
                     "Sorry, I encountered an error processing your document.");
        log_write(loop->deps.log, LOG_ERROR,
                  "LLM error for document: rc=%d, is_error=%d, detail=%.200s",
                  rc, resp.is_error, resp.result);
        { char diag[512]; el_spawn_diag_text(diag, sizeof(diag));
          log_write(loop->deps.log, LOG_INFO, "%s", diag); }
        return;
    }

    dispatch_llm_response(loop, msg->chat_id, session_key, prompt, &resp, "Document");
}

static void handle_voice_message(event_loop_t *loop, telegram_message_t *msg)
{
    /* Handle voice message through Python voice pipeline */
    handle_context_memory_flush(loop, msg->chat_id);

    /* 1. Get voice file path from Telegram */
    char file_path[RELAY_MAX_VALUE];
    int rc = telegram_get_file_path(loop->deps.telegram,
                                     msg->voice_file_id,
                                     file_path, sizeof(file_path));
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't retrieve the voice message.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to get file path for voice %s: %d",
                  msg->voice_file_id, rc);
        return;
    }

    /* 2. Build download path */
    char temp_path[RELAY_MAX_PATH];
    const char *dl_dir = config_get(loop->deps.cfg, "download_dir", NULL);
    if (dl_dir) {
        snprintf(temp_path, sizeof(temp_path),
                 "%s/voice_%ld_%s.ogg", dl_dir, time(NULL), msg->chat_id);
    } else {
        const char *workspace = config_get(loop->deps.cfg, "workspace_path", ".");
        snprintf(temp_path, sizeof(temp_path),
                 "%s/data/downloads/voice_%ld_%s.ogg",
                 workspace, time(NULL), msg->chat_id);
    }

    /* 3. Download voice file */

    rc = telegram_download_file(loop->deps.telegram, file_path, temp_path);
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't download the voice message.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to download voice to %s: %d", temp_path, rc);
        return;
    }

    /* 3. Send typing indicator */
    send_typing(loop, msg->chat_id);

    /* 4. Process through voice pipeline (safe fork+exec, no shell) */
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        send_telegram(loop, msg->chat_id,
                     "Sorry, voice processing failed.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Cannot run voice pipeline: HOME is not set");
        unlink(temp_path);
        return;
    }

    log_write(loop->deps.log, LOG_INFO,
              "Processing voice message from %s", msg->chat_id);

    char output[8192];
    int voice_rc = voice_pipeline_run(home, msg->chat_id, temp_path,
                                       loop->deps.proc, output, sizeof(output));
    if (voice_rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, voice processing failed.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Voice pipeline spawn failed: %d", voice_rc);
        unlink(temp_path);
        return;
    }

    /* 5. Parse JSON response */
    cJSON *json = cJSON_Parse(output);
    if (!json) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, voice processing failed.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to parse voice pipeline output: %.200s", output);
        unlink(temp_path);
        return;
    }

    cJSON *status = cJSON_GetObjectItem(json, "status");
    if (!status || strcmp(status->valuestring, "success") != 0) {
        cJSON *error = cJSON_GetObjectItem(json, "error");
        send_telegram(loop, msg->chat_id,
                     "Sorry, I had trouble processing your voice message.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Voice pipeline error: %s",
                  error ? error->valuestring : "unknown");
        cJSON_Delete(json);
        unlink(temp_path);
        return;
    }

    /* 6. Get response text and audio */
    cJSON *transcription = cJSON_GetObjectItem(json, "transcription");
    cJSON *response_text = cJSON_GetObjectItem(json, "response_text");
    cJSON *response_audio = cJSON_GetObjectItem(json, "response_audio_path");

    /* 7. Send text response (for now - TODO: add voice reply) */
    if (response_text && response_text->valuestring) {
        char reply[RELAY_MAX_MSG];
        if (transcription && transcription->valuestring) {
            snprintf(reply, sizeof(reply),
                     "🎤 You said: \"%s\"\n\n%s",
                     transcription->valuestring,
                     response_text->valuestring);
        } else {
            snprintf(reply, sizeof(reply), "%s", response_text->valuestring);
        }
        send_telegram(loop, msg->chat_id, reply);
    }

    /* 8. Clean up */
    unlink(temp_path);
    if (response_audio && response_audio->valuestring) {
        unlink(response_audio->valuestring);
    }
    cJSON_Delete(json);

    log_write(loop->deps.log, LOG_INFO,
              "Voice message processed and response sent to %s", msg->chat_id);
}

/* ── Worker thread for async Claude processing ──────────────────────── */

/* External function to set chat context for PID tracking */
extern void proc_set_current_chat_id(const char *chat_id);

typedef struct {
    event_loop_t *loop;
    telegram_message_t msg;
    char chat_id[RELAY_MAX_USER_ID];
    char request_id[96];
    int placeholder_msg_id;
} claude_work_t;

typedef struct {
    event_loop_t *loop;
    char chat_id[RELAY_MAX_USER_ID];
    char request_id[96];
    char emoji[32];
    int  message_id;
} reaction_work_t;

typedef struct {
    event_loop_t *loop;
    char session_id[RELAY_MAX_SESSION_ID];
    char flush_prompt[RELAY_MAX_MSG];
} memory_flush_work_t;

static long ms_since(struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000 +
           (now.tv_nsec - start->tv_nsec) / 1000000;
}

static void make_request_id(const char *prefix, const char *chat_id,
                            int message_id, char *out, size_t out_size)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(out, out_size, "%s-%lld-%s-%d-%lu",
             prefix ? prefix : "req",
             (long long)ts.tv_sec,
             chat_id ? chat_id : "unknown",
             message_id,
             (unsigned long)pthread_self());
}

/* ── Streaming callback context ─────────────────────────────────────── */

typedef struct {
    event_loop_t    *loop;
    const char      *chat_id;
    int              placeholder_msg_id;
    char             accumulated[RELAY_MAX_RESPONSE];
    size_t           accumulated_len;
    int              first_chunk_sent;  /* has the placeholder been replaced yet? */
    volatile time_t  last_output_time;  /* last time a paragraph was sent */
    pthread_mutex_t  placeholder_mutex; /* guards placeholder edits vs spinner */
} stream_ctx_t;

/* ── Spinner thread ─────────────────────────────────────────────────── */

typedef struct {
    event_loop_t    *loop;
    char             chat_id[RELAY_MAX_USER_ID];
    int              placeholder_msg_id;
    volatile int     stop;           /* set to 1 to request exit */
    int             *first_chunk_sent; /* shared with stream_ctx */
    volatile time_t *last_output_time; /* shared with stream_ctx */
    pthread_mutex_t *placeholder_mutex;
} spinner_arg_t;

static void *spinner_thread_fn(void *arg)
{
    spinner_arg_t *s = (spinner_arg_t *)arg;
    int tick = 0;
    int heartbeat_msg_id = 0;  /* message ID for heartbeat updates */

    /* Phase 1: Spinner animation (2s interval) until first token arrives */
    while (!s->stop) {
        for (int i = 0; i < 10 && !s->stop; i++) {
            struct timespec ts = {0, 200 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        if (s->stop) {
            break;
        }

        pthread_mutex_lock(s->placeholder_mutex);
        if (*s->first_chunk_sent) {
            pthread_mutex_unlock(s->placeholder_mutex);
            break;  /* Transition to phase 2 */
        }
        telegram_edit_message(s->loop->deps.telegram, s->chat_id,
                              s->placeholder_msg_id,
                              el_spinner_frame(tick++));
        pthread_mutex_unlock(s->placeholder_mutex);
    }

    /* Phase 2: Heartbeat (30s interval) while tokens are stalled */
    while (!s->stop) {
        /* Sleep 5 seconds in 200ms increments for responsive cancellation */
        for (int i = 0; i < 25 && !s->stop; i++) {
            struct timespec ts = {0, 200 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        if (s->stop) {
            break;
        }

        time_t now = time(NULL);
        time_t last = *s->last_output_time;
        int elapsed = (int)(now - last);

        if (elapsed >= 30) {
            char buf[128];
            el_heartbeat_text(elapsed, buf, sizeof(buf));

            if (heartbeat_msg_id > 0) {
                /* Update existing heartbeat message */
                telegram_edit_message(s->loop->deps.telegram, s->chat_id,
                                      heartbeat_msg_id, buf);
            } else {
                /* Send first heartbeat as a new message */
                heartbeat_msg_id = telegram_send_text(
                    s->loop->deps.telegram, s->chat_id, buf);
            }
        }
    }

    return NULL;
}

static int on_llm_token(const char *text, size_t len, void *userdata)
{
    stream_ctx_t *ctx = userdata;

    /* Append to accumulated buffer */
    if (ctx->accumulated_len + len >= RELAY_MAX_RESPONSE - 1) {
        log_write(ctx->loop->deps.log, LOG_WARN,
                  "[stream] token dropped — buffer full (%zu)", ctx->accumulated_len);
        return 0;
    }
    memcpy(ctx->accumulated + ctx->accumulated_len, text, len);
    ctx->accumulated_len += len;
    ctx->accumulated[ctx->accumulated_len] = '\0';

    /* Send each complete paragraph (double-newline boundary) as its own
     * Telegram message.  The user can read each paragraph as it arrives
     * without a single message mutating beneath them. */
    while (1) {
        char *boundary = strstr(ctx->accumulated, "\n\n");
        if (!boundary) {
            break;
        }

        /* Paragraph text: from start up to (but not including) the \n\n.
         * Strip any trailing whitespace so messages look clean. */
        size_t para_len = (size_t)(boundary - ctx->accumulated);
        while (para_len > 0 &&
               (ctx->accumulated[para_len - 1] == ' '  ||
                ctx->accumulated[para_len - 1] == '\n' ||
                ctx->accumulated[para_len - 1] == '\r')) {
            para_len--;
        }

        if (para_len > 0) {
            /* Use a local buffer; telegram_chunk_message handles >4096 chars */
            char para[RELAY_MAX_RESPONSE];
            if (para_len >= RELAY_MAX_RESPONSE) {
                para_len = RELAY_MAX_RESPONSE - 1;
            }
            memcpy(para, ctx->accumulated, para_len);
            para[para_len] = '\0';

            char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
            int n = telegram_chunk_message(para, chunks, 8);

            if (!ctx->first_chunk_sent && ctx->placeholder_msg_id > 0) {
                /* Stop the spinner and replace the placeholder with real content */
                pthread_mutex_lock(&ctx->placeholder_mutex);
                ctx->first_chunk_sent = 1;
                if (n > 0) {
                    telegram_edit_message(ctx->loop->deps.telegram,
                                         ctx->chat_id,
                                         ctx->placeholder_msg_id,
                                         chunks[0]);
                    sent_messages_record(ctx->placeholder_msg_id, chunks[0]);
                }
                pthread_mutex_unlock(&ctx->placeholder_mutex);
                for (int i = 1; i < n; i++) {
                    int mid = send_telegram_with_id(ctx->loop, ctx->chat_id, chunks[i]);
                    if (mid > 0) sent_messages_record(mid, chunks[i]);
                }
            } else {
                for (int i = 0; i < n; i++) {
                    int mid = send_telegram_with_id(ctx->loop, ctx->chat_id, chunks[i]);
                    if (mid > 0) sent_messages_record(mid, chunks[i]);
                }
            }
            ctx->last_output_time = time(NULL);
            log_write(ctx->loop->deps.log, LOG_INFO,
                      "[stream] paragraph sent len=%zu", para_len);
        }

        /* Skip past the \n\n and any additional blank lines */
        char *next = boundary + 2;
        while (*next == '\n' || *next == '\r') {
            next++;
        }

        /* Shift the buffer: remove what was just sent */
        size_t remaining = ctx->accumulated_len - (size_t)(next - ctx->accumulated);
        if (remaining > 0) {
            memmove(ctx->accumulated, next, remaining);
        }
        ctx->accumulated_len = remaining;
        ctx->accumulated[remaining] = '\0';
    }

    return 0; /* continue streaming */
}

static void *claude_worker_thread(void *arg)
{
    claude_work_t *work = (claude_work_t *)arg;
    event_loop_t *loop = work->loop;
    telegram_message_t *msg = &work->msg;
    profiler_set_context(work->request_id, work->chat_id,
                         llm_provider_name(loop->deps.llm));
    profiler_emit_event("request.worker_start", 0, "ok", "telegram");

    /* T0: Worker thread started */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Set chat context for PID tracking (thread-local) */
    proc_set_current_chat_id(work->chat_id);

    /* Get session key */
    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, work->chat_id, session_key, sizeof(session_key));

    /* Resolve active workspace */
    resolved_workspace_t resolved_ws;
    workspace_resolve(loop->deps.sessions, loop->deps.cfg,
                      work->chat_id, &resolved_ws);
    if (resolved_ws.is_error) {
        send_telegram(loop, work->chat_id,
                      "No workspace configured. "
                      "Add a [workspace \"name\"] block to relay.conf.");
        free(work);
        profiler_emit_event("request.total", ms_since(&t0), "error",
                            "no_workspace");
        profiler_clear_context();
        return NULL;
    }
    /* Only resume existing Claude session if workspace hasn't changed.
     * If workspace changed, pass NULL so Claude starts fresh in the new one. */
    const char *session_id = session_get_if_workspace_matches(
        loop->deps.sessions, session_key, resolved_ws.name);

    /* Tag session entry with resolved workspace metadata */
    if (resolved_ws.name[0] != '\0') {
        session_set_workspace(loop->deps.sessions, session_key,
                              resolved_ws.name,
                              resolved_ws.path,
                              resolved_ws.provider);
    }
    const char *workspace = resolved_ws.path;

    /* Stream tokens back to Telegram as they arrive */
    stream_ctx_t stream_ctx;
    memset(&stream_ctx, 0, sizeof(stream_ctx));
    stream_ctx.loop = loop;
    stream_ctx.chat_id = work->chat_id;
    stream_ctx.placeholder_msg_id = work->placeholder_msg_id;
    stream_ctx.last_output_time = time(NULL);
    pthread_mutex_init(&stream_ctx.placeholder_mutex, NULL);

    /* Start spinner thread to animate the placeholder every 2 seconds,
     * then transition to heartbeat mode after first token arrives. */
    spinner_arg_t spinner_arg;
    memset(&spinner_arg, 0, sizeof(spinner_arg));
    spinner_arg.loop = loop;
    snprintf(spinner_arg.chat_id, RELAY_MAX_USER_ID, "%s", work->chat_id);
    spinner_arg.placeholder_msg_id = work->placeholder_msg_id;
    spinner_arg.first_chunk_sent   = &stream_ctx.first_chunk_sent;
    spinner_arg.last_output_time   = &stream_ctx.last_output_time;
    spinner_arg.placeholder_mutex  = &stream_ctx.placeholder_mutex;
    spinner_arg.stop = 0;

    pthread_t spinner_thread;
    int spinner_started = (work->placeholder_msg_id > 0) &&
                          (pthread_create(&spinner_thread, NULL,
                                          spinner_thread_fn, &spinner_arg) == 0);

    /* Inject recent group chat context so Telegram one-on-ones remember
     * what was discussed in group sessions via the dashboard. */
    char ctx_buf[4096] = "";
    group_chat_context_load(workspace, ctx_buf, sizeof(ctx_buf));

    /* Consume recovery context (crash recovery transcript injection).
     * Cleared after first use so it only applies to the first message. */
    char recovery_buf[4096] = "";
    if (loop->recovery_context[0] != '\0') {
        snprintf(recovery_buf, sizeof(recovery_buf), "%s",
                 loop->recovery_context);
        loop->recovery_context[0] = '\0';
    }

    char full_prompt[12288];
    if (recovery_buf[0] != '\0' || ctx_buf[0] != '\0') {
        int off = 0;
        if (recovery_buf[0] != '\0') {
            off += snprintf(full_prompt + off, sizeof(full_prompt) - (size_t)off,
                            "[Recent conversation before restart]:\n%s\n",
                            recovery_buf);
        }
        if (ctx_buf[0] != '\0') {
            off += snprintf(full_prompt + off, sizeof(full_prompt) - (size_t)off,
                            "%s\n", ctx_buf);
        }
        snprintf(full_prompt + off, sizeof(full_prompt) - (size_t)off,
                 "[Current message from user]: %s", msg->text);
    } else {
        snprintf(full_prompt, sizeof(full_prompt), "%s", msg->text);
    }

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));

    profiler_timer_t llm_timer;
    profiler_timer_start(&llm_timer);
    int rc = llm_provider_send_streaming(loop->deps.llm, full_prompt, session_id,
                                          resolved_ws.path,
                                          on_llm_token, &stream_ctx, &resp);
    long llm_stage_ms = profiler_timer_elapsed_ms(&llm_timer);
    profiler_emit_event("llm.request", llm_stage_ms,
                        (rc == RELAY_OK && !resp.is_error) ? "ok" : "error",
                        "send_streaming");

    /* Stop and join the spinner thread now that we have a response */
    if (spinner_started) {
        spinner_arg.stop = 1;
        pthread_join(spinner_thread, NULL);
    }
    pthread_mutex_destroy(&stream_ctx.placeholder_mutex);

    /* T1: LLM response received */
    long t1_ms = ms_since(&t0);
    log_write(loop->deps.log, LOG_INFO,
              "[TIMING] LLM total (memory+claude): %ldms", t1_ms);

    /* Handle response */
    if (rc != RELAY_OK || resp.is_error) {
        health_failure(loop->deps.health, HEALTH_LLM);
        check_llm_health_alert(loop, resp.result);

        log_write(loop->deps.log, LOG_ERROR,
                  "LLM error after retries: rc=%d, is_error=%d, detail=%.200s",
                  rc, resp.is_error, resp.result);
        { char diag[512]; el_spawn_diag_text(diag, sizeof(diag));
          log_write(loop->deps.log, LOG_INFO, "%s", diag); }

        /* Notify user — do not expose raw error detail */
        {
            const char *apology;
            if (rc == RELAY_ERR_TIMEOUT || rc == RELAY_ERR_SIGNAL) {
                apology = el_timeout_reply_text(stream_ctx.first_chunk_sent);
            } else {
                apology = el_llm_error_text();
            }
            if (work->placeholder_msg_id > 0) {
                telegram_edit_message(loop->deps.telegram, work->chat_id,
                                      work->placeholder_msg_id, apology);
            } else {
                send_telegram(loop, work->chat_id, apology);
            }
        }

        profiler_emit_event("request.total", ms_since(&t0), "error",
                            "llm_failure");
    } else {
        profiler_timer_t tg_timer;
        profiler_timer_start(&tg_timer);
        /* ── Flush remaining partial paragraph from streaming buffer ── */
        /* Strip trailing whitespace the response may end with */
        while (stream_ctx.accumulated_len > 0 &&
               (stream_ctx.accumulated[stream_ctx.accumulated_len - 1] == '\n' ||
                stream_ctx.accumulated[stream_ctx.accumulated_len - 1] == '\r' ||
                stream_ctx.accumulated[stream_ctx.accumulated_len - 1] == ' ')) {
            stream_ctx.accumulated[--stream_ctx.accumulated_len] = '\0';
        }

        if (stream_ctx.first_chunk_sent) {
            /* Streaming sent ≥1 paragraph already.  Send any remaining text
             * (the last paragraph, which had no trailing \n\n). */
            if (stream_ctx.accumulated_len > 0) {
                char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
                int n = telegram_chunk_message(stream_ctx.accumulated, chunks, 8);
                for (int i = 0; i < n; i++) {
                    int mid = send_telegram_with_id(loop, work->chat_id, chunks[i]);
                    if (mid > 0) sent_messages_record(mid, chunks[i]);
                }
            }
        } else if (work->placeholder_msg_id > 0) {
            /* Response had no paragraph breaks — edit placeholder with
             * the full response (same behaviour as before streaming). */
            char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
            int n = telegram_chunk_message(resp.result, chunks, 8);
            if (n > 0) {
                telegram_edit_message(loop->deps.telegram, work->chat_id,
                                      work->placeholder_msg_id, chunks[0]);
                sent_messages_record(work->placeholder_msg_id, chunks[0]);
            }
            for (int i = 1; i < n; i++) {
                send_telegram(loop, work->chat_id, chunks[i]);
            }
        } else {
            /* No placeholder (initial send failed) — send full response. */
            char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
            int n = telegram_chunk_message(resp.result, chunks, 8);
            for (int i = 0; i < n; i++) {
                send_telegram(loop, work->chat_id, chunks[i]);
            }
        }
        profiler_emit_event("telegram.send_response",
                            profiler_timer_elapsed_ms(&tg_timer),
                            "ok", "streamed_response");

        /* Fallback: if nothing was sent (empty result, no streaming tokens),
         * notify user rather than silently dropping the turn. */
        const char *send_text = el_pick_reply_text(stream_ctx.first_chunk_sent,
                                                    resp.result);
        if (send_text != resp.result) {
            log_write(loop->deps.log, LOG_WARN,
                      "empty result on apparent success (exit-code-1 scenario)");
            if (work->placeholder_msg_id > 0) {
                telegram_edit_message(loop->deps.telegram, work->chat_id,
                                      work->placeholder_msg_id, send_text);
            } else {
                send_telegram(loop, work->chat_id, send_text);
            }
        }

        /* T2: Telegram send complete */
        long t2_ms = ms_since(&t0);

        health_success(loop->deps.health, HEALTH_LLM);
        loop->llm_alert_sent = 0;
        if (resp.session_id[0] != '\0') {
            session_set(loop->deps.sessions, session_key, resp.session_id);
        }
        track_response_tokens(loop, session_key, msg->text, send_text);
        profiler_timer_t transcript_timer;
        profiler_timer_start(&transcript_timer);
        transcript_log_outbound(loop->deps.transcript, work->chat_id,
                               send_text, resp.session_id, resp.duration_ms,
                               work->placeholder_msg_id);
        profiler_emit_event("transcript.outbound",
                            profiler_timer_elapsed_ms(&transcript_timer),
                            "ok", "telegram");
        log_write(loop->deps.log, LOG_INFO,
                  "[TIMING] %s total=%ldms | llm=%ldms (claude_reported=%dms) | telegram_send=%ldms | response_len=%zu",
                  work->chat_id, t2_ms, t1_ms, resp.duration_ms, t2_ms - t1_ms,
                  strlen(resp.result));
        profiler_emit_event("request.total", t2_ms, "ok", "telegram_text");

        /* Record conversation turn for memory curation */
        if (loop->deps.curator) {
            const char *agent_home = config_get(loop->deps.cfg,
                                                "workspace_path", ".");
            memory_curator_on_turn(loop->deps.curator, msg->text,
                                   send_text, agent_home);
        }
    }

    /* Delete pending-response record — response was completed (or errored) */
    {
        const char *ws = config_get(loop->deps.cfg, "workspace_path", ".");
        pending_response_delete(ws);
    }

    /* Cleanup session from active registry */
    pthread_mutex_lock(&g_sessions_mutex);
    active_sessions_cleanup(g_active_sessions, work->chat_id);
    pthread_mutex_unlock(&g_sessions_mutex);

    profiler_clear_context();
    free(work);
    return NULL;
}

static void *reaction_worker_thread(void *arg)
{
    reaction_work_t *work = (reaction_work_t *)arg;
    event_loop_t *loop = work->loop;
    profiler_set_context(work->request_id, work->chat_id,
                         llm_provider_name(loop->deps.llm));
    profiler_timer_t total_timer;
    profiler_timer_start(&total_timer);

    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, work->chat_id, session_key, sizeof(session_key));
    const char *session_id = session_get(loop->deps.sessions, session_key);

    const char *user_name = config_get(loop->deps.cfg, "user_name", "The user");
    char reacted_text[SENT_MESSAGES_TEXT_MAX];
    reacted_text[0] = '\0';
    char prompt[128 + SENT_MESSAGES_TEXT_MAX];

    if (sent_messages_lookup(work->message_id, reacted_text, sizeof(reacted_text)) ||
        transcript_find_by_message_id(loop->deps.transcript, work->message_id,
                                      reacted_text, sizeof(reacted_text))) {
        snprintf(prompt, sizeof(prompt),
                 "%s reacted %s to your message: \"%s\"",
                 user_name, work->emoji, reacted_text);
    } else {
        snprintf(prompt, sizeof(prompt),
                 "%s reacted %s to one of your messages.",
                 user_name, work->emoji);
    }

    /* Log reaction here (after lookup) so we can include the reacted-to text */
    transcript_log_reaction(loop->deps.transcript, work->chat_id,
                            work->emoji,
                            reacted_text[0] ? reacted_text : NULL);

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    profiler_timer_t llm_timer;
    profiler_timer_start(&llm_timer);
    int rc = llm_provider_send_with_retry(loop->deps.llm, prompt, session_id, &resp);
    profiler_emit_event("llm.request", profiler_timer_elapsed_ms(&llm_timer),
                        (rc == RELAY_OK && !resp.is_error) ? "ok" : "error",
                        "reaction");

    if (rc == RELAY_OK && !resp.is_error && resp.result[0] != '\0') {
        profiler_timer_t send_timer;
        profiler_timer_start(&send_timer);
        char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
        int n = telegram_chunk_message(resp.result, chunks, 8);
        for (int i = 0; i < n; i++) {
            send_telegram(loop, work->chat_id, chunks[i]);
        }
        profiler_emit_event("telegram.send_response",
                            profiler_timer_elapsed_ms(&send_timer),
                            "ok", "reaction");
        if (resp.session_id[0] != '\0') {
            session_set(loop->deps.sessions, session_key, resp.session_id);
        }
        track_response_tokens(loop, session_key, prompt, resp.result);
        profiler_timer_t transcript_timer;
        profiler_timer_start(&transcript_timer);
        transcript_log_outbound(loop->deps.transcript, work->chat_id,
                                resp.result, resp.session_id, resp.duration_ms, 0);
        profiler_emit_event("transcript.outbound",
                            profiler_timer_elapsed_ms(&transcript_timer),
                            "ok", "reaction");
        health_success(loop->deps.health, HEALTH_LLM);
        loop->llm_alert_sent = 0;
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "reaction");
    } else {
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "reaction");
    }

    profiler_clear_context();
    free(work);
    return NULL;
}

static void handle_photo_message(event_loop_t *loop, telegram_message_t *msg)
{
    /* Check if context-based memory flush needed */
    handle_context_memory_flush(loop, msg->chat_id);

    /* 1. Get file path from Telegram */
    char file_path[RELAY_MAX_VALUE];
    int rc = telegram_get_file_path(loop->deps.telegram,
                                     msg->photo_file_id,
                                     file_path, sizeof(file_path));
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't retrieve the photo from Telegram.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to get file path for photo %s: %d",
                  msg->photo_file_id, rc);
        return;
    }

    /* 2. Extract extension from file_path (e.g. "photos/file_0.jpg") */
    const char *ext = "jpg";
    const char *dot = strrchr(file_path, '.');
    if (dot && dot[1] != '\0') {
        ext = dot + 1;
    }

    /* 3. Build persistent save path using photo_dir config */
    char temp_path[RELAY_MAX_PATH];
    const char *photo_dir = config_get(loop->deps.cfg, "photo_dir", NULL);
    const char *workspace = config_get(loop->deps.cfg, "workspace_path", NULL);
    el_build_photo_path(photo_dir, workspace,
                        msg->photo_file_id, ext,
                        temp_path, sizeof(temp_path));

    /* 4. Download file */
    rc = telegram_download_file(loop->deps.telegram, file_path, temp_path);
    if (rc != RELAY_OK) {
        send_telegram(loop, msg->chat_id,
                     "Sorry, I couldn't download the photo.");
        log_write(loop->deps.log, LOG_ERROR,
                  "Failed to download photo to %s: %d", temp_path, rc);
        return;
    }

    /* 5. Acknowledge receipt — let the user know the photo arrived before the
     * (potentially slow) vision describe or LLM call. */
    {
        char ack_msg[512];
        el_photo_ack_text(msg->caption[0] != '\0' ? msg->caption : NULL,
                          ack_msg, sizeof(ack_msg));
        send_telegram(loop, msg->chat_id, ack_msg);
    }

    /* 6. Vision: describe the image locally before sending to LLM */
    char vision_desc[4096];
    vision_desc[0] = '\0';
    if (loop->deps.vision && vision_is_enabled(loop->deps.vision)) {
        int vrc = vision_describe(loop->deps.vision, temp_path,
                                  vision_desc, sizeof(vision_desc));
        if (vrc != RELAY_OK) {
            log_write(loop->deps.log, LOG_WARN,
                      "Vision describe failed for %s: %d — proceeding without",
                      temp_path, vrc);
            vision_desc[0] = '\0';
        } else {
            log_write(loop->deps.log, LOG_INFO,
                      "Vision described image (%zu chars)", strlen(vision_desc));
        }
    }

    /* 7. Build prompt */
    const char *user_name = config_get(loop->deps.cfg, "user_name", "The user");
    char prompt[RELAY_MAX_MSG];
    if (vision_desc[0] != '\0') {
        if (msg->caption[0] != '\0') {
            snprintf(prompt, sizeof(prompt),
                     "%s sent you this image.\n"
                     "Vision analysis: %s\n"
                     "%s's caption: \"%s\"",
                     user_name, vision_desc, user_name, msg->caption);
        } else {
            snprintf(prompt, sizeof(prompt),
                     "%s sent you this image.\n"
                     "Vision analysis: %s",
                     user_name, vision_desc);
        }
    } else {
        /* Fallback: no vision available — pass file path (works with Claude) */
        if (msg->caption[0] != '\0') {
            snprintf(prompt, sizeof(prompt),
                     "%s sent you this image (saved to %s): \"%s\"",
                     user_name, temp_path, msg->caption);
        } else {
            snprintf(prompt, sizeof(prompt),
                     "%s sent you this image: %s\n\nWhat do you see?",
                     user_name, temp_path);
        }
    }

    /* 8. Log inbound, send typing, get session */
    log_write(loop->deps.log, LOG_INFO,
              "Photo from %s (file_id=%s)", msg->chat_id, msg->photo_file_id);
    send_typing(loop, msg->chat_id);
    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, msg->chat_id, session_key, sizeof(session_key));
    const char *session_id = session_get(loop->deps.sessions, session_key);

    /* 7. Resolve workspace — photos are conversational, use $HOME fallback if not set */
    resolved_workspace_t resolved_ws;
    workspace_resolve(loop->deps.sessions, loop->deps.cfg, msg->chat_id, &resolved_ws);
    char ws_fallback[RELAY_MAX_PATH];
    const char *photo_ws = el_photo_llm_workspace(&resolved_ws,
                                                   ws_fallback, sizeof(ws_fallback));

    /* 8. Send to LLM */
    log_write(loop->deps.log, LOG_INFO,
              "Photo prompt (file=%s): %.300s", temp_path, prompt);
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    rc = llm_provider_send_workspace(loop->deps.llm, prompt, session_id,
                                     photo_ws, NULL, &resp);

    /* 9. Handle response */
    if (rc != RELAY_OK || resp.is_error) {
        health_failure(loop->deps.health, HEALTH_LLM);
        check_llm_health_alert(loop, resp.result);
        send_telegram(loop, msg->chat_id,
                     "Sorry, I encountered an error processing your photo.");
        log_write(loop->deps.log, LOG_ERROR,
                  "LLM error for photo (file=%s): rc=%d, is_error=%d, detail=%.400s",
                  temp_path, rc, resp.is_error, resp.result);
        { char diag[512]; el_spawn_diag_text(diag, sizeof(diag));
          log_write(loop->deps.log, LOG_INFO, "%s", diag); }
        return;
    }

    dispatch_llm_response(loop, msg->chat_id, session_key, prompt, &resp, "Photo");
}

static void handle_reaction(event_loop_t *loop, const telegram_reaction_t *reaction)
{
    /* Reactions have no from_id in the struct; pass NULL so group reactions
     * are denied (we can't verify the sender). DM reactions still pass. */
    if (!telegram_is_authorized(loop->deps.telegram, reaction->chat_id, NULL)) {
        return;
    }
    if (reaction->emoji[0] == '\0') {
        return;  /* reaction removed — ignore */
    }
    log_write(loop->deps.log, LOG_INFO, "Reaction: %s on msg %d",
              reaction->emoji, reaction->message_id);

    reaction_work_t *work = malloc(sizeof(reaction_work_t));
    if (!work) return;
    work->loop = loop;
    snprintf(work->chat_id, sizeof(work->chat_id), "%s", reaction->chat_id);
    make_request_id("telegram-reaction", reaction->chat_id, reaction->message_id,
                    work->request_id, sizeof(work->request_id));
    snprintf(work->emoji, sizeof(work->emoji), "%s", reaction->emoji);
    work->message_id = reaction->message_id;

    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setstacksize(&tattr, 4 * 1024 * 1024);
    pthread_t thread;
    if (pthread_create(&thread, &tattr, reaction_worker_thread, work) == 0) {
        pthread_detach(thread);
    } else {
        free(work);
    }
    pthread_attr_destroy(&tattr);
}

static void handle_message(event_loop_t *loop, telegram_message_t *msg)
{
    char request_id[96];
    make_request_id("telegram", msg->chat_id, msg->message_id,
                    request_id, sizeof(request_id));
    profiler_set_context(request_id, msg->chat_id, llm_provider_name(loop->deps.llm));
    profiler_timer_t total_timer;
    profiler_timer_start(&total_timer);

    /* Authorization check */
    if (!telegram_is_authorized(loop->deps.telegram, msg->chat_id, msg->from_id)) {
        log_write(loop->deps.log, LOG_WARN,
                  "Unauthorized message from %s", msg->chat_id);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "unauthorized");
        profiler_clear_context();
        return;
    }

    /* Group chat mention check: only respond if mentioned */
    if (telegram_is_group_chat(msg->chat_id)) {
        const char *agent_name = config_get(loop->deps.cfg, "agent_name", "Kai");
        if (!telegram_message_mentions_agent(msg, agent_name)) {
            log_write(loop->deps.log, LOG_INFO,
                      "Group message without mention, ignoring");
            profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                                "ok", "ignored_group_message");
            profiler_clear_context();
            return;
        }
        log_write(loop->deps.log, LOG_INFO,
                  "Group message with mention detected");
    }

    /* Log inbound */
    profiler_timer_t transcript_timer;
    profiler_timer_start(&transcript_timer);
    transcript_log_inbound(loop->deps.transcript, msg->chat_id, msg->text);
    profiler_emit_event("transcript.inbound",
                        profiler_timer_elapsed_ms(&transcript_timer),
                        "ok", "telegram");
    loop->last_message_time = time(NULL);
    loop->memory_flushed = 0;

    /* Check if context-based memory flush needed */
    handle_context_memory_flush(loop, msg->chat_id);

    /* Handle photo messages */
    if (msg->has_photo) {
        handle_photo_message(loop, msg);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "photo");
        profiler_clear_context();
        return;
    }

    /* Handle documents */
    if (msg->has_document) {
        handle_document_message(loop, msg);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "document");
        profiler_clear_context();
        return;
    }

    /* Handle voice messages */
    if (msg->has_voice) {
        handle_voice_message(loop, msg);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "voice");
        profiler_clear_context();
        return;
    }

    /* Handle other non-text media (not yet supported) */
    if (telegram_is_media_message(msg)) {
        profiler_timer_t send_timer;
        profiler_timer_start(&send_timer);
        send_telegram(loop, msg->chat_id,
                     "Sorry, I can't process videos or stickers yet. "
                     "Photos, documents, and voice messages are supported!");
        profiler_emit_event("telegram.send_response",
                            profiler_timer_elapsed_ms(&send_timer),
                            "ok", "unsupported_media");
        log_write(loop->deps.log, LOG_WARN,
                  "Received unsupported media from %s (video=%d, sticker=%d)",
                  msg->chat_id, msg->has_video, msg->has_sticker);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "unsupported_media");
        profiler_clear_context();
        return;
    }

    /* Handle commands */
    if (msg->is_command) {
        /* Workspace commands — /session, /sessions, /workspace, /close, /clear */
        {
            char ws_reply[512] = {0};
            if (cmd_workspace_handle(loop->deps.sessions, loop->deps.cfg,
                                     msg->chat_id, msg->text,
                                     ws_reply, sizeof(ws_reply))) {
                profiler_timer_t send_timer;
                profiler_timer_start(&send_timer);
                send_telegram(loop, msg->chat_id, ws_reply);
                profiler_emit_event("telegram.send_response",
                                    profiler_timer_elapsed_ms(&send_timer),
                                    "ok", "command_workspace");
                log_write(loop->deps.log, LOG_INFO,
                          "Workspace command '%s' handled for %s",
                          msg->text, msg->chat_id);
                profiler_emit_event("request.total",
                                    profiler_timer_elapsed_ms(&total_timer),
                                    "ok", "command_workspace");
                profiler_clear_context();
                return;
            }
        }
        if (strcmp(msg->text, "/status") == 0) {
            int healthy = loop->deps.sidecar
                              ? memory_sidecar_is_healthy(loop->deps.sidecar)
                              : 0;
            send_telegram(loop, msg->chat_id, el_status_text(healthy));
            profiler_emit_event("request.total",
                                profiler_timer_elapsed_ms(&total_timer),
                                "ok", "command_status");
            profiler_clear_context();
            return;
        }
        if (strcmp(msg->text, "/restart") == 0) {
            if (time(NULL) - loop->start_time < 60) {
                send_telegram(loop, msg->chat_id, el_restart_cooldown_text());
                profiler_emit_event("request.total",
                                    profiler_timer_elapsed_ms(&total_timer),
                                    "ok", "command_restart_cooldown");
                profiler_clear_context();
                return;
            }
            send_telegram(loop, msg->chat_id, el_restart_text());
            profiler_emit_event("request.total",
                                profiler_timer_elapsed_ms(&total_timer),
                                "ok", "command_restart");
            profiler_clear_context();
            event_loop_stop(loop);
            return;
        }
        if (strcmp(msg->text, "/reload") == 0) {
            loop->reload_requested = 1;
            int rc = handle_reload(loop);
            send_telegram(loop, msg->chat_id,
                          rc == RELAY_OK ? el_reload_ok_text()
                                         : el_reload_error_text());
            profiler_emit_event("request.total",
                                profiler_timer_elapsed_ms(&total_timer),
                                rc == RELAY_OK ? "ok" : "error", "command_reload");
            profiler_clear_context();
            return;
        }
        if (strcmp(msg->text, "/start") == 0 ||
            strcmp(msg->text, "/help") == 0) {
            char help_msg[512];
            el_help_text(config_get(loop->deps.cfg, "agent_name", "Kai"),
                         help_msg, sizeof(help_msg));
            profiler_timer_t send_timer;
            profiler_timer_start(&send_timer);
            send_telegram(loop, msg->chat_id, help_msg);
            profiler_emit_event("telegram.send_response",
                                profiler_timer_elapsed_ms(&send_timer),
                                "ok", "command_help");
            profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                                "ok", "command_help");
            profiler_clear_context();
            return;
        }
        /* Unrecognized command — reply immediately instead of falling to LLM */
        {
            char unknown_msg[256];
            el_unknown_command_text(msg->text, unknown_msg, sizeof(unknown_msg));
            send_telegram(loop, msg->chat_id, unknown_msg);
            profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                                "ok", "command_unknown");
            profiler_clear_context();
            return;
        }
    }

    /* CHECK FOR INTERRUPTION */
    pthread_mutex_lock(&g_sessions_mutex);
    int was_interrupted = 0;
    if (active_sessions_has(g_active_sessions, msg->chat_id)) {
        active_sessions_interrupt(g_active_sessions, msg->chat_id);
        was_interrupted = 1;
    }
    pthread_mutex_unlock(&g_sessions_mutex);

    /* Notify user if we interrupted previous work */
    if (was_interrupted) {
        profiler_timer_t send_timer;
        profiler_timer_start(&send_timer);
        send_telegram(loop, msg->chat_id, "⚡️ Restarting");
        profiler_emit_event("telegram.send_response",
                            profiler_timer_elapsed_ms(&send_timer),
                            "ok", "interruption_notice");
    }

    /* Send typing indicator */
    profiler_timer_t typing_timer;
    profiler_timer_start(&typing_timer);
    send_typing(loop, msg->chat_id);
    profiler_emit_event("telegram.typing",
                        profiler_timer_elapsed_ms(&typing_timer),
                        "ok", "pre_llm");

    /* Send initial "thinking" message and capture message_id for editing */
    profiler_timer_t placeholder_timer;
    profiler_timer_start(&placeholder_timer);
    int placeholder_msg_id = send_telegram_with_id(loop, msg->chat_id, el_placeholder_text());
    profiler_emit_event("telegram.placeholder",
                        profiler_timer_elapsed_ms(&placeholder_timer),
                        placeholder_msg_id > 0 ? "ok" : "error",
                        "working_on_it");

    /* Prepare work for thread */
    claude_work_t *work = malloc(sizeof(claude_work_t));
    if (!work) {
        send_telegram(loop, msg->chat_id, "Error: Out of memory");
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "oom");
        profiler_clear_context();
        return;
    }

    work->loop = loop;
    memcpy(&work->msg, msg, sizeof(telegram_message_t));
    snprintf(work->chat_id, RELAY_MAX_USER_ID, "%s", msg->chat_id);
    snprintf(work->request_id, sizeof(work->request_id), "%s", request_id);
    work->placeholder_msg_id = placeholder_msg_id;

    /* Persist pending-response record so we can detect interrupted responses on restart */
    {
        const char *ws = config_get(loop->deps.cfg, "workspace_path", ".");
        pending_response_write(ws, msg->chat_id, msg->text);
    }

    /* Spawn worker thread with 4MB stack — response buffers are large (256KB each) */
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setstacksize(&tattr, 4 * 1024 * 1024);

    pthread_t thread;
    int create_rc = pthread_create(&thread, &tattr, claude_worker_thread, work);
    pthread_attr_destroy(&tattr);

    if (create_rc != 0) {
        send_telegram(loop, msg->chat_id, "Error: Could not spawn worker");
        free(work);
        profiler_emit_event("request.total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "worker_spawn_failed");
        profiler_clear_context();
        return;
    }

    /* Detach thread so it cleans up automatically */
    pthread_detach(thread);
    profiler_emit_event("worker.spawn", profiler_timer_elapsed_ms(&total_timer),
                        "ok", "claude_worker");
    profiler_clear_context();

    /* Event loop continues immediately, can receive new messages */
}


static void *memory_flush_worker_thread(void *arg)
{
    memory_flush_work_t *work = (memory_flush_work_t *)arg;

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    llm_provider_send(work->loop->deps.llm, work->flush_prompt,
                      work->session_id, &resp);

    log_write(work->loop->deps.log, LOG_INFO, "Memory flush completed");

    free(work);
    return NULL;
}

static void handle_memory_flush(event_loop_t *loop)
{
    if (loop->memory_flushed || loop->last_message_time == 0) {
        return;
    }

    time_t now = time(NULL);
    if (now - loop->last_message_time < loop->flush_idle_sec) {
        return;
    }

    /* Find the most recently active chat session */
    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, loop->authorized_user, session_key, sizeof(session_key));
    const char *session_id = session_get(loop->deps.sessions, session_key);
    if (!session_id) {
        loop->memory_flushed = 1;
        return;
    }

    /* Mark flushed immediately to prevent re-triggering while in flight */
    loop->memory_flushed = 1;

    log_write(loop->deps.log, LOG_INFO, "Auto-memory flush triggered");

    /* Get current date for memory file path */
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    /* Prepare async work */
    memory_flush_work_t *work = malloc(sizeof(memory_flush_work_t));
    if (!work) {
        log_write(loop->deps.log, LOG_ERROR, "Memory flush: out of memory");
        return;
    }

    work->loop = loop;
    snprintf(work->session_id, RELAY_MAX_SESSION_ID, "%s", session_id);

    /* Load compaction prompt from file (or fallback), substitute {date} */
    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    const char *workspace = config_get(loop->deps.cfg, "workspace_path", ".");
    llm_load_compaction_prompt(work->flush_prompt, sizeof(work->flush_prompt),
                               workspace, date_str);

    /* Spawn worker thread so the event loop is never blocked */
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setstacksize(&tattr, 4 * 1024 * 1024);

    pthread_t thread;
    if (pthread_create(&thread, &tattr, memory_flush_worker_thread, work) == 0) {
        pthread_detach(thread);
    } else {
        log_write(loop->deps.log, LOG_ERROR, "Memory flush: failed to spawn worker");
        free(work);
    }
    pthread_attr_destroy(&tattr);
}

static void handle_context_memory_flush(event_loop_t *loop, const char *chat_id)
{
    const size_t FLUSH_THRESHOLD = loop->context_window - loop->compaction_reserve - loop->compaction_soft_threshold;

    /* Check if flush needed (176K tokens) */
    char session_key[RELAY_MAX_USER_ID + 32];
    get_session_key(loop, chat_id, session_key, sizeof(session_key));
    if (!session_needs_memory_flush(loop->deps.sessions, session_key, FLUSH_THRESHOLD)) {
        return;
    }

    log_write(loop->deps.log, LOG_INFO,
              "[Session %s] Context memory flush triggered at threshold", chat_id);

    /* Get session ID */
    const char *session_id = session_get(loop->deps.sessions, session_key);
    if (!session_id) {
        return;
    }

    /* Build flush prompt with NO_REPLY instruction */
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);

    char flush_prompt[RELAY_MAX_MSG];
    snprintf(flush_prompt, sizeof(flush_prompt),
        "CRITICAL: Your context window is approaching the limit (%zu tokens). "
        "Write important context to data/memory/%04d-%02d-%02d.md NOW before "
        "auto-compaction occurs. Include: active tasks, key decisions, file "
        "locations, important outcomes. Keep it factual and brief. "
        "Prioritise: decisions made, files changed, errors fixed, open tasks. "
        "Skip redundant discussion — if a topic was re-explained or repeated, "
        "keep only the final summary or conclusion, not the iterations. "
        "Reply with ONLY 'NO_REPLY' to suppress this message to user.",
        session_get_context_tokens(loop->deps.sessions, session_key),
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    /* Send flush request */
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int result = llm_provider_send(loop->deps.llm, flush_prompt, session_id, &resp);

    if (result == RELAY_OK) {
        /* Mark as flushed */
        session_mark_memory_flushed(loop->deps.sessions, session_key);

        /* Check if response starts with NO_REPLY */
        if (strncmp(resp.result, "NO_REPLY", 8) == 0) {
            log_write(loop->deps.log, LOG_INFO,
                      "[Session %s] Context memory flush completed (suppressed)", chat_id);
            /* Don't send to Telegram - silent flush */
        } else {
            log_write(loop->deps.log, LOG_WARN,
                      "[Session %s] Context memory flush response not suppressed", chat_id);
            /* Send response anyway (fallback) */
            send_telegram(loop, chat_id, resp.result);
        }

        /* Track tokens for flush exchange */
        track_response_tokens(loop, session_key, flush_prompt, resp.result);

        /* Post-compaction: trigger curation pipeline */
        if (loop->deps.curator) {
            const char *agent_home = config_get(loop->deps.cfg,
                                                "workspace_path", ".");
            memory_curator_flush(loop->deps.curator, agent_home);
        }
    } else {
        log_write(loop->deps.log, LOG_ERROR,
                  "[Session %s] Context memory flush failed", chat_id);
    }
}

static int handle_reload(event_loop_t *loop)
{
    if (!loop->reload_requested) {
        return RELAY_OK;
    }
    loop->reload_requested = 0;

    if (loop->config_path[0] == '\0') {
        log_write(loop->deps.log, LOG_ERROR, "Reload: no config path set");
        return RELAY_ERR;
    }

    log_write(loop->deps.log, LOG_INFO, "Reloading config from %s",
              loop->config_path);

    config_t *new_cfg = config_load(loop->config_path);
    if (!new_cfg) {
        log_write(loop->deps.log, LOG_ERROR,
                  "Reload failed: could not load %s", loop->config_path);
        return RELAY_ERR;
    }

    /* Validate required keys */
    const char *provider = config_get(new_cfg, "llm_provider", "claude");
    const char *required[6];
    int required_count = el_reload_collect_required(provider, required, 6);

    char errors[6][RELAY_MAX_VALUE];
    int missing = config_validate(new_cfg, required, required_count, errors, 6);
    if (missing > 0) {
        log_write(loop->deps.log, LOG_ERROR,
                  "Reload failed: %d missing key(s), first: %s",
                  missing, errors[0]);
        config_free(new_cfg);
        return RELAY_ERR;
    }

    char validation_errors[8][RELAY_MAX_VALUE];
    int validation_count = config_validate_options(new_cfg,
                                                   validation_errors, 8);
    if (validation_count > 0) {
        for (int i = 0; i < validation_count; i++) {
            log_write(loop->deps.log, LOG_ERROR,
                      "Reload validation error: %s", validation_errors[i]);
        }
        config_free(new_cfg);
        return RELAY_ERR;
    }

    /* Update mutable values */
    llm_provider_update_config(loop->deps.llm, new_cfg);
    loop->flush_idle_sec = config_get_int(new_cfg,
                                           "memory_flush_idle_sec", 600);
    loop->context_window = config_get_int(new_cfg, "context_window", 200000);
    loop->compaction_reserve = config_get_int(new_cfg, "compaction_reserve_tokens", 20000);
    loop->compaction_soft_threshold = config_get_int(new_cfg,
                                        "compaction_memory_flush_soft_threshold", 4000);

    /* Update timezone if changed */
    const char *tz = config_get(new_cfg, "timezone", NULL);
    if (tz) {
        setenv("TZ", tz, 1);
        tzset();
    }

    /* Swap config in deps */
    config_free(loop->deps.cfg);
    loop->deps.cfg = new_cfg;

    /* Flush sessions on reload to ensure state is persisted */
    session_flush(loop->deps.sessions);

    log_write(loop->deps.log, LOG_INFO, "Config reloaded successfully");
    return RELAY_OK;
}

/* ── Polling loop (simplified — no kqueue for initial version) ──────── */

static void poll_telegram(event_loop_t *loop)
{
    profiler_set_context("system-telegram-poll", "", "system");
    profiler_timer_t total_timer;
    profiler_timer_start(&total_timer);

    char url[RELAY_MAX_URL];
    telegram_api_url(loop->deps.telegram, "getUpdates", url, sizeof(url));

    /* Build URL with offset and long-poll timeout.
     * message_reaction requires explicit opt-in since Bot API 7.0. */
    char full_url[RELAY_MAX_URL];
    if (loop->telegram_offset > 0) {
        snprintf(full_url, sizeof(full_url),
                 "%s?timeout=30&offset=%lld"
                 "&allowed_updates=[\"message\",\"message_reaction\"]",
                 url, loop->telegram_offset);
    } else {
        snprintf(full_url, sizeof(full_url),
                 "%s?timeout=30"
                 "&allowed_updates=[\"message\",\"message_reaction\"]",
                 url);
    }

    char resp[RELAY_MAX_RESPONSE];
    profiler_timer_t http_timer;
    profiler_timer_start(&http_timer);
    int rc = loop->deps.http->get(full_url, resp, sizeof(resp));
    profiler_emit_event("telegram.poll_http", profiler_timer_elapsed_ms(&http_timer),
                        rc == RELAY_OK ? "ok" : "error", "getUpdates");

    if (rc != RELAY_OK) {
        if (el_poll_abort_is_shutdown(loop->running)) {
            /* Intentional abort — curl cancelled because shutdown was requested */
            log_write(loop->deps.log, LOG_INFO,
                      "[telegram] poll cancelled for shutdown");
            profiler_emit_event("telegram.poll_total",
                                profiler_timer_elapsed_ms(&total_timer),
                                "ok", "shutdown");
            profiler_clear_context();
            return;
        }
        health_failure(loop->deps.health, HEALTH_TELEGRAM);
        int backoff = health_backoff(loop->deps.health, HEALTH_TELEGRAM);
        const char *fail_tag = el_poll_failure_tag(resp);
        log_write(loop->deps.log, LOG_WARN,
                  "[telegram] poll failed (type=%s, rc=%d, detail=%.200s), backing off %ds",
                  fail_tag, rc, resp, backoff);
        profiler_emit_event("telegram.poll_total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "poll_failed");
        profiler_clear_context();
        sleep((unsigned int)backoff);
        return;
    }

    health_success(loop->deps.health, HEALTH_TELEGRAM);

    /* Parse updates array */
    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        log_write(loop->deps.log, LOG_WARN,
                  "[telegram] poll response parse failed: %.200s", resp);
        profiler_emit_event("telegram.poll_total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "parse_failed");
        profiler_clear_context();
        return;
    }

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *error_code = cJSON_GetObjectItem(root, "error_code");
        cJSON *description = cJSON_GetObjectItem(root, "description");
        int code = cJSON_IsNumber(error_code) ? (int)error_code->valuedouble : 0;
        const char *desc = cJSON_IsString(description) ? description->valuestring : "(no description)";
        health_failure(loop->deps.health, HEALTH_TELEGRAM);
        int backoff = health_backoff(loop->deps.health, HEALTH_TELEGRAM);
        log_write(loop->deps.log, LOG_WARN,
                  "[telegram] API error (code=%d, desc=%.200s), backing off %ds",
                  code, desc, backoff);
        cJSON_Delete(root);
        profiler_emit_event("telegram.poll_total", profiler_timer_elapsed_ms(&total_timer),
                            "error", "api_error");
        profiler_clear_context();
        sleep((unsigned int)backoff);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        profiler_emit_event("telegram.poll_total", profiler_timer_elapsed_ms(&total_timer),
                            "ok", "no_results");
        profiler_clear_context();
        return;
    }

    profiler_timer_t updates_timer;
    profiler_timer_start(&updates_timer);
    int updates_count = cJSON_GetArraySize(result);
    cJSON *update;
    cJSON_ArrayForEach(update, result) {
        /* Track update_id for offset — next poll starts after this */
        cJSON *uid = cJSON_GetObjectItem(update, "update_id");
        if (cJSON_IsNumber(uid)) {
            long long id = (long long)uid->valuedouble;
            if (id >= loop->telegram_offset) {
                loop->telegram_offset = id + 1;
            }
        }

        /* Convert update back to string for parsing */
        char *update_str = cJSON_PrintUnformatted(update);
        if (update_str) {
            telegram_message_t msg;
            memset(&msg, 0, sizeof(msg));

            if (telegram_parse_update(update_str, &msg) == RELAY_OK) {
                handle_message(loop, &msg);
            } else {
                telegram_reaction_t reaction;
                memset(&reaction, 0, sizeof(reaction));
                if (telegram_parse_reaction(update_str, &reaction) == RELAY_OK) {
                    handle_reaction(loop, &reaction);
                }
            }
            free(update_str);
        }
    }
    char detail[64];
    snprintf(detail, sizeof(detail), "updates=%d", updates_count);
    profiler_emit_event("telegram.handle_updates",
                        profiler_timer_elapsed_ms(&updates_timer),
                        "ok", detail);

    /* Persist offset after each poll so restarts don't reprocess old messages */
    if (updates_count > 0) {
        const char *ws = config_get(loop->deps.cfg, "workspace_path", ".");
        telegram_offset_save(ws, loop->telegram_offset);
    }

    cJSON_Delete(root);
    profiler_emit_event("telegram.poll_total", profiler_timer_elapsed_ms(&total_timer),
                        "ok", "complete");
    profiler_clear_context();
}

/* ── Real fs helpers for poll_parent_alert ───────────────────────────── */

static char *el_real_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int el_real_file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static int el_real_delete_file(const char *path) {
    return (unlink(path) == 0) ? RELAY_OK : RELAY_ERR_IO;
}

/* ── Public API ─────────────────────────────────────────────────────── */

event_loop_t *event_loop_create(event_loop_deps_t *deps)
{
    if (!deps) {
        return NULL;
    }

    event_loop_t *loop = calloc(1, sizeof(event_loop_t));
    if (!loop) {
        return NULL;
    }

    /* Initialize interruption support */
    if (!g_active_sessions) {
        g_active_sessions = active_sessions_create();
    }

    loop->deps = *deps;
    loop->running = 1;
    loop->start_time = time(NULL);
    loop->flush_idle_sec = config_get_int(deps->cfg,
                                           "memory_flush_idle_sec", 600);
    loop->context_window = config_get_int(deps->cfg, "context_window", 200000);
    loop->compaction_reserve = config_get_int(deps->cfg, "compaction_reserve_tokens", 20000);
    loop->compaction_soft_threshold = config_get_int(deps->cfg,
                                        "compaction_memory_flush_soft_threshold", 4000);
    snprintf(loop->authorized_user, RELAY_MAX_USER_ID, "%s",
             config_get(deps->cfg, "telegram_user_id", ""));
    snprintf(loop->parent_alert_path, RELAY_MAX_PATH,
             "%s/data/state/parent-alert.json",
             config_get(deps->cfg, "workspace_path", "/tmp"));
    if (deps->config_path) {
        snprintf(loop->config_path, RELAY_MAX_PATH, "%s", deps->config_path);
    }

    {
        const char *env = getenv("RELAY_PROFILING");
        int profiling_enabled = 0;
        if (env && env[0] != '\0') {
            profiling_enabled = (strcmp(env, "1") == 0 ||
                                 strcmp(env, "true") == 0 ||
                                 strcmp(env, "on") == 0 ||
                                 strcmp(env, "yes") == 0);
        }
        /* Also allow enabling via relay.conf (survives daemonization) */
        if (!profiling_enabled) {
            profiling_enabled = config_get_int(deps->cfg, "profiling_enabled", 0);
        }

        char profiling_path[RELAY_MAX_PATH];
        const char *cfg_path = config_get(deps->cfg, "profiling_log_path", "");
        if (cfg_path[0] != '\0') {
            snprintf(profiling_path, sizeof(profiling_path), "%s", cfg_path);
        } else {
            snprintf(profiling_path, sizeof(profiling_path),
                     "%s/data/state/perf/profile.jsonl",
                     config_get(deps->cfg, "workspace_path", "/tmp"));
        }

        int prc = profiler_init(profiling_path, profiling_enabled);
        if (profiling_enabled && prc == RELAY_OK) {
            log_write(deps->log, LOG_INFO,
                      "[profiler] enabled: %s", profiling_path);
        } else if (profiling_enabled) {
            log_write(deps->log, LOG_WARN,
                      "[profiler] failed to initialize (rc=%d): %s",
                      prc, profiling_path);
        }
    }

    /* Initialize agent bus state */
    loop->agent_bus_enabled = (agent_bus_get_fd() >= 0) ? 1 : 0;
    if (loop->agent_bus_enabled) {
        const char *ws = config_get(deps->cfg, "workspace_path", ".");
        snprintf(loop->agent_bus_log_dir, sizeof(loop->agent_bus_log_dir),
                 "%s/data/transcripts", ws);
    }

    /* Crash recovery: if a pending-response record exists, the daemon was
     * killed while a worker was mid-response.  Log the incident and store
     * the chat_id so event_loop_run can notify the user once Telegram polling
     * starts. */
    {
        const char *ws = config_get(deps->cfg, "workspace_path", ".");
        char pr_chat_id[RELAY_MAX_USER_ID] = "";
        char pr_text[RELAY_MAX_PATH]       = "";
        if (pending_response_load(ws, pr_chat_id, sizeof(pr_chat_id),
                                  pr_text, sizeof(pr_text))) {
            log_write(deps->log, LOG_WARN,
                      "Crash recovery: incomplete response to %s detected (text=%.60s...)",
                      pr_chat_id, pr_text);
            snprintf(loop->pending_recovery_chat_id,
                     sizeof(loop->pending_recovery_chat_id), "%s", pr_chat_id);
            /* Read recent transcript for context recovery */
            if (deps->transcript) {
                transcript_read_recent(deps->transcript, pr_chat_id, 10,
                                       loop->recovery_context,
                                       sizeof(loop->recovery_context));
                if (loop->recovery_context[0] != '\0') {
                    log_write(deps->log, LOG_INFO,
                              "Crash recovery: loaded recent transcript context (%zu bytes)",
                              strlen(loop->recovery_context));
                }
            }
            /* Delete now so we don't re-trigger on subsequent restarts */
            pending_response_delete(ws);
        }
    }

    /* Restore Telegram offset so we don't reprocess already-handled messages
     * (e.g. /restart) after a watchdog restart. */
    {
        const char *ws = config_get(deps->cfg, "workspace_path", ".");
        loop->telegram_offset = telegram_offset_load(ws);
        if (loop->telegram_offset > 0) {
            log_write(deps->log, LOG_INFO,
                      "[telegram] restored offset %lld from disk",
                      loop->telegram_offset);
        }
    }

    /* Start memory sidecar (probe existing or spawn new). */
    if (deps->sidecar) {
        memory_sidecar_startup(deps->sidecar, deps->log);
    }

    return loop;
}

/* ── Agent Bus polling ──────────────────────────────────────────────────
 *
 * Called once per event loop iteration (before poll_telegram).
 * Non-blocking — returns immediately when no message is pending.
 * When a message arrives: log it, run LLM, log + send reply.
 * ─────────────────────────────────────────────────────────────────────── */

static void poll_agent_bus(event_loop_t *loop)
{
    if (!loop->agent_bus_enabled) return;

    agent_bus_message_t msg;
    int rc = agent_bus_accept_message(&msg);
    if (rc == RELAY_ERR_NOTFOUND) return;  /* Nothing pending — fast path */
    if (rc != RELAY_OK) {
        log_write(loop->deps.log, LOG_WARN,
                  "Agent bus: accept/parse error (rc=%d)", rc);
        return;
    }

    int max_depth = config_get_int(loop->deps.cfg, "agent_bus_max_depth", 3);

    log_write(loop->deps.log, LOG_INFO,
              "Agent bus: from=%s depth=%d autonomous=%d text=%.80s",
              msg.from, msg.depth, msg.is_autonomous, msg.text);

    /* Log the inbound message */
    agent_bus_log(loop->agent_bus_log_dir, "in", &msg, NULL);

    /* Circuit breaker: drop at depth limit so conversations don't recurse */
    if (msg.depth >= max_depth) {
        log_write(loop->deps.log, LOG_INFO,
                  "Agent bus: depth limit (%d) reached — dropping", max_depth);
        return;
    }

    /* Selective response: if addressed_to is set and doesn't include us, observe silently.
     * Comparison is case-insensitive so "Kai" matches "kai" (registry vs agent_name). */
    const char *my_name = config_get(loop->deps.cfg, "agent_name", "");
    if (msg.addressed_to[0] != '\0') {
        /* Lowercase both strings for comparison */
        char lc_addr[64], lc_name[64];
        size_t i;
        for (i = 0; i < sizeof(lc_addr) - 1 && msg.addressed_to[i]; i++)
            lc_addr[i] = (char)tolower((unsigned char)msg.addressed_to[i]);
        lc_addr[i] = '\0';
        for (i = 0; i < sizeof(lc_name) - 1 && my_name[i]; i++)
            lc_name[i] = (char)tolower((unsigned char)my_name[i]);
        lc_name[i] = '\0';

        if (strcmp(lc_addr, "all") != 0
            && strcmp(lc_addr, "team") != 0
            && strstr(lc_addr, lc_name) == NULL) {
            log_write(loop->deps.log, LOG_INFO,
                      "Agent bus: not addressed to me (addressed_to=%s) — observing silently",
                      msg.addressed_to);
            agent_bus_log(loop->agent_bus_log_dir, "observe", &msg, NULL);
            return;
        }
    }

    /* Build LLM prompt with group-chat context */
    char prompt[4096 + 256];
    if (msg.participants[0] != '\0') {
        snprintf(prompt, sizeof(prompt),
                 "[Group chat — participants: %s — from %s]: %s\n\nRespond concisely.",
                 msg.participants, msg.from, msg.text);
    } else {
        snprintf(prompt, sizeof(prompt),
                 "[Group chat — from %s]: %s\n\nRespond concisely.",
                 msg.from, msg.text);
    }

    /* Prepend any messages this agent missed due to previous LLM errors */
    const char *ws = config_get(loop->deps.cfg, "workspace_path", ".");
    char pending_prefix[8192] = "";
    int has_pending = pending_bus_load(ws, pending_prefix, sizeof(pending_prefix));

    char full_bus_prompt[12288];
    if (has_pending) {
        snprintf(full_bus_prompt, sizeof(full_bus_prompt),
                 "%s[Current message]:\n%s", pending_prefix, prompt);
    } else {
        snprintf(full_bus_prompt, sizeof(full_bus_prompt), "%s", prompt);
    }

    /* Use a per-agent-pair session key so context is maintained */
    char session_key[128];
    snprintf(session_key, sizeof(session_key), "agent-bus:%s", msg.from);
    const char *session_id = session_get(loop->deps.sessions, session_key);

    /* Call LLM synchronously (same pattern as document/photo handlers) */
    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    rc = llm_provider_send_with_retry(loop->deps.llm, full_bus_prompt, session_id, &resp);

    if (rc != RELAY_OK || resp.is_error || resp.result[0] == '\0') {
        log_write(loop->deps.log, LOG_WARN,
                  "Agent bus: LLM error (rc=%d is_error=%d) — queuing for retry",
                  rc, resp.is_error);
        agent_bus_log(loop->agent_bus_log_dir, "err", &msg, "LLM error");
        pending_bus_save(ws, &msg);
        return;
    }

    /* Success — clear any pending queue that was just caught up on */
    if (has_pending) {
        pending_bus_clear(ws);
    }

    /* Update session context */
    if (resp.session_id[0] != '\0') {
        session_set(loop->deps.sessions, session_key, resp.session_id);
    }
    track_response_tokens(loop, session_key, full_bus_prompt, resp.result);

    /* Log the outbound response */
    agent_bus_log(loop->agent_bus_log_dir, "out", &msg, resp.result);

    /* Persist this exchange to today's memory file for semantic search */
    {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char mem_path[RELAY_MAX_PATH];
        const char *workspace = config_get(loop->deps.cfg, "workspace_path", ".");
        snprintf(mem_path, sizeof(mem_path), "%s/data/memory/%04d-%02d-%02d.md",
                 workspace, tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
        FILE *memf = fopen(mem_path, "a");
        if (memf) {
            char ts_buf[16];
            strftime(ts_buf, sizeof(ts_buf), "%H:%M", &tm_now);
            fprintf(memf, "\n## Agent Chat — %s\n**From %s:** %s\n**Response:** %s\n",
                    ts_buf, msg.from, msg.text, resp.result);
            fclose(memf);
        }
    }

    /* Send reply back to sender's socket */
    if (msg.from_socket[0] != '\0') {
        int send_rc = agent_bus_send(msg.from_socket, my_name, NULL,
                                     resp.result, msg.depth + 1, 0, msg.from);
        if (send_rc != RELAY_OK) {
            log_write(loop->deps.log, LOG_WARN,
                      "Agent bus: failed to send reply to %s", msg.from_socket);
        } else {
            log_write(loop->deps.log, LOG_INFO,
                      "Agent bus: replied to %s at depth %d", msg.from, msg.depth + 1);
        }
    }
}

/* Poll for a parent alert written by Claude during a conversation.
 * Claude writes data/state/parent-alert.json:
 *   {"message": "..."} — message to send to the parent user
 * This function reads the file, sends the alert, and deletes it.
 * No-op if no parent is configured or file does not exist. */
static void poll_parent_alert(event_loop_t *loop)
{
    const char *parent_user = telegram_get_parent_user(loop->deps.telegram);
    if (!parent_user || parent_user[0] == '\0') {
        return;  /* No parent configured */
    }

    if (!el_real_file_exists(loop->parent_alert_path)) {
        return;  /* Nothing to send */
    }

    char *content = el_real_read_file(loop->parent_alert_path);
    el_real_delete_file(loop->parent_alert_path);

    if (!content) {
        return;
    }

    /* Parse JSON: {"message": "..."} */
    const char *alert_text = NULL;
    cJSON *json = cJSON_Parse(content);
    if (json) {
        cJSON *msg = cJSON_GetObjectItem(json, "message");
        if (cJSON_IsString(msg) && msg->valuestring && msg->valuestring[0] != '\0') {
            alert_text = msg->valuestring;
            log_write(loop->deps.log, LOG_INFO,
                      "[parent-alert] sending to parent: %.100s", alert_text);
            send_telegram(loop, parent_user, alert_text);
        }
        cJSON_Delete(json);
    } else {
        /* Fallback: treat raw content as the message */
        log_write(loop->deps.log, LOG_WARN,
                  "[parent-alert] file not valid JSON, sending raw content");
        send_telegram(loop, parent_user, content);
    }

    free(content);
}

int event_loop_run(event_loop_t *loop)
{
    if (!loop) {
        return RELAY_ERR;
    }

    log_write(loop->deps.log, LOG_INFO,
              "relay " RELAY_VERSION " starting event loop");

    /* Crash recovery: if we found an interrupted response at startup, notify
     * the user once the event loop begins (Telegram polling is now live).
     * recovery_context is kept alive for injection into the first LLM prompt. */
    if (loop->pending_recovery_chat_id[0] != '\0') {
        char recovery_msg[512];
        el_recovery_text(loop->recovery_context[0] != '\0' ? 1 : 0,
                         recovery_msg, sizeof(recovery_msg));
        send_telegram(loop, loop->pending_recovery_chat_id, recovery_msg);
        loop->pending_recovery_chat_id[0] = '\0';
    }

    /* Startup notification — lets the user know the daemon is live and
     * accepting commands. Skipped silently if no authorized user is set. */
    if (loop->authorized_user[0] != '\0') {
        send_telegram(loop, loop->authorized_user, el_startup_ready_text());
    }

    while (loop->running) {
        /* Check for config reload */
        handle_reload(loop);

        /* Check agent bus (non-blocking — returns immediately if nothing pending) */
        poll_agent_bus(loop);

        /* Poll Telegram */
        poll_telegram(loop);

        poll_parent_alert(loop);        /* Non-blocking: sends alert to parent if file present */

        /* Check auto-memory flush */
        handle_memory_flush(loop);

        /* Memory sidecar: health watch + ingest timers */
        if (loop->deps.sidecar) {
            time_t now = time(NULL);
            const char *agent_home = config_get(loop->deps.cfg,
                                                "workspace_path", ".");
            memory_sidecar_watch(loop->deps.sidecar, now, loop->deps.log);
            memory_sidecar_maybe_ingest_logs(loop->deps.sidecar,
                                             now, agent_home);
            memory_sidecar_maybe_ingest_transcripts(loop->deps.sidecar,
                                                    now, agent_home);
        }

        /* Flush dirty sessions (non-blocking if clean) */
        session_flush(loop->deps.sessions);
    }

    log_write(loop->deps.log, LOG_INFO, "Event loop stopped");
    return RELAY_OK;
}

void event_loop_stop(event_loop_t *loop)
{
    if (loop) {
        loop->running = 0;
    }
}

void event_loop_request_reload(event_loop_t *loop)
{
    if (loop) {
        loop->reload_requested = 1;
    }
}

void event_loop_free(event_loop_t *loop)
{
    if (loop) {
        if (loop->deps.sidecar) {
            memory_sidecar_stop(loop->deps.sidecar);
        }
        profiler_close();
        config_free(loop->deps.cfg);
        loop->deps.cfg = NULL;
    }
    free(loop);
}
