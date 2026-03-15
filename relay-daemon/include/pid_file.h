#ifndef RELAY_PID_FILE_H
#define RELAY_PID_FILE_H

#include "relay.h"
#include "log.h"

/* Process-liveness abstraction for testability. */
typedef struct {
    int (*is_alive)(int pid); /* Returns 1 if the process is running, 0 if dead. */
} pid_proc_t;

/* Production implementation (uses kill(pid, 0)). */
extern const pid_proc_t PID_PROC_REAL;

/* Check whether it is safe to start and write the PID file.
 *
 * If no PID file exists: write my_pid to path, return RELAY_OK.
 * If a PID file exists and the recorded process is dead (stale):
 *   log WARN, remove the old file, write my_pid, return RELAY_OK.
 * If a PID file exists and the recorded process is alive:
 *   log ERROR "relay already running (pid=NNN)", return RELAY_ERR_FULL.
 * If writing fails: log ERROR, return RELAY_ERR. */
int pid_startup_check(const char *path, int my_pid,
                      const pid_proc_t *proc, const relay_fs_t *fs,
                      relay_log_t *log);

/* Remove the PID file (call on clean shutdown). */
void pid_file_remove(const char *path, const relay_fs_t *fs);

/* Derive the default PID file path from the config file path.
 *
 * Given e.g. "/home/relay/config/relay.conf", produces
 * "/home/relay/data/relay.pid".
 *
 * Returns RELAY_OK on success, RELAY_ERR if config_path is NULL or
 * does not end with "/config/relay.conf". */
int pid_default_path(const char *config_path, char *out, size_t max);

#endif /* RELAY_PID_FILE_H */
