#include "llm_prompt.h"
#include "llm_format.h"
#include "peer_registry.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void llm_build_system_prompt(char *buf, size_t max,
                          const char *workspace, const char *message,
                          const char *agent_name, const char *user_name)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    static const char *days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };

    const char *tz_label = tm.tm_zone ? tm.tm_zone : "UTC";

    snprintf(buf, max,
        "System context:\n"
        "You are %s, %s's AI familiar. Read SOUL.md and IDENTITY.md for your "
        "full personality. You are NOT a generic assistant - you are %s.\n"
        "Workspace: %s\n"
        "Current time: %04d-%02d-%02d %02d:%02d %s (%s)\n\n"
        "User message:\n%s",
        agent_name, user_name, agent_name,
        workspace,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tz_label, days[tm.tm_wday],
        message);

    /* Append peer agent context if available */
    size_t used = strlen(buf);
    if (used + 1 < max) {
        peer_registry_build_context(buf + used, max - used);
    }

    llm_append_structured_format_instructions(buf, max);
}

/* ── Compaction prompt ───────────────────────────────────────────────── */

/* Substitute every occurrence of {date} in src with date_str, writing
 * the result into dst (at most max-1 bytes + NUL). */
static void subst_date(char *dst, size_t max,
                       const char *src, const char *date_str)
{
    size_t written = 0;
    size_t dlen    = strlen(date_str);
    const char *p  = src;

    while (*p && written + 1 < max) {
        if (strncmp(p, "{date}", 6) == 0) {
            size_t copy = dlen;
            if (written + copy + 1 >= max) {
                copy = max - written - 1;
            }
            memcpy(dst + written, date_str, copy);
            written += copy;
            p += 6;
        } else {
            dst[written++] = *p++;
        }
    }
    dst[written] = '\0';
}

void llm_load_compaction_prompt(char *buf, size_t max,
                                const char *workspace,
                                const char *date_str)
{
    /* Try to load {workspace}/data/prompts/compaction.txt */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/prompts/compaction.txt", workspace);

    FILE *f = fopen(path, "r");
    if (f) {
        char raw[4096];
        size_t n = fread(raw, 1, sizeof(raw) - 1, f);
        fclose(f);
        raw[n] = '\0';

        /* Strip trailing whitespace to detect empty file */
        while (n > 0 && (raw[n-1] == '\n' || raw[n-1] == ' ' || raw[n-1] == '\r'))
            raw[--n] = '\0';

        if (n > 0) {
            subst_date(buf, max, raw, date_str);
            return;
        }
    }

    /* Fallback: agent-reader optimised default */
    char fallback[2048];
    snprintf(fallback, sizeof(fallback),
        "Review this conversation. If anything is worth remembering "
        "long-term, write a structured entry to data/memory/%s.md.\n\n"
        "Use only the sections that apply:\n"
        "## Intent\n"
        "What the user was trying to accomplish; decisions made.\n\n"
        "## Technical Concepts\n"
        "Algorithms, patterns, domain knowledge introduced.\n\n"
        "## Files & Code\n"
        "Exact filenames, function names, data structures. "
        "Quote short critical snippets.\n\n"
        "## Errors & Fixes\n"
        "Exact error messages, root cause, fix applied.\n\n"
        "## Decisions\n"
        "Choices made, alternatives rejected, reasons.\n\n"
        "Preserve precision: names, numbers, paths. "
        "Omit small talk and status updates. "
        "If nothing worth remembering happened, do nothing.",
        date_str);

    snprintf(buf, max, "%s", fallback);
}
