#include "pid_file.h"
#include "path_util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Production proc implementation ─────────────────────────────────── */

static int real_is_alive(int pid)
{
    return pid > 0 && kill(pid, 0) == 0;
}

const pid_proc_t PID_PROC_REAL = { .is_alive = real_is_alive };

/* ── Public API ──────────────────────────────────────────────────────── */

int pid_startup_check(const char *path, int my_pid,
                      const pid_proc_t *proc, const relay_fs_t *fs,
                      relay_log_t *log)
{
    if (fs->file_exists(path)) {
        char *content = fs->read_file(path);
        int old_pid = 0;
        if (content) {
            old_pid = atoi(content);
            free(content);
        }

        if (old_pid > 0 && proc->is_alive(old_pid)) {
            log_write(log, LOG_ERROR,
                      "relay already running (pid=%d)", old_pid);
            return RELAY_ERR_FULL;
        }

        if (old_pid > 0) {
            log_write(log, LOG_WARN,
                      "Removing stale PID file (pid %d is dead)", old_pid);
        }
        fs->delete_file(path);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", my_pid);
    if (fs->write_file(path, buf) != RELAY_OK) {
        log_write(log, LOG_ERROR, "Failed to write PID file");
        return RELAY_ERR;
    }

    return RELAY_OK;
}

void pid_file_remove(const char *path, const relay_fs_t *fs)
{
    fs->delete_file(path);
}

int pid_default_path(const char *config_path, char *out, size_t max)
{
    char install_dir[RELAY_MAX_PATH];
    if (path_util_install_dir(config_path, install_dir, sizeof(install_dir))
            != RELAY_OK) {
        return RELAY_ERR;
    }

    snprintf(out, max, "%s/data/relay.pid", install_dir);
    return RELAY_OK;
}
