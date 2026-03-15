#ifndef RELAY_PROC_LOG_PARTIAL_H
#define RELAY_PROC_LOG_PARTIAL_H

#include "log.h"
#include <stddef.h>

/* ── proc_log_partial ───────────────────────────────────────────────── */
/* Log the last N bytes of a streaming output buffer at WARN level.     */
/* Used when a Claude CLI child exits without emitting a result line,   */
/* to capture partial/diagnostic output for post-mortem debugging.      */
/*                                                                       */
/* At most PROC_LOG_PARTIAL_MAX bytes from the end of buf are logged.   */
/* If log or buf is NULL, or len == 0, the call is a no-op.             */

#define PROC_LOG_PARTIAL_MAX 512

void proc_log_partial(relay_log_t *log, const char *buf, size_t len);

#endif /* RELAY_PROC_LOG_PARTIAL_H */
