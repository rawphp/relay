#ifndef RELAY_LOG_H
#define RELAY_LOG_H

#include "relay.h"

/* ── Log ────────────────────────────────────────────────────────────── */
/* File logger with timestamps, levels, and token masking.              */

/* Log levels */
typedef enum {
    LOG_INFO  = 0,
    LOG_WARN  = 1,
    LOG_ERROR = 2
} log_level_t;

/* Opaque log type */
typedef struct relay_log relay_log_t;

/* Create logger. Writes to file at path. min_level filters output.
 * Pass clock for testable timestamps. Returns NULL on error. */
relay_log_t *log_create(const char *path, log_level_t min_level,
                        relay_clock_t *clock);

/* Write a log line. */
void log_write(relay_log_t *log, log_level_t level,
               const char *fmt, ...);

/* Mask a sensitive value (returns static buffer — not thread-safe).
 * Shows first 6 chars + "..." for values > 6 chars. */
const char *log_mask(const char *value);

/* Close and free logger. */
void log_close(relay_log_t *log);

/* Format a log line into a buffer (for testing).
 * Returns number of chars written, or -1 on error. */
int log_format(char *buf, size_t max, log_level_t level,
               const struct tm *tm, const char *msg);

#endif /* RELAY_LOG_H */
