#include "el_reply.h"
#include "el_spinner.h"
#include "llm_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *el_pick_reply_text(int first_chunk_sent, const char *llm_result)
{
    if (!first_chunk_sent && (!llm_result || llm_result[0] == '\0')) {
        return "(no response)";
    }
    return llm_result;
}

const char *el_llm_error_text(void)
{
    return "Sorry, I couldn't process that. Please try again.";
}

const char *el_timeout_reply_text(int first_chunk_sent)
{
    if (first_chunk_sent) {
        return "\u23f1 Response cut short \u2014 Claude timed out. "
               "Try a shorter request.";
    }
    return "Claude timed out \u2014 no response generated. "
           "Try a shorter request.";
}

const char *el_reload_ok_text(void)
{
    return "Config reloaded.";
}

const char *el_reload_error_text(void)
{
    return "Reload failed. Check logs.";
}

const char *el_status_text(int sidecar_healthy)
{
    if (sidecar_healthy) {
        return "Daemon: running\nMemory sidecar: running";
    }
    return "Daemon: running\nMemory sidecar: stopped";
}

const char *el_restart_text(void)
{
    return "Restarting...";
}

const char *el_restart_cooldown_text(void)
{
    return "Just restarted \u2014 try again in a minute.";
}

void el_help_text(const char *agent_name, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size,
             "I'm %s. Send me a message and I'll respond "
             "with full access to your workspace.\n\n"
             "Commands:\n"
             "/help - Show this message\n"
             "/status - Show daemon and memory status\n"
             "/restart - Restart the daemon\n"
             "/reload - Reload config\n"
             "/clear - Start a new conversation\n"
             "/close - Close the active workspace\n"
             "/workspace - Show current workspace info\n"
             "/session - Switch workspace (/session <name> or list)",
             agent_name ? agent_name : "relay");
}

int el_poll_abort_is_shutdown(int loop_running)
{
    return !loop_running;
}

const char *el_poll_failure_tag(const char *detail)
{
    if (!detail || detail[0] == '\0') return "network";
    if (strstr(detail, "resolve host") || strstr(detail, "resolve proxy"))
        return "dns";
    if (strstr(detail, "Timeout"))
        return "timeout";
    if (strstr(detail, "aborted"))
        return "abort";
    return "network";
}

const char *el_photo_llm_workspace(const resolved_workspace_t *ws,
                                    char *fallback_buf, size_t buf_size)
{
    if (ws && !ws->is_error && ws->path[0] != '\0') {
        return ws->path;
    }
    /* No workspace configured — fall back to $HOME then /tmp */
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(fallback_buf, buf_size, "%s", home);
        return fallback_buf;
    }
    snprintf(fallback_buf, buf_size, "/tmp");
    return fallback_buf;
}

int el_reload_collect_required(const char *provider,
                                const char *required[], int max_count)
{
    int n = 0;
    if (n < max_count) required[n++] = "telegram_bot_token";
    if (n < max_count) required[n++] = "telegram_user_id";
    /* workspace_path is optional — it has fallbacks everywhere, so it must
     * not block a reload when the key is absent from the config file. */
    if (llm_is_openai_provider(provider)) {
        if (n < max_count) required[n++] = "openai_binary";
        if (n < max_count) required[n++] = "openai_model";
    } else {
        if (n < max_count) required[n++] = "claude_binary";
    }
    return n;
}

void el_health_alert_text(int failures, const char *last_error,
                          char *buf, size_t buf_size)
{
    const char *detail = (last_error && last_error[0] != '\0')
                             ? last_error
                             : "(unknown)";
    snprintf(buf, buf_size,
             "\xe2\x9a\xa0 LLM has failed %d consecutive times. "
             "Last error: %.200s. Check logs.",
             failures, detail);
}

void el_spawn_diag_text(char *buf, size_t buf_size)
{
    const char *claudecode = getenv("CLAUDECODE");
    const char *home = getenv("HOME");
    const char *path = getenv("PATH");

    /* Truncate PATH to 200 chars for readability */
    char path_trunc[204];
    if (path && strlen(path) > 200) {
        snprintf(path_trunc, sizeof(path_trunc), "%.200s...", path);
        path = path_trunc;
    }

    snprintf(buf, buf_size,
             "Spawn env: CLAUDECODE=%s HOME=%s PATH=%s",
             claudecode ? claudecode : "(unset)",
             home ? home : "(unset)",
             path ? path : "(unset)");
}

void el_heartbeat_text(int elapsed_sec, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size,
             "\xe2\x8f\xb3 Still working... (%ds since last output)",
             elapsed_sec);
}

void el_retry_notify_text(int attempt, int max_retries,
                           char *buf, size_t buf_size)
{
    snprintf(buf, buf_size,
             "Claude timed out. Retrying (attempt %d of %d)...",
             attempt, max_retries);
}

void el_build_photo_path(const char *photo_dir, const char *workspace,
                         const char *file_id, const char *ext,
                         char *out, size_t max)
{
    if (photo_dir && photo_dir[0] != '\0') {
        snprintf(out, max, "%s/%s.%s", photo_dir, file_id, ext);
    } else {
        const char *ws = (workspace && workspace[0] != '\0') ? workspace : "/tmp";
        snprintf(out, max, "%s/data/.telegram-photos/%s.%s", ws, file_id, ext);
    }
}

void el_photo_ack_text(const char *caption, char *buf, size_t buf_size)
{
    if (caption && caption[0] != '\0') {
        snprintf(buf, buf_size, "Got your photo! (caption: \"%s\")", caption);
    } else {
        snprintf(buf, buf_size, "Got your photo!");
    }
}

void el_unknown_command_text(const char *cmd, char *buf, size_t buf_size)
{
    const char *name = (cmd && cmd[0] != '\0') ? cmd : "(unknown)";
    snprintf(buf, buf_size, "Unknown command: %s \xe2\x80\x94 type /help for a list.", name);
}

const char *el_placeholder_text(void)
{
    return el_spinner_frame(0);
}

const char *el_startup_ready_text(void)
{
    return "\xe2\x9c\x93 Ready.";
}

void el_recovery_text(int has_context, char *buf, size_t buf_size)
{
    if (has_context) {
        snprintf(buf, buf_size,
                 "\xe2\x9a\xa1 I was restarted before I could finish my last response. "
                 "I've reviewed our recent conversation \xe2\x80\x94 "
                 "go ahead and I'll pick up where we left off.");
    } else {
        snprintf(buf, buf_size,
                 "\xe2\x9a\xa1 I was restarted before I could finish my last response. "
                 "Could you remind me what we were working on?");
    }
}
