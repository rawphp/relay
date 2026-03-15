#include "gemini.h"
#include "llm_prompt.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct gemini {
    relay_proc_t *proc;
    char binary[RELAY_MAX_PATH];
    char workspace[RELAY_MAX_PATH];
    char model[RELAY_MAX_VALUE];
    char approval_mode[RELAY_MAX_VALUE];
    char agent_name[RELAY_MAX_VALUE];
    char user_name[RELAY_MAX_VALUE];
    int sandbox_enabled;
    int timeout;
    int retry_count;
    int retry_backoff_ms;
};

static int gemini_parse_response(const char *json, gemini_response_t *resp)
{
    if (!json || !resp) {
        return RELAY_ERR_PARSE;
    }

    resp->session_id[0] = '\0';
    resp->result[0] = '\0';
    resp->duration_ms = 0;
    resp->is_error = 0;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        /* Gemini CLI may print a preamble line before JSON. */
        const char *start = strchr(json, '{');
        const char *end = strrchr(json, '}');
        if (start && end && end >= start) {
            size_t len = (size_t)(end - start + 1);
            char *trimmed = malloc(len + 1);
            if (!trimmed) {
                return RELAY_ERR_NOMEM;
            }
            memcpy(trimmed, start, len);
            trimmed[len] = '\0';
            root = cJSON_Parse(trimmed);
            free(trimmed);
        }
    }
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    cJSON *sid = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(sid)) {
        snprintf(resp->session_id, RELAY_MAX_SESSION_ID, "%s", sid->valuestring);
    }

    cJSON *response = cJSON_GetObjectItem(root, "response");
    if (cJSON_IsString(response)) {
        snprintf(resp->result, RELAY_MAX_RESPONSE, "%s", response->valuestring);
    }

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(error)) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (cJSON_IsString(message)) {
            snprintf(resp->result, RELAY_MAX_RESPONSE, "%s", message->valuestring);
        } else {
            char *dump = cJSON_PrintUnformatted(error);
            if (dump) {
                snprintf(resp->result, RELAY_MAX_RESPONSE, "%s", dump);
                free(dump);
            }
        }
        resp->is_error = 1;
    }

    cJSON_Delete(root);

    if (!resp->is_error && resp->result[0] == '\0') {
        return RELAY_ERR_PARSE;
    }

    return RELAY_OK;
}

static int parse_bool(const config_t *cfg, const char *key, int fallback)
{
    int value = config_get_int(cfg, key, fallback ? 1 : 0);
    return value != 0;
}

gemini_t *gemini_create(relay_proc_t *proc, const config_t *cfg)
{
    if (!proc || !cfg) {
        return NULL;
    }

    gemini_t *gm = calloc(1, sizeof(gemini_t));
    if (!gm) {
        return NULL;
    }

    gm->proc = proc;
    snprintf(gm->binary, RELAY_MAX_PATH, "%s",
             config_get(cfg, "gemini_binary", "gemini"));
    snprintf(gm->workspace, RELAY_MAX_PATH, "%s",
             config_get(cfg, "workspace_path", "."));
    snprintf(gm->model, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "gemini_model", "gemini-2.5-flash"));
    snprintf(gm->approval_mode, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "gemini_approval_mode", "plan"));
    snprintf(gm->agent_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "agent_name", "Kai"));
    snprintf(gm->user_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "user_name", "User"));
    gm->sandbox_enabled = parse_bool(cfg, "gemini_enable_sandbox", 1);
    gm->timeout = config_get_int(cfg, "gemini_timeout", 120);
    gm->retry_count = config_get_int(cfg, "gemini_retry_count", 3);
    gm->retry_backoff_ms = config_get_int(cfg, "gemini_retry_backoff_ms", 1000);

    return gm;
}

int gemini_send(gemini_t *gm, const char *message,
                const char *session_id, const char *workspace_path,
                gemini_response_t *resp)
{
    if (!gm || !message || !resp) {
        return RELAY_ERR;
    }
    (void)workspace_path; /* stub: workspace override not yet supported for Gemini */

    char prompt[RELAY_MAX_MSG];
    llm_build_system_prompt(prompt, sizeof(prompt), gm->workspace, message,
                         gm->agent_name, gm->user_name);

    const char *args[32];
    int argc = 0;

    args[argc++] = gm->binary;
    args[argc++] = "--prompt";
    args[argc++] = prompt;
    args[argc++] = "--output-format";
    args[argc++] = "json";
    args[argc++] = "--model";
    args[argc++] = gm->model;
    args[argc++] = "--include-directories";
    args[argc++] = gm->workspace;

    if (gm->sandbox_enabled) {
        args[argc++] = "--sandbox";
    }

    if (gm->approval_mode[0] != '\0' &&
        strcmp(gm->approval_mode, "none") != 0) {
        args[argc++] = "--approval-mode";
        args[argc++] = gm->approval_mode;
    }

    if (session_id && session_id[0] != '\0') {
        args[argc++] = "--resume";
        args[argc++] = session_id;
    }

    args[argc] = NULL;

    char output[RELAY_MAX_RESPONSE];
    int rc = gm->proc->spawn(gm->binary, args, NULL,
                             output, sizeof(output), gm->timeout);
    if (rc != RELAY_OK) {
        if (resp) {
            resp->is_error = 1;
            snprintf(resp->result, RELAY_MAX_RESPONSE, "%s", output);
        }
        return rc;
    }

    return gemini_parse_response(output, resp);
}

static int gemini_is_retriable(int rc)
{
    return (rc == RELAY_ERR_TIMEOUT || rc == RELAY_ERR);
}

int gemini_send_with_retry(gemini_t *gm, const char *message,
                           const char *session_id, const char *workspace_path,
                           gemini_response_t *resp)
{
    if (!gm || !message || !resp) {
        return RELAY_ERR;
    }
    (void)workspace_path; /* stub: workspace override not yet supported for Gemini */

    int attempt = 0;
    int backoff_ms = gm->retry_backoff_ms;
    int rc = RELAY_ERR;
    const char *attempt_session = session_id;

    while (attempt < gm->retry_count) {
        rc = gemini_send(gm, message, attempt_session, workspace_path, resp);
        if (rc == RELAY_OK && !resp->is_error) {
            return RELAY_OK;
        }

        if (attempt_session && attempt_session[0] != '\0' &&
            gemini_is_retriable(rc)) {
            attempt_session = NULL;
            continue;
        }

        if (!gemini_is_retriable(rc)) {
            return rc;
        }

        attempt++;
        if (attempt < gm->retry_count) {
            usleep((useconds_t)backoff_ms * 1000);
            backoff_ms *= 2;
        }
    }

    return rc;
}

void gemini_update_config(gemini_t *gm, const config_t *cfg)
{
    if (!gm || !cfg) {
        return;
    }

    gm->timeout = config_get_int(cfg, "gemini_timeout", gm->timeout);
    snprintf(gm->model, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "gemini_model", gm->model));
    snprintf(gm->approval_mode, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "gemini_approval_mode", gm->approval_mode));
    gm->sandbox_enabled = parse_bool(cfg, "gemini_enable_sandbox",
                                     gm->sandbox_enabled);
    gm->retry_count = config_get_int(cfg, "gemini_retry_count", gm->retry_count);
    gm->retry_backoff_ms = config_get_int(cfg, "gemini_retry_backoff_ms",
                                          gm->retry_backoff_ms);
}

void gemini_free(gemini_t *gm)
{
    free(gm);
}
