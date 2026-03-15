#include "openai_codex.h"
#include "llm_prompt.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct openai_codex {
    relay_proc_t *proc;
    char binary[RELAY_MAX_PATH];
    char workspace[RELAY_MAX_PATH];
    char model[RELAY_MAX_VALUE];
    char sandbox[RELAY_MAX_VALUE];
    char agent_name[RELAY_MAX_VALUE];
    char user_name[RELAY_MAX_VALUE];
    int timeout;
    int retry_count;
    int retry_backoff_ms;
};

openai_codex_t *openai_codex_create(relay_proc_t *proc, const config_t *cfg)
{
    if (!proc || !cfg) {
        return NULL;
    }

    openai_codex_t *oa = calloc(1, sizeof(openai_codex_t));
    if (!oa) {
        return NULL;
    }

    oa->proc = proc;
    snprintf(oa->binary, RELAY_MAX_PATH, "%s",
             config_get(cfg, "openai_binary", "codex"));
    snprintf(oa->workspace, RELAY_MAX_PATH, "%s",
             config_get(cfg, "workspace_path", "."));
    snprintf(oa->model, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "openai_model", "gpt-5-codex"));
    snprintf(oa->sandbox, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "openai_sandbox", "workspace-write"));
    snprintf(oa->agent_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "agent_name", "Kai"));
    snprintf(oa->user_name, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "user_name", "User"));
    oa->timeout = config_get_int(cfg, "openai_timeout", 120);
    oa->retry_count = config_get_int(cfg, "openai_retry_count", 3);
    oa->retry_backoff_ms = config_get_int(cfg, "openai_retry_backoff_ms", 1000);

    return oa;
}

int openai_codex_parse_response(const char *jsonl, openai_codex_response_t *resp)
{
    if (!jsonl || !resp) {
        return RELAY_ERR_PARSE;
    }

    resp->session_id[0] = '\0';
    resp->result[0] = '\0';
    resp->duration_ms = 0;
    resp->is_error = 0;

    const char *line = jsonl;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);

        if (len > 0) {
            char entry[RELAY_MAX_MSG];
            if (len >= sizeof(entry)) {
                len = sizeof(entry) - 1;
            }
            memcpy(entry, line, len);
            entry[len] = '\0';

            cJSON *root = cJSON_Parse(entry);
            if (root) {
                cJSON *type = cJSON_GetObjectItem(root, "type");
                const char *type_str = cJSON_IsString(type) ? type->valuestring : "";

                if (strcmp(type_str, "thread.started") == 0) {
                    cJSON *thread_id = cJSON_GetObjectItem(root, "thread_id");
                    if (cJSON_IsString(thread_id)) {
                        snprintf(resp->session_id, RELAY_MAX_SESSION_ID,
                                 "%s", thread_id->valuestring);
                    }
                } else if (strcmp(type_str, "item.completed") == 0) {
                    cJSON *item = cJSON_GetObjectItem(root, "item");
                    cJSON *item_type = item ? cJSON_GetObjectItem(item, "type") : NULL;
                    cJSON *item_text = item ? cJSON_GetObjectItem(item, "text") : NULL;
                    if (cJSON_IsString(item_type) &&
                        strcmp(item_type->valuestring, "agent_message") == 0 &&
                        cJSON_IsString(item_text)) {
                        snprintf(resp->result, RELAY_MAX_RESPONSE,
                                 "%s", item_text->valuestring);
                    }
                } else if (strcmp(type_str, "error") == 0) {
                    resp->is_error = 1;
                    cJSON *message = cJSON_GetObjectItem(root, "message");
                    if (cJSON_IsString(message)) {
                        snprintf(resp->result, RELAY_MAX_RESPONSE,
                                 "%s", message->valuestring);
                    }
                }

                cJSON_Delete(root);
            }
        }

        if (!eol) {
            break;
        }
        line = eol + 1;
    }

    if (resp->result[0] == '\0' && !resp->is_error) {
        return RELAY_ERR_PARSE;
    }

    return RELAY_OK;
}

int openai_codex_send(openai_codex_t *oa, const char *message,
                      const char *session_id, const char *workspace_path,
                      openai_codex_response_t *resp)
{
    if (!oa || !message || !resp) {
        return RELAY_ERR;
    }
    (void)workspace_path; /* stub: workspace override not yet supported for OpenAI Codex */

    char prompt[RELAY_MAX_MSG];
    llm_build_system_prompt(prompt, sizeof(prompt), oa->workspace, message,
                         oa->agent_name, oa->user_name);

    const char *args[40];
    int argc = 0;

    args[argc++] = oa->binary;
    args[argc++] = "exec";
    int use_resume = (session_id && session_id[0] != '\0');
    if (use_resume) {
        args[argc++] = "resume";
    }
    args[argc++] = "--full-auto";

    /* codex exec resume has stricter accepted flags than plain exec */
    if (!use_resume) {
        args[argc++] = "--sandbox";
        args[argc++] = oa->sandbox;
        args[argc++] = "--cd";
        args[argc++] = oa->workspace;
    }

    args[argc++] = "--json";
    args[argc++] = "--skip-git-repo-check";
    args[argc++] = "--model";
    args[argc++] = oa->model;

    if (use_resume) {
        args[argc++] = session_id;
    }

    args[argc++] = "-";
    args[argc] = NULL;

    char output[RELAY_MAX_RESPONSE];
    int rc = oa->proc->spawn(oa->binary, args, prompt,
                             output, sizeof(output), oa->timeout);
    if (rc != RELAY_OK) {
        /* Preserve process output for caller logs on failures */
        snprintf(resp->result, RELAY_MAX_RESPONSE, "%s", output);
        return rc;
    }

    return openai_codex_parse_response(output, resp);
}

static int is_retriable_error(int rc)
{
    return (rc == RELAY_ERR_TIMEOUT || rc == RELAY_ERR);
}

int openai_codex_send_with_retry(openai_codex_t *oa, const char *message,
                                 const char *session_id,
                                 const char *workspace_path,
                                 openai_codex_response_t *resp)
{
    if (!oa || !message || !resp) {
        return RELAY_ERR;
    }
    (void)workspace_path; /* stub: workspace override not yet supported for OpenAI Codex */

    int rc = RELAY_ERR;
    int attempt = 0;
    int backoff_ms = oa->retry_backoff_ms;
    const char *attempt_session = session_id;

    while (attempt < oa->retry_count) {
        rc = openai_codex_send(oa, message, attempt_session, workspace_path, resp);

        if (rc == RELAY_OK && !resp->is_error) {
            return RELAY_OK;
        }

        /* If resume path fails, retry once on a fresh thread immediately. */
        if (attempt_session && attempt_session[0] != '\0' &&
            is_retriable_error(rc)) {
            attempt_session = NULL;
            continue;
        }

        if (!is_retriable_error(rc)) {
            return rc;
        }

        attempt++;
        if (attempt < oa->retry_count) {
            usleep((useconds_t)backoff_ms * 1000);
            backoff_ms *= 2;
        }
    }

    return rc;
}

void openai_codex_update_config(openai_codex_t *oa, const config_t *cfg)
{
    if (!oa || !cfg) {
        return;
    }

    oa->timeout = config_get_int(cfg, "openai_timeout", oa->timeout);
    snprintf(oa->model, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "openai_model", oa->model));
    snprintf(oa->sandbox, RELAY_MAX_VALUE, "%s",
             config_get(cfg, "openai_sandbox", oa->sandbox));
    oa->retry_count = config_get_int(cfg, "openai_retry_count", oa->retry_count);
    oa->retry_backoff_ms = config_get_int(cfg,
                                          "openai_retry_backoff_ms",
                                          oa->retry_backoff_ms);
}

void openai_codex_free(openai_codex_t *oa)
{
    free(oa);
}
