#ifndef RELAY_MEMORY_SIDECAR_H
#define RELAY_MEMORY_SIDECAR_H

#include "relay.h"
#include "config.h"
#include "log.h"
#include <sys/types.h>

/* ── Memory Sidecar Lifecycle ───────────────────────────────────────── */
/* Manages probe, autostart, health-watch, ingest timers, and shutdown  */
/* for the apps/memory-py/memory_http.py Python sidecar process.        */

/* Process-control abstraction for testability. */
typedef struct {
    /* Spawn a background process.  Returns PID on success, -1 on failure.
     * The process must not inherit the parent's file descriptors except
     * stdin/stdout/stderr.  The caller does NOT wait for it. */
    pid_t (*spawn_bg)(const char *bin, const char **args);

    /* Returns 1 if the process is still running, 0 if it has exited or
     * the PID is not valid. */
    int   (*pid_alive)(pid_t pid);

    /* Send SIGTERM to the given PID (best-effort, no error return). */
    void  (*kill_pid)(pid_t pid);
} memory_sidecar_proc_t;

/* Production implementations (defined in memory_sidecar.c). */
extern const memory_sidecar_proc_t MEMORY_SIDECAR_REAL_PROC;

/* Opaque sidecar manager type. */
typedef struct memory_sidecar memory_sidecar_t;

/* Create sidecar manager.
 * http — used for health probes and ingest POSTs.
 * proc — process-control abstraction; pass &MEMORY_SIDECAR_REAL_PROC in
 *         production, a mock struct in tests.
 * cfg  — read memory_service_url, memory_service_autostart, intervals,
 *         and memory_service_script.
 * Returns NULL on allocation failure. */
memory_sidecar_t *memory_sidecar_create(relay_http_t *http,
                                         const memory_sidecar_proc_t *proc,
                                         const config_t *cfg);

/* Probe the sidecar health endpoint once.
 * Returns 1 if healthy, 0 if unreachable or returning an error status. */
int memory_sidecar_probe(memory_sidecar_t *sc);

/* Called once at event loop startup.
 * If the sidecar is already healthy: log "found at <url>" and return.
 * If unhealthy and memory_service_autostart=1: spawn and re-probe.
 * If unhealthy and autostart=0: log warning; memory stays disabled. */
void memory_sidecar_startup(memory_sidecar_t *sc, relay_log_t *log);

/* Called every event loop iteration.
 * If memory_service_watch_interval_sec has elapsed since the last check:
 *   probe health; if unhealthy and autostart=1: respawn. */
void memory_sidecar_watch(memory_sidecar_t *sc, time_t now, relay_log_t *log);

/* Fire POST /ingest_daily_logs if memory_ingest_interval_sec has elapsed.
 * agent_home — root directory of the relay agent (e.g. ~/relay). */
void memory_sidecar_maybe_ingest_logs(memory_sidecar_t *sc, time_t now,
                                       const char *agent_home);

/* Fire POST /ingest_transcripts if memory_transcript_ingest_interval_sec
 * has elapsed. */
void memory_sidecar_maybe_ingest_transcripts(memory_sidecar_t *sc, time_t now,
                                              const char *agent_home);

/* Send SIGTERM to the sidecar process if the daemon spawned it.
 * No-op if the daemon did not start the sidecar (external instance). */
void memory_sidecar_stop(memory_sidecar_t *sc);

/* Returns 1 if the last health probe succeeded, 0 otherwise. */
int memory_sidecar_is_healthy(const memory_sidecar_t *sc);

/* Free all resources.  Call memory_sidecar_stop() first if needed. */
void memory_sidecar_free(memory_sidecar_t *sc);

#endif /* RELAY_MEMORY_SIDECAR_H */
