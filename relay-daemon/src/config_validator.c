#include "config_validator.h"
#include "llm_provider.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void add_error(char errors[][RELAY_MAX_VALUE], int max,
                      int *count, const char *fmt, ...)
{
    if (*count >= max) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errors[*count], RELAY_MAX_VALUE, fmt, ap);
    va_end(ap);
    (*count)++;
}

static int is_gemini_provider(const char *provider)
{
    return provider &&
        (strcmp(provider, "gemini") == 0 ||
         strcmp(provider, "google_gemini") == 0);
}

int config_validate_options(const config_t *cfg,
                            char errors[][RELAY_MAX_VALUE],
                            int max_errors)
{
    if (!cfg) {
        return 0;
    }

    int count = 0;

    const char *provider = config_get(cfg, "llm_provider", "claude");
    if (strcmp(provider, "claude") != 0 &&
        !llm_is_openai_provider(provider) &&
        !is_gemini_provider(provider)) {
        add_error(errors, max_errors, &count,
                  "llm_provider '%s' is invalid (use claude, openai_codex, openai, codex, gemini, google_gemini)",
                  provider);
    }

    if (llm_is_openai_provider(provider)) {
        const char *sandbox = config_get(cfg, "openai_sandbox", "workspace-write");
        if (strcmp(sandbox, "read-only") != 0 &&
            strcmp(sandbox, "workspace-write") != 0 &&
            strcmp(sandbox, "danger-full-access") != 0) {
            add_error(errors, max_errors, &count,
                      "openai_sandbox '%s' is invalid (allowed: read-only, workspace-write, danger-full-access)",
                      sandbox);
        }
    }

    if (is_gemini_provider(provider)) {
        int sandbox = config_get_int(cfg, "gemini_enable_sandbox", 1);
        if (sandbox != 0 && sandbox != 1) {
            add_error(errors, max_errors, &count,
                      "gemini_enable_sandbox must be 0 or 1");
        }

        const char *approval = config_get(cfg, "gemini_approval_mode", "plan");
        if (strcmp(approval, "default") != 0 &&
            strcmp(approval, "auto_edit") != 0 &&
            strcmp(approval, "yolo") != 0 &&
            strcmp(approval, "plan") != 0) {
            add_error(errors, max_errors, &count,
                      "gemini_approval_mode '%s' is invalid (allowed: default, auto_edit, yolo, plan)",
                      approval);
        }
    }

    if (config_get_int(cfg, "session_expiry_hours", 24) <= 0) {
        add_error(errors, max_errors, &count,
                  "session_expiry_hours must be > 0");
    }

    return count;
}
