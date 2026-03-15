#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ─────────────────────────────────────────────────── */

struct relay_log {
    FILE *file;
    log_level_t min_level;
    relay_clock_t *clock;
};

/* ── Helpers ────────────────────────────────────────────────────────── */

static const char *level_str(log_level_t level)
{
    switch (level) {
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    default:        return "UNKNOWN";
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

int log_format(char *buf, size_t max, log_level_t level,
               const struct tm *tm, const char *msg)
{
    if (!buf || max == 0 || !tm || !msg) {
        return -1;
    }

    return snprintf(buf, max, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec,
                    level_str(level), msg);
}

relay_log_t *log_create(const char *path, log_level_t min_level,
                        relay_clock_t *clock)
{
    if (!path || !clock) {
        return NULL;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        return NULL;
    }

    relay_log_t *log = calloc(1, sizeof(relay_log_t));
    if (!log) {
        fclose(f);
        return NULL;
    }

    log->file = f;
    log->min_level = min_level;
    log->clock = clock;
    return log;
}

void log_write(relay_log_t *log, log_level_t level, const char *fmt, ...)
{
    if (!log || !fmt || level < log->min_level) {
        return;
    }

    /* Format the message */
    char msg[RELAY_MAX_LINE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* Get timestamp */
    time_t now = log->clock->now();
    struct tm tm;
    log->clock->localtime_r(&now, &tm);

    /* Format and write the log line */
    char line[RELAY_MAX_LINE];
    log_format(line, sizeof(line), level, &tm, msg);
    fputs(line, log->file);
    fflush(log->file);
}

const char *log_mask(const char *value)
{
    _Thread_local static char masked[RELAY_MAX_TOKEN];

    if (!value) {
        return "(null)";
    }

    size_t len = strlen(value);
    if (len <= 6) {
        /* Short values aren't sensitive enough to mask */
        snprintf(masked, sizeof(masked), "%s", value);
    } else {
        /* Show first 6 chars + "..." */
        snprintf(masked, sizeof(masked), "%.6s...", value);
    }

    return masked;
}

void log_close(relay_log_t *log)
{
    if (!log) {
        return;
    }
    if (log->file) {
        fclose(log->file);
    }
    free(log);
}
