#include "claude.h"
#include "llm_prompt.h"
#include "peer_registry.h"

#include <cJSON/cJSON.h>

/* Provided by main.c (production) or the test stub (tests).
 * Writes workspace path into thread-local storage so proc_spawn can chdir()
 * to it in the child process without touching the relay_proc_t interface. */
extern void proc_set_current_workspace_path(const char *path);
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Internal types ─────────────────────────────────────────────────── */

struct claude {
    relay_proc_t *proc;
    char binary[RELAY_MAX_PATH];
    char workspace[RELAY_MAX_PATH];
    char agent_name[RELAY_MAX_VALUE];
    char user_name[RELAY_MAX_VALUE];
    char allowed_tools[RELAY_MAX_VALUE]; /* --allowedTools value (empty = unrestricted) */
    int timeout;
    int retry_count;            /* max number of retries */
    int retry_backoff_ms;       /* initial backoff in milliseconds */
    _Atomic int in_flight;      /* number of active LLM calls (thread-safe) */
    claude_retry_notify_cb retry_notify_cb;
    void *retry_notify_userdata;
};

/* ── Internal helpers ───────────────────────────────────────────────── */

/* Build the standard system prompt.
 * Injected as --system-prompt on every Claude CLI invocation.
 * Points Claude to identity files and provides current time context.
 * Buffer must be at least 1024 bytes. */
void claude_build_system_prompt(char *buf, size_t max,
                                const char *agent_name, const char *user_name,
                                const char *workspace_name,
                                const char *workspace_path)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    static const char *days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };

    /* Build workspace context line */
    char ws_line[256] = "";
    int has_name = workspace_name && workspace_name[0] != '\0';
    int has_path = workspace_path && workspace_path[0] != '\0';
    if (has_name && has_path) {
        snprintf(ws_line, sizeof(ws_line),
                 "\nCurrent workspace: %s (%s)", workspace_name, workspace_path);
    } else if (has_path) {
        snprintf(ws_line, sizeof(ws_line),
                 "\nCurrent workspace: %s", workspace_path);
    }

    const char *tz_label = tm.tm_zone ? tm.tm_zone : "UTC";

    snprintf(buf, max,
        "You are %s, %s's AI familiar. "
        "Your identity context (SOUL.md, IDENTITY.md, USER.md, PRIORITIES.md, SKILLS.md) "
        "and relevant memories are pre-injected at the start of each message — "
        "no need to read them again. "
        "You are NOT a generic assistant — you are %s.\n"
        "Current time: %04d-%02d-%02d %02d:%02d %s (%s)%s",
        agent_name, user_name,
        agent_name,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tz_label, days[tm.tm_wday],
        ws_line);

    /* Append peer agent context if available */
    size_t used = strlen(buf);
    if (used + 1 < max) {
        peer_registry_build_context(buf + used, max - used);
    }
}

/* Populate resp with a human-readable error when the process spawn fails.
 * Ensures operators always get something useful in the log detail field. */
static void set_spawn_error(claude_response_t *resp, int rc)
{
    resp->is_error = 1;
    switch (rc) {
        case RELAY_ERR_TIMEOUT:
            snprintf(resp->result, sizeof(resp->result),
                     "Claude process timed out (rc=%d)", rc);
            break;
        case RELAY_ERR_NOMEM:
            snprintf(resp->result, sizeof(resp->result),
                     "Out of memory spawning Claude (rc=%d)", rc);
            break;
        case RELAY_ERR_IO:
            snprintf(resp->result, sizeof(resp->result),
                     "I/O error spawning Claude (rc=%d)", rc);
            break;
        case RELAY_ERR_SIGNAL:
            snprintf(resp->result, sizeof(resp->result),
                     "Claude process interrupted by signal (rc=%d)", rc);
            break;
        default:
            snprintf(resp->result, sizeof(resp->result),
                     "Claude process failed (rc=%d)", rc);
            break;
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

claude_t *claude_create(relay_proc_t *proc, const config_t *cfg)
{
    if (!proc || !cfg) {
        return NULL;
    }

    claude_t *cl = calloc(1, sizeof(claude_t));
    if (!cl) {
        return NULL;
    }

    cl->proc = proc;
    snprintf(cl->binary, RELAY_MAX_PATH, "%s",
             config_get(cfg, "claude_binary", "claude"));
    snprintf(cl->workspace, RELAY_MAX_PATH, "%s",
             config_get(cfg, "workspace_path", "."));
    snprintf(cl->agent_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "agent_name", "Kai"));
    snprintf(cl->user_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "user_name", "User"));
    snprintf(cl->allowed_tools, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "allowed_tools", ""));
    cl->timeout = config_get_int(cfg, "claude_timeout", 600);
    cl->retry_count = config_get_int(cfg, "claude_retry_count", 3);
    cl->retry_backoff_ms = config_get_int(cfg, "claude_retry_backoff_ms", 1000);

    return cl;
}

void claude_set_retry_notify(claude_t *cl, claude_retry_notify_cb cb,
                              void *userdata)
{
    if (!cl) {
        return;
    }
    cl->retry_notify_cb = cb;
    cl->retry_notify_userdata = userdata;
}

int claude_parse_response(const char *json, claude_response_t *resp)
{
    if (!json || !resp) {
        return RELAY_ERR_PARSE;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    /* Extract session_id */
    cJSON *sid = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(sid)) {
        snprintf(resp->session_id, RELAY_MAX_SESSION_ID,
                 "%s", sid->valuestring);
    } else {
        resp->session_id[0] = '\0';
    }

    /* Extract result */
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsString(result)) {
        snprintf(resp->result, RELAY_MAX_RESPONSE,
                 "%s", result->valuestring);
    } else {
        resp->result[0] = '\0';
    }

    /* Extract duration_ms */
    cJSON *dur = cJSON_GetObjectItem(root, "duration_ms");
    resp->duration_ms = cJSON_IsNumber(dur) ? (int)dur->valuedouble : 0;

    /* Check if error type — handle both non-streaming ("type":"error")
     * and streaming ("type":"result","subtype":"error") formats */
    cJSON *type = cJSON_GetObjectItem(root, "type");
    resp->is_error = (cJSON_IsString(type) &&
                      strcmp(type->valuestring, "error") == 0) ? 1 : 0;
    if (!resp->is_error) {
        cJSON *subtype = cJSON_GetObjectItem(root, "subtype");
        if (cJSON_IsString(subtype) &&
            strcmp(subtype->valuestring, "error") == 0) {
            resp->is_error = 1;
        }
    }

    cJSON_Delete(root);
    return RELAY_OK;
}

int claude_send(claude_t *cl, const char *message,
                const char *session_id, const char *workspace_path,
                claude_response_t *resp)
{
    if (!cl || !message || !resp) {
        return RELAY_ERR;
    }

    /* Reset workspace TLS before resolution — prevents stale path leaking
     * from a prior call on the same thread */
    proc_set_current_workspace_path("");

    /* Resolve effective workspace: caller-supplied path overrides cl->workspace.
     * Tilde prefix is expanded via $HOME. */
    char resolved_workspace[RELAY_MAX_PATH];
    const char *eff_wsp = (workspace_path && workspace_path[0] != '\0')
                          ? workspace_path : cl->workspace;
    if (eff_wsp[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(resolved_workspace, sizeof(resolved_workspace),
                     "%s%s", home, eff_wsp + 1);
            eff_wsp = resolved_workspace;
        }
    }

    /* Register resolved workspace path for the child process chdir() */
    proc_set_current_workspace_path(eff_wsp);

    /* Build system prompt — include resolved workspace path */
    char sys_prompt[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(sys_prompt, sizeof(sys_prompt),
                               cl->agent_name, cl->user_name, "", eff_wsp);

    /* Build args array */
    const char *args[24];
    int argc = 0;

    args[argc++] = cl->binary;
    args[argc++] = "-p";
    args[argc++] = "--output-format";
    args[argc++] = "json";
    args[argc++] = "--dangerously-skip-permissions";
    args[argc++] = "--add-dir";
    args[argc++] = eff_wsp;
    args[argc++] = "--system-prompt";
    args[argc++] = sys_prompt;

    if (cl->allowed_tools[0] != '\0') {
        args[argc++] = "--allowedTools";
        args[argc++] = cl->allowed_tools;
    }

    if (session_id && session_id[0] != '\0') {
        args[argc++] = "--resume";
        args[argc++] = session_id;
    }

    args[argc] = NULL;

    /* Spawn process — track in-flight count */
    atomic_fetch_add(&cl->in_flight, 1);
    char output[RELAY_MAX_RESPONSE];
    int rc = cl->proc->spawn(cl->binary, args, message,
                              output, sizeof(output), cl->timeout);
    atomic_fetch_sub(&cl->in_flight, 1);

    if (rc != RELAY_OK) {
        set_spawn_error(resp, rc);
        return rc;
    }

    /* Empty output means the process exited without producing a result —
     * transient crash, not a structural parse error. Return RELAY_ERR
     * (retriable) instead of letting claude_parse_response return
     * RELAY_ERR_PARSE (not retriable). */
    if (output[0] == '\0') {
        return RELAY_ERR;
    }

    /* Parse JSON response */
    return claude_parse_response(output, resp);
}

int claude_send_streaming(claude_t *cl, const char *message,
                           const char *session_id, const char *workspace_path,
                           claude_token_cb on_token, void *userdata,
                           claude_response_t *resp)
{
    if (!cl || !message || !resp) {
        return RELAY_ERR;
    }

    /* Fall back to blocking send if streaming not supported */
    if (!cl->proc->spawn_streaming) {
        return claude_send(cl, message, session_id, workspace_path, resp);
    }

    /* Reset workspace TLS before resolution — prevents stale path leaking */
    proc_set_current_workspace_path("");

    /* Resolve effective workspace (same logic as claude_send) */
    char resolved_workspace[RELAY_MAX_PATH];
    const char *eff_wsp = (workspace_path && workspace_path[0] != '\0')
                          ? workspace_path : cl->workspace;
    if (eff_wsp[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(resolved_workspace, sizeof(resolved_workspace),
                     "%s%s", home, eff_wsp + 1);
            eff_wsp = resolved_workspace;
        }
    }

    /* Register resolved workspace path for the child process chdir() */
    proc_set_current_workspace_path(eff_wsp);

    /* Build system prompt — include resolved workspace path */
    char sys_prompt[CLAUDE_SYSTEM_PROMPT_MAX];
    claude_build_system_prompt(sys_prompt, sizeof(sys_prompt),
                               cl->agent_name, cl->user_name, "", eff_wsp);

    /* Build args — stream-json with partial messages enabled */
    const char *args[28];
    int argc = 0;

    args[argc++] = cl->binary;
    args[argc++] = "-p";
    args[argc++] = "--output-format";
    args[argc++] = "stream-json";
    args[argc++] = "--verbose";
    args[argc++] = "--include-partial-messages";
    args[argc++] = "--dangerously-skip-permissions";
    args[argc++] = "--add-dir";
    args[argc++] = eff_wsp;
    args[argc++] = "--system-prompt";
    args[argc++] = sys_prompt;

    if (cl->allowed_tools[0] != '\0') {
        args[argc++] = "--allowedTools";
        args[argc++] = cl->allowed_tools;
    }

    if (session_id && session_id[0] != '\0') {
        args[argc++] = "--resume";
        args[argc++] = session_id;
    }

    args[argc] = NULL;

    /* Spawn streaming process — track in-flight count */
    atomic_fetch_add(&cl->in_flight, 1);
    char result_line[RELAY_MAX_RESPONSE];
    result_line[0] = '\0'; /* ensure NUL-terminated if proc writes nothing */
    int rc = cl->proc->spawn_streaming(cl->binary, args, message,
                                        on_token, userdata,
                                        result_line, sizeof(result_line),
                                        cl->timeout);
    atomic_fetch_sub(&cl->in_flight, 1);

    if (rc != RELAY_OK) {
        set_spawn_error(resp, rc);
        return rc;
    }

    /* Parse the result event line.
     * Empty output means the process exited without producing a result —
     * this is a transient crash, not a structural parse error, so return
     * RELAY_ERR (retriable) rather than RELAY_ERR_PARSE (not retriable). */
    if (result_line[0] == '\0') {
        return RELAY_ERR;
    }
    return claude_parse_response(result_line, resp);
}

/* Check if error code is retriable (transient) or permanent */
static int is_retriable_error(int rc)
{
    /* Retry on timeout and generic errors, but NOT on parse errors */
    return (rc == RELAY_ERR_TIMEOUT || rc == RELAY_ERR);
}

int claude_send_with_retry(claude_t *cl, const char *message,
                            const char *session_id, const char *workspace_path,
                            claude_response_t *resp)
{
    if (!cl || !message || !resp) {
        return RELAY_ERR;
    }

    int rc = RELAY_ERR;
    int attempt = 0;
    int backoff_ms = cl->retry_backoff_ms;

    while (attempt < cl->retry_count) {
        rc = claude_send(cl, message, session_id, workspace_path, resp);

        /* Success! */
        if (rc == RELAY_OK && !resp->is_error) {
            return RELAY_OK;
        }

        /* Permanent error (e.g., parse failure) — don't retry */
        if (!is_retriable_error(rc)) {
            return rc;
        }

        attempt++;

        /* Notify caller about the retry (for user-facing status updates) */
        if (attempt < cl->retry_count && cl->retry_notify_cb) {
            cl->retry_notify_cb(attempt + 1, cl->retry_count,
                                cl->retry_notify_userdata);
        }

        /* If we have more retries left, sleep with exponential backoff */
        if (attempt < cl->retry_count) {
            usleep(backoff_ms * 1000); /* usleep takes microseconds */
            backoff_ms *= 2; /* Exponential backoff */
        }
    }

    /* Exhausted all retries */
    return rc;
}

/* ── Token tracking wrapper for streaming retry ──────────────────────── */

typedef struct {
    claude_token_cb real_cb;
    void           *real_userdata;
    int             fired;   /* non-zero if callback was called at least once */
} retry_token_ctx_t;

static int retry_token_wrapper(const char *text, size_t len, void *userdata)
{
    retry_token_ctx_t *ctx = userdata;
    ctx->fired = 1;
    if (ctx->real_cb) {
        return ctx->real_cb(text, len, ctx->real_userdata);
    }
    return 0;
}

int claude_send_streaming_with_retry(claude_t *cl, const char *message,
                                      const char *session_id,
                                      const char *workspace_path,
                                      claude_token_cb on_token, void *userdata,
                                      claude_response_t *resp)
{
    if (!cl || !message || !resp) {
        return RELAY_ERR;
    }

    int rc = RELAY_ERR;
    int attempt = 0;
    int backoff_ms = cl->retry_backoff_ms;

    while (attempt < cl->retry_count) {
        retry_token_ctx_t ctx = { on_token, userdata, 0 };

        rc = claude_send_streaming(cl, message, session_id, workspace_path,
                                   retry_token_wrapper, &ctx, resp);

        /* Success! */
        if (rc == RELAY_OK && !resp->is_error) {
            return RELAY_OK;
        }

        /* Tokens were already delivered — do NOT retry to avoid duplicates.
         * Treat as success: the response was streamed to the user; missing
         * result JSON is a bookkeeping gap, not a user-facing failure.
         * This prevents crash recovery from re-sending an already-sent reply. */
        if (ctx.fired) {
            if (resp->result[0] == '\0') {
                snprintf(resp->result, RELAY_MAX_RESPONSE, "(streamed)");
            }
            resp->is_error = 0;
            return RELAY_OK;
        }

        /* Permanent error (e.g., parse failure) — don't retry */
        if (!is_retriable_error(rc)) {
            return rc;
        }

        attempt++;

        /* Notify caller about the retry (for user-facing status updates) */
        if (attempt < cl->retry_count && cl->retry_notify_cb) {
            cl->retry_notify_cb(attempt + 1, cl->retry_count,
                                cl->retry_notify_userdata);
        }

        /* Exponential backoff before next attempt */
        if (attempt < cl->retry_count) {
            usleep(backoff_ms * 1000);
            backoff_ms *= 2;
        }
    }

    /* Exhausted all retries */
    return rc;
}

void claude_update_config(claude_t *cl, const config_t *cfg)
{
    if (!cl || !cfg) {
        return;
    }
    cl->timeout = config_get_int(cfg, "claude_timeout", cl->timeout);
    cl->retry_count = config_get_int(cfg, "claude_retry_count", cl->retry_count);
    cl->retry_backoff_ms = config_get_int(cfg, "claude_retry_backoff_ms",
                                          cl->retry_backoff_ms);
}

int claude_in_flight(const claude_t *cl)
{
    if (!cl) {
        return 0;
    }
    return (int)atomic_load(&cl->in_flight);
}

void claude_free(claude_t *cl)
{
    free(cl);
}
