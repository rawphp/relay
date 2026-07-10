#include "llm_provider.h"

#include "claude.h"
#include "gemini.h"
#include "openai_codex.h"
#include "profiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    LLM_BACKEND_CLAUDE = 0,
    LLM_BACKEND_OPENAI_CODEX = 1,
    LLM_BACKEND_GEMINI = 2
} llm_backend_t;

/* Max bytes for pre-injected identity file contents.
 * SOUL.md + IDENTITY.md + USER.md + PRIORITIES.md + MEMORY.md + SKILLS.md. */
#define RELAY_MAX_IDENTITY_CONTEXT 32768

struct llm_provider {
    llm_backend_t backend;
    union {
        claude_t *claude;
        openai_codex_t *openai_codex;
        gemini_t *gemini;
    } client;
    /* Lazy-initialized secondary backends for per-workspace provider override */
    claude_t       *lazy_claude;
    gemini_t       *lazy_gemini;
    memory_search_t *memory;             /* optional — may be NULL */
    relay_fs_t       *fs;                 /* optional — for identity injection */
    char             workspace[RELAY_MAX_PATH]; /* used by identity injection */
    relay_proc_t    *proc;               /* stored for lazy-init */
    const config_t  *cfg;                /* stored for lazy-init */
};

/* ── Identity file injection ─────────────────────────────────────────── */

/* Identity files injected in order into the message prefix.
 * SOUL.md first so Claude gets character before anything else.
 * MEMORY.md is the long-term curated store — always relevant context.
 * SKILLS.md lists available tools/skills so the agent knows what it can use. */
static const char *k_identity_files[] = {
    "SOUL.md", "IDENTITY.md", "USER.md", "PRIORITIES.md", "MEMORY.md",
    "SKILLS.md", NULL
};

/* Build an identity context block from the files in llm->workspace.
 * Returns the number of bytes written to buf (0 if nothing injected). */
static size_t build_identity_block(llm_provider_t *llm,
                                    char *buf, size_t max)
{
    if (!llm->fs || llm->workspace[0] == '\0') {
        return 0;
    }

    size_t w = 0;
    w += (size_t)snprintf(buf + w, max - w,
                          "[Identity context — pre-injected]\n");

    int any = 0;
    for (int i = 0; k_identity_files[i] && w + 2 < max; i++) {
        char path[RELAY_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s",
                 llm->workspace, k_identity_files[i]);

        char *content = llm->fs->read_file(path);
        if (!content) {
            continue;
        }

        w += (size_t)snprintf(buf + w, max - w,
                              "\n## %s\n%s\n",
                              k_identity_files[i], content);
        free(content);
        any = 1;
    }

    if (!any) {
        buf[0] = '\0';
        return 0;
    }

    w += (size_t)snprintf(buf + w, max - w,
                          "[End of identity context]\n\n");
    return w;
}

/* ── Memory + identity context injection ─────────────────────────────── */

static void augment_with_memory(llm_provider_t *llm, const char *message,
                                char *augmented, size_t augmented_size)
{
    /* ── 1. Build identity prefix ──────────────────────────────────── */
    char identity[RELAY_MAX_IDENTITY_CONTEXT];
    identity[0] = '\0';
    build_identity_block(llm, identity, sizeof(identity));

    /* ── 2. Run memory search (on the original user message) ───────── */
    char context[RELAY_MAX_MEMORY_CONTEXT];
    context[0] = '\0';
    int n = 0;

    if (llm->memory) {
        profiler_timer_t timer;
        profiler_timer_start(&timer);

        n = memory_search_query(llm->memory, message,
                                context, sizeof(context));

        long elapsed = profiler_timer_elapsed_ms(&timer);
        char detail[64];
        snprintf(detail, sizeof(detail), "results=%d", n);
        profiler_emit_event("memory.search", elapsed, "ok", detail);
    }

    /* ── 3. Assemble: identity + memory + message ───────────────────── */
    int has_identity = identity[0] != '\0';
    int has_memory   = n > 0 && context[0] != '\0';

    if (has_identity && has_memory) {
        snprintf(augmented, augmented_size,
                 "%s"
                 "[Relevant memories for context]\n%s\n"
                 "[End of memory context]\n\n%s",
                 identity, context, message);
    } else if (has_identity) {
        snprintf(augmented, augmented_size, "%s%s", identity, message);
    } else if (has_memory) {
        snprintf(augmented, augmented_size,
                 "[Relevant memories for context]\n%s\n"
                 "[End of memory context]\n\n%s",
                 context, message);
    } else {
        snprintf(augmented, augmented_size, "%s", message);
    }
}

int llm_is_openai_provider(const char *provider)
{
    return provider &&
        (strcmp(provider, "openai_codex") == 0 ||
         strcmp(provider, "openai") == 0 ||
         strcmp(provider, "codex") == 0);
}

static int is_gemini_provider_name(const char *provider)
{
    return strcmp(provider, "gemini") == 0 ||
           strcmp(provider, "google_gemini") == 0;
}

static void copy_provider_resp(llm_response_t *dst,
                               const char *session_id, const char *result,
                               int duration_ms, int is_error)
{
    snprintf(dst->session_id, RELAY_MAX_SESSION_ID, "%s", session_id);
    snprintf(dst->result, RELAY_MAX_RESPONSE, "%s", result);
    dst->duration_ms = duration_ms;
    dst->is_error = is_error;
}

llm_provider_t *llm_provider_create(relay_proc_t *proc, const config_t *cfg)
{
    if (!proc || !cfg) {
        return NULL;
    }

    llm_provider_t *llm = calloc(1, sizeof(llm_provider_t));
    if (!llm) {
        return NULL;
    }

    const char *provider = config_get(cfg, "llm_provider", "claude");
    if (llm_is_openai_provider(provider)) {
        llm->backend = LLM_BACKEND_OPENAI_CODEX;
        llm->client.openai_codex = openai_codex_create(proc, cfg);
        if (!llm->client.openai_codex) {
            free(llm);
            return NULL;
        }
    } else if (is_gemini_provider_name(provider)) {
        llm->backend = LLM_BACKEND_GEMINI;
        llm->client.gemini = gemini_create(proc, cfg);
        if (!llm->client.gemini) {
            free(llm);
            return NULL;
        }
    } else {
        llm->backend = LLM_BACKEND_CLAUDE;
        llm->client.claude = claude_create(proc, cfg);
        if (!llm->client.claude) {
            free(llm);
            return NULL;
        }
    }

    llm->proc = proc;
    llm->cfg  = cfg;

    return llm;
}

int llm_provider_send(llm_provider_t *llm, const char *message,
                      const char *session_id, llm_response_t *resp)
{
    if (!llm || !message || !resp) {
        return RELAY_ERR;
    }

    /* Augment message with memory context (no-op if memory is NULL) */
    char augmented[RELAY_MAX_MSG];
    augment_with_memory(llm, message, augmented, sizeof(augmented));
    profiler_timer_t backend_timer;
    profiler_timer_start(&backend_timer);
    const char *backend_name = llm_provider_name(llm);

    if (llm->backend == LLM_BACKEND_OPENAI_CODEX) {
        openai_codex_response_t oa_resp;
        memset(&oa_resp, 0, sizeof(oa_resp));
        int rc = openai_codex_send(llm->client.openai_codex,
                                   augmented, session_id, NULL, &oa_resp);
        copy_provider_resp(resp, oa_resp.session_id, oa_resp.result,
                           oa_resp.duration_ms, oa_resp.is_error);
        profiler_emit_event("llm.send", profiler_timer_elapsed_ms(&backend_timer),
                            (rc == RELAY_OK && !oa_resp.is_error) ? "ok" : "error",
                            backend_name);
        return rc;
    }

    if (llm->backend == LLM_BACKEND_GEMINI) {
        gemini_response_t gm_resp;
        memset(&gm_resp, 0, sizeof(gm_resp));
        int rc = gemini_send(llm->client.gemini,
                             augmented, session_id, NULL, &gm_resp);
        copy_provider_resp(resp, gm_resp.session_id, gm_resp.result,
                           gm_resp.duration_ms, gm_resp.is_error);
        profiler_emit_event("llm.send", profiler_timer_elapsed_ms(&backend_timer),
                            (rc == RELAY_OK && !gm_resp.is_error) ? "ok" : "error",
                            backend_name);
        return rc;
    }

    claude_response_t cl_resp;
    memset(&cl_resp, 0, sizeof(cl_resp));
    int rc = claude_send(llm->client.claude, augmented, session_id, NULL, &cl_resp);
    copy_provider_resp(resp, cl_resp.session_id, cl_resp.result,
                       cl_resp.duration_ms, cl_resp.is_error);
    profiler_emit_event("llm.send", profiler_timer_elapsed_ms(&backend_timer),
                        (rc == RELAY_OK && !cl_resp.is_error) ? "ok" : "error",
                        backend_name);
    return rc;
}

int llm_provider_send_with_retry(llm_provider_t *llm, const char *message,
                                 const char *session_id, llm_response_t *resp)
{
    if (!llm || !message || !resp) {
        return RELAY_ERR;
    }

    /* Augment message with memory context (no-op if memory is NULL) */
    char augmented[RELAY_MAX_MSG];
    augment_with_memory(llm, message, augmented, sizeof(augmented));
    profiler_timer_t backend_timer;
    profiler_timer_start(&backend_timer);
    const char *backend_name = llm_provider_name(llm);

    if (llm->backend == LLM_BACKEND_OPENAI_CODEX) {
        openai_codex_response_t oa_resp;
        memset(&oa_resp, 0, sizeof(oa_resp));
        int rc = openai_codex_send_with_retry(llm->client.openai_codex,
                                              augmented, session_id, NULL, &oa_resp);
        copy_provider_resp(resp, oa_resp.session_id, oa_resp.result,
                           oa_resp.duration_ms, oa_resp.is_error);
        profiler_emit_event("llm.send_with_retry",
                            profiler_timer_elapsed_ms(&backend_timer),
                            (rc == RELAY_OK && !oa_resp.is_error) ? "ok" : "error",
                            backend_name);
        return rc;
    }

    if (llm->backend == LLM_BACKEND_GEMINI) {
        gemini_response_t gm_resp;
        memset(&gm_resp, 0, sizeof(gm_resp));
        int rc = gemini_send_with_retry(llm->client.gemini,
                                        augmented, session_id, NULL, &gm_resp);
        copy_provider_resp(resp, gm_resp.session_id, gm_resp.result,
                           gm_resp.duration_ms, gm_resp.is_error);
        profiler_emit_event("llm.send_with_retry",
                            profiler_timer_elapsed_ms(&backend_timer),
                            (rc == RELAY_OK && !gm_resp.is_error) ? "ok" : "error",
                            backend_name);
        return rc;
    }

    claude_response_t cl_resp;
    memset(&cl_resp, 0, sizeof(cl_resp));
    int rc = claude_send_with_retry(llm->client.claude,
                                    augmented, session_id, NULL, &cl_resp);
    copy_provider_resp(resp, cl_resp.session_id, cl_resp.result,
                       cl_resp.duration_ms, cl_resp.is_error);
    profiler_emit_event("llm.send_with_retry",
                        profiler_timer_elapsed_ms(&backend_timer),
                        (rc == RELAY_OK && !cl_resp.is_error) ? "ok" : "error",
                        backend_name);
    return rc;
}

int llm_provider_send_streaming(llm_provider_t *llm, const char *message,
                                const char *session_id,
                                const char *workspace_path,
                                llm_token_cb on_token, void *userdata,
                                llm_response_t *resp)
{
    if (!llm || !message || !resp) {
        return RELAY_ERR;
    }

    /* Memory augmentation — same as send_with_retry */
    char augmented[RELAY_MAX_MSG];
    augment_with_memory(llm, message, augmented, sizeof(augmented));
    profiler_timer_t backend_timer;
    profiler_timer_start(&backend_timer);
    const char *backend_name = llm_provider_name(llm);

    /* Only Claude backend supports streaming; fall back for others.
     * Use the retry wrapper — it handles transient failures and stale
     * --resume sessions (drops the dead session and retries fresh) and
     * never re-streams after tokens were already delivered. */
    if (llm->backend == LLM_BACKEND_CLAUDE) {
        claude_response_t cl_resp;
        memset(&cl_resp, 0, sizeof(cl_resp));
        int rc = claude_send_streaming_with_retry(
            llm->client.claude, augmented, session_id,
            workspace_path, on_token, userdata, &cl_resp);
        copy_provider_resp(resp, cl_resp.session_id, cl_resp.result,
                           cl_resp.duration_ms, cl_resp.is_error);
        profiler_emit_event("llm.send_streaming",
                            profiler_timer_elapsed_ms(&backend_timer),
                            (rc == RELAY_OK && !cl_resp.is_error) ? "ok" : "error",
                            backend_name);
        return rc;
    }

    /* Non-Claude backends: fall back to blocking with retry */
    return llm_provider_send_with_retry(llm, message, session_id, resp);
}

void llm_provider_set_memory(llm_provider_t *llm, memory_search_t *ms)
{
    if (llm) {
        llm->memory = ms;
    }
}

void llm_provider_set_identity(llm_provider_t *llm, relay_fs_t *fs,
                                const char *workspace)
{
    if (!llm) {
        return;
    }
    llm->fs = fs;
    if (workspace) {
        snprintf(llm->workspace, sizeof(llm->workspace), "%s", workspace);
    } else {
        llm->workspace[0] = '\0';
    }
}

void llm_provider_update_config(llm_provider_t *llm, const config_t *cfg)
{
    if (!llm || !cfg) {
        return;
    }

    if (llm->backend == LLM_BACKEND_OPENAI_CODEX) {
        openai_codex_update_config(llm->client.openai_codex, cfg);
        return;
    }

    if (llm->backend == LLM_BACKEND_GEMINI) {
        gemini_update_config(llm->client.gemini, cfg);
        return;
    }

    claude_update_config(llm->client.claude, cfg);
}

const char *llm_provider_name(const llm_provider_t *llm)
{
    if (!llm) {
        return "unknown";
    }

    if (llm->backend == LLM_BACKEND_OPENAI_CODEX) {
        return "openai_codex";
    }

    if (llm->backend == LLM_BACKEND_GEMINI) {
        return "gemini";
    }

    return "claude";
}

int llm_provider_in_flight(const llm_provider_t *llm)
{
    if (!llm) {
        return 0;
    }
    /* Only the Claude backend tracks in-flight calls; other backends return 0 */
    if (llm->backend == LLM_BACKEND_CLAUDE) {
        return claude_in_flight(llm->client.claude);
    }
    return 0;
}

void llm_provider_free(llm_provider_t *llm)
{
    if (!llm) {
        return;
    }

    if (llm->backend == LLM_BACKEND_OPENAI_CODEX) {
        openai_codex_free(llm->client.openai_codex);
    } else if (llm->backend == LLM_BACKEND_GEMINI) {
        gemini_free(llm->client.gemini);
    } else {
        claude_free(llm->client.claude);
    }

    /* Free lazy-initialized secondary backends */
    if (llm->lazy_claude)  claude_free(llm->lazy_claude);
    if (llm->lazy_gemini)  gemini_free(llm->lazy_gemini);

    free(llm);
}

/* ── Workspace-aware send ─────────────────────────────────────────────── */

int llm_provider_send_workspace(llm_provider_t *llm,
                                 const char     *message,
                                 const char     *session_id,
                                 const char     *workspace_path,
                                 const char     *provider_name,
                                 llm_response_t *resp)
{
    if (!llm || !message || !resp) {
        return RELAY_ERR;
    }

    /* ── 1. Temporarily override workspace for identity injection ──────── */
    char saved_workspace[RELAY_MAX_PATH];
    snprintf(saved_workspace, sizeof(saved_workspace), "%s", llm->workspace);

    if (workspace_path && workspace_path[0] != '\0') {
        snprintf(llm->workspace, sizeof(llm->workspace),
                 "%s", workspace_path);
    }

    /* ── 2. Select backend by provider_name ────────────────────────── */
    /* Build response using appropriate backend */
    int rc;
    if (provider_name && provider_name[0] != '\0' &&
        llm->proc && llm->cfg) {

        if (is_gemini_provider_name(provider_name) &&
            llm->backend != LLM_BACKEND_GEMINI) {
            /* Lazy-init gemini if not already the primary */
            if (!llm->lazy_gemini) {
                llm->lazy_gemini = gemini_create(llm->proc, llm->cfg);
            }
            if (llm->lazy_gemini) {
                char augmented[RELAY_MAX_MSG];
                augment_with_memory(llm, message, augmented, sizeof(augmented));
                gemini_response_t gm_resp;
                memset(&gm_resp, 0, sizeof(gm_resp));
                rc = gemini_send_with_retry(llm->lazy_gemini,
                                            augmented, session_id, workspace_path, &gm_resp);
                copy_provider_resp(resp, gm_resp.session_id, gm_resp.result,
                                   gm_resp.duration_ms, gm_resp.is_error);
                goto restore;
            }
        }

        if (!is_gemini_provider_name(provider_name) &&
            !llm_is_openai_provider(provider_name) &&
            llm->backend != LLM_BACKEND_CLAUDE) {
            /* Lazy-init claude if not already the primary */
            if (!llm->lazy_claude) {
                llm->lazy_claude = claude_create(llm->proc, llm->cfg);
            }
            if (llm->lazy_claude) {
                char augmented[RELAY_MAX_MSG];
                augment_with_memory(llm, message, augmented, sizeof(augmented));
                claude_response_t cl_resp;
                memset(&cl_resp, 0, sizeof(cl_resp));
                rc = claude_send_with_retry(llm->lazy_claude,
                                            augmented, session_id, workspace_path, &cl_resp);
                copy_provider_resp(resp, cl_resp.session_id, cl_resp.result,
                                   cl_resp.duration_ms, cl_resp.is_error);
                goto restore;
            }
        }
    }

    /* ── 3. Fall back to default backend ────────────────────────────── */
    /* For Claude: pass workspace_path explicitly so claude_send() uses it
     * for chdir() and --add-dir rather than the stale cl->workspace default. */
    if (llm->backend == LLM_BACKEND_CLAUDE && llm->client.claude
        && workspace_path && workspace_path[0] != '\0') {
        char aug[RELAY_MAX_MSG];
        augment_with_memory(llm, message, aug, sizeof(aug));
        claude_response_t cl_resp;
        memset(&cl_resp, 0, sizeof(cl_resp));
        rc = claude_send_with_retry(llm->client.claude, aug, session_id,
                                    workspace_path, &cl_resp);
        copy_provider_resp(resp, cl_resp.session_id, cl_resp.result,
                           cl_resp.duration_ms, cl_resp.is_error);
    } else {
        rc = llm_provider_send_with_retry(llm, message, session_id, resp);
    }

restore:
    /* Restore saved workspace */
    snprintf(llm->workspace, sizeof(llm->workspace), "%s", saved_workspace);
    return rc;
}
