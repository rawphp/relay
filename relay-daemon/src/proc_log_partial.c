#include "proc_log_partial.h"
#include <string.h>

/*
 * proc_log_partial — log the tail of a streaming output buffer.
 *
 * When Claude CLI exits without emitting a "result" JSON line, the
 * streaming buffer may contain a partial error message or raw CLI output
 * that would otherwise be silently discarded.  This helper logs the last
 * min(len, PROC_LOG_PARTIAL_MAX) bytes at WARN level so post-mortem
 * diagnosis is possible without rerunning the job.
 */
void proc_log_partial(relay_log_t *log, const char *buf, size_t len)
{
    if (!log || !buf || len == 0) {
        return;
    }

    /* Select the tail of the buffer (most recent output). */
    const char *tail   = buf;
    size_t      tail_len = len;
    if (len > PROC_LOG_PARTIAL_MAX) {
        tail     = buf + (len - PROC_LOG_PARTIAL_MAX);
        tail_len = PROC_LOG_PARTIAL_MAX;
    }

    /* Copy into a NUL-terminated stack buffer so log_write can use %s. */
    char tmp[PROC_LOG_PARTIAL_MAX + 1];
    memcpy(tmp, tail, tail_len);
    tmp[tail_len] = '\0';

    log_write(log, LOG_WARN, "[claude] partial output: %.512s", tmp);
}
