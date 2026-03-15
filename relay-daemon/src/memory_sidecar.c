#include "memory_sidecar.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

/* ── Defaults ────────────────────────────────────────────────────────── */

#define DEFAULT_SERVICE_URL        "http://localhost:8765"
#define DEFAULT_WATCH_INTERVAL_SEC 30
#define DEFAULT_INGEST_INTERVAL_SEC 300
#define DEFAULT_TX_INTERVAL_SEC    1800

/* ── Internal struct ─────────────────────────────────────────────────── */

struct memory_sidecar {
    relay_http_t           *http;
    memory_sidecar_proc_t   proc;

    char service_url[RELAY_MAX_URL];
    char script_path[RELAY_MAX_PATH]; /* path to memory_http.py */
    char data_dir[RELAY_MAX_PATH];    /* --data-dir arg */

    int  autostart;
    int  watch_interval_sec;
    int  ingest_interval_sec;
    int  tx_interval_sec;

    pid_t sidecar_pid;   /* 0 = not spawned by this daemon */
    time_t last_watch;
    time_t last_ingest;
    time_t last_tx_ingest;
    int healthy;         /* 1 = last probe succeeded */

    int respawn_count;   /* consecutive unhealthy respawn attempts */
    int max_respawn;     /* cap from memory_max_respawn config (default 5) */

    int backoff_sec;         /* current backoff interval (doubles on failure) */
    int backoff_max_sec;     /* ceiling for backoff (from config, default 300) */

    int stable_count;        /* consecutive healthy probes since last failure */
    int stable_threshold;    /* required consecutive healthy probes to reset respawn_count */

    char auth_token[65];     /* random hex token for sidecar auth (64 hex chars + NUL) */
};

/* ── Production process-control implementations ──────────────────────── */

static pid_t real_spawn_bg(const char *bin, const char **args)
{
    pid_t pid = fork();
    if (pid < 0) {
        return (pid_t)-1;
    }
    if (pid == 0) {
        /* Child: redirect stdin/stdout/stderr to /dev/null */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        /* Close all other fds */
        int maxfd = (int)sysconf(_SC_OPEN_MAX);
        for (int fd = STDERR_FILENO + 1; fd < maxfd; fd++) {
            close(fd);
        }
        execv(bin, (char *const *)args);
        _exit(127); /* execv failed */
    }
    return pid; /* parent: return child PID */
}

static int real_pid_alive(pid_t pid)
{
    if (pid <= 0) return 0;
    /* waitpid with WNOHANG to reap zombie if exited */
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) {
        return 1; /* still running */
    }
    return 0; /* exited or error */
}

static void real_kill_pid(pid_t pid)
{
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

const memory_sidecar_proc_t MEMORY_SIDECAR_REAL_PROC = {
    .spawn_bg  = real_spawn_bg,
    .pid_alive = real_pid_alive,
    .kill_pid  = real_kill_pid,
};

/* ── Internal helpers ────────────────────────────────────────────────── */

static void generate_auth_token(char *buf, size_t buf_size)
{
    /* Generate 32 random bytes → 64 hex chars */
    unsigned char raw[32];
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) {
        size_t n = fread(raw, 1, sizeof(raw), fp);
        fclose(fp);
        if (n == sizeof(raw)) {
            size_t pos = 0;
            for (size_t i = 0; i < sizeof(raw) && pos + 2 < buf_size; i++) {
                pos += (size_t)snprintf(buf + pos, buf_size - pos,
                                         "%02x", raw[i]);
            }
            return;
        }
    }
    /* Fallback: use time + pid for a weaker but usable token */
    snprintf(buf, buf_size, "%lx%lx%08x",
             (unsigned long)time(NULL), (unsigned long)getpid(), rand());
}

static void build_auth_url(const memory_sidecar_t *sc,
                            const char *path, char *url, size_t url_size)
{
    if (sc->auth_token[0] != '\0') {
        snprintf(url, url_size, "%s%s?auth=%s",
                 sc->service_url, path, sc->auth_token);
    } else {
        snprintf(url, url_size, "%s%s", sc->service_url, path);
    }
}

static int probe_once(memory_sidecar_t *sc)
{
    char url[RELAY_MAX_URL];
    char resp[512];
    build_auth_url(sc, "/health", url, sizeof(url));
    int rc = sc->http->get(url, resp, sizeof(resp));
    if (rc != RELAY_OK) {
        sc->healthy = 0;
        sc->stable_count = 0;
        return 0;
    }
    if (strstr(resp, "\"ok\"") == NULL &&
        strstr(resp, "\"degraded\"") == NULL) {
        sc->healthy = 0;
        sc->stable_count = 0;
        return 0;
    }
    sc->healthy = 1;
    sc->stable_count++;
    if (sc->stable_count >= sc->stable_threshold) {
        sc->respawn_count = 0; /* reset cap counter after sustained health */
        sc->backoff_sec = 0;   /* reset backoff on recovery */
    }
    return 1;
}

static void do_spawn(memory_sidecar_t *sc, relay_log_t *log)
{
    if (!sc->autostart || sc->proc.spawn_bg == NULL) {
        return;
    }
    if (sc->script_path[0] == '\0') {
        log_write(log, LOG_WARN,
                  "[memory] autostart=1 but memory_service_script not set");
        return;
    }

    /* Extract port from service_url (e.g. http://localhost:8766) */
    char port_str[16] = "8765";
    const char *after_scheme = strstr(sc->service_url, "://");
    if (after_scheme) {
        after_scheme += 3; /* skip "://" */
        int port_num = 0;
        char host_tmp[256];
        if (sscanf(after_scheme, "%255[^:/]:%d", host_tmp, &port_num) >= 2) {
            snprintf(port_str, sizeof(port_str), "%d", port_num);
        }
    }

    /* Generate fresh auth token for this spawn */
    generate_auth_token(sc->auth_token, sizeof(sc->auth_token));

    const char *args[] = { sc->script_path,
                            "--data-dir", sc->data_dir,
                            "--port", port_str,
                            "--auth-token", sc->auth_token,
                            NULL };
    pid_t pid = sc->proc.spawn_bg(sc->script_path, args);
    if (pid > 0) {
        sc->sidecar_pid = pid;
        log_write(log, LOG_INFO,
                  "[memory] sidecar spawned (pid=%d)", (int)pid);
    } else {
        log_write(log, LOG_ERROR, "[memory] sidecar spawn failed");
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

memory_sidecar_t *memory_sidecar_create(relay_http_t *http,
                                         const memory_sidecar_proc_t *proc,
                                         const config_t *cfg)
{
    if (!http || !cfg) {
        return NULL;
    }

    memory_sidecar_t *sc = calloc(1, sizeof(memory_sidecar_t));
    if (!sc) {
        return NULL;
    }

    sc->http = http;
    if (proc) {
        sc->proc = *proc;
    }

    snprintf(sc->service_url, sizeof(sc->service_url), "%s",
             config_get(cfg, "memory_service_url", DEFAULT_SERVICE_URL));

    snprintf(sc->script_path, sizeof(sc->script_path), "%s",
             config_get(cfg, "memory_service_script", ""));

    snprintf(sc->data_dir, sizeof(sc->data_dir), "%s",
             config_get(cfg, "workspace_path", "."));

    sc->autostart = config_get_int(cfg, "memory_service_autostart", 0);

    /* Watch interval stored in ms in config; convert to seconds. */
    int watch_ms = config_get_int(cfg, "memory_service_watch_interval_ms",
                                   DEFAULT_WATCH_INTERVAL_SEC * 1000);
    sc->watch_interval_sec = watch_ms / 1000;
    if (sc->watch_interval_sec < 1) {
        sc->watch_interval_sec = 1;
    }

    sc->ingest_interval_sec = config_get_int(cfg,
                               "memory_ingest_interval_sec",
                               DEFAULT_INGEST_INTERVAL_SEC);

    sc->tx_interval_sec = config_get_int(cfg,
                           "memory_transcript_ingest_interval_sec",
                           DEFAULT_TX_INTERVAL_SEC);

    sc->max_respawn = config_get_int(cfg, "memory_max_respawn", 5);

    int backoff_max_ms = config_get_int(cfg, "memory_respawn_backoff_max_ms",
                                         300000);
    sc->backoff_max_sec = backoff_max_ms / 1000;
    if (sc->backoff_max_sec < 1) sc->backoff_max_sec = 1;
    sc->backoff_sec = 0; /* no backoff until first failure */

    sc->stable_threshold = config_get_int(cfg, "memory_stable_threshold", 3);
    if (sc->stable_threshold < 1) sc->stable_threshold = 1;

    /* Generate initial auth token (regenerated on each spawn) */
    generate_auth_token(sc->auth_token, sizeof(sc->auth_token));

    return sc;
}

int memory_sidecar_probe(memory_sidecar_t *sc)
{
    if (!sc) return 0;
    return probe_once(sc);
}

void memory_sidecar_startup(memory_sidecar_t *sc, relay_log_t *log)
{
    if (!sc) return;

    if (probe_once(sc)) {
        log_write(log, LOG_INFO,
                  "[memory] sidecar found at %s", sc->service_url);
        return;
    }

    if (sc->autostart) {
        log_write(log, LOG_INFO,
                  "[memory] sidecar not reachable — spawning");
        do_spawn(sc, log);
    } else {
        log_write(log, LOG_WARN,
                  "[memory] sidecar not reachable and autostart=0 — "
                  "memory search disabled");
    }
}

void memory_sidecar_watch(memory_sidecar_t *sc, time_t now, relay_log_t *log)
{
    if (!sc) return;
    int interval = sc->backoff_sec > 0 ? sc->backoff_sec
                                        : sc->watch_interval_sec;
    if (now - sc->last_watch < interval) return;
    sc->last_watch = now;

    if (probe_once(sc)) return; /* healthy — nothing to do */

    /* Unhealthy */
    if (!sc->autostart) return; /* autostart disabled — leave it */

    /* If we spawned it and it's still alive, just wait */
    if (sc->sidecar_pid > 0 && sc->proc.pid_alive &&
        sc->proc.pid_alive(sc->sidecar_pid)) {
        return; /* still starting up */
    }

    /* Respawn cap: stop trying after max_respawn consecutive failures */
    if (sc->respawn_count >= sc->max_respawn) {
        /* Kill any lingering tracked process; no more retries */
        if (sc->sidecar_pid > 0 && sc->proc.kill_pid) {
            sc->proc.kill_pid(sc->sidecar_pid);
            sc->sidecar_pid = 0;
        }
        return;
    }

    log_write(log, LOG_WARN,
              "[memory] sidecar health check failed — respawning");
    sc->sidecar_pid = 0;
    do_spawn(sc, log);
    sc->respawn_count++;

    /* Exponential backoff: double the interval after each failure */
    if (sc->backoff_sec == 0) {
        sc->backoff_sec = sc->watch_interval_sec * 2;
    } else {
        sc->backoff_sec *= 2;
    }
    if (sc->backoff_sec > sc->backoff_max_sec) {
        sc->backoff_sec = sc->backoff_max_sec;
    }

    if (sc->respawn_count >= sc->max_respawn) {
        log_write(log, LOG_ERROR,
                  "[memory] sidecar failed to start after %d attempts"
                  " — memory disabled", sc->max_respawn);
    }
}

void memory_sidecar_maybe_ingest_logs(memory_sidecar_t *sc, time_t now,
                                       const char *agent_home)
{
    if (!sc || !sc->healthy) return;
    if (now - sc->last_ingest < sc->ingest_interval_sec) return;
    sc->last_ingest = now;

    char url[RELAY_MAX_URL];
    char body[RELAY_MAX_PATH + 64];
    char resp[512];
    build_auth_url(sc, "/ingest_daily_logs", url, sizeof(url));
    snprintf(body, sizeof(body), "{\"agent_id\":\"%s\"}",
             agent_home ? agent_home : "");
    /* Fire-and-forget with a short timeout; ignore result. */
    sc->http->post(url, body, resp, sizeof(resp));
}

void memory_sidecar_maybe_ingest_transcripts(memory_sidecar_t *sc, time_t now,
                                              const char *agent_home)
{
    if (!sc || !sc->healthy) return;
    if (now - sc->last_tx_ingest < sc->tx_interval_sec) return;
    sc->last_tx_ingest = now;

    char url[RELAY_MAX_URL];
    char body[RELAY_MAX_PATH + 64];
    char resp[512];
    build_auth_url(sc, "/ingest_transcripts", url, sizeof(url));
    snprintf(body, sizeof(body), "{\"agent_id\":\"%s\"}",
             agent_home ? agent_home : "");
    sc->http->post(url, body, resp, sizeof(resp));
}

void memory_sidecar_stop(memory_sidecar_t *sc)
{
    if (!sc || sc->sidecar_pid <= 0) return;
    if (sc->proc.kill_pid) {
        sc->proc.kill_pid(sc->sidecar_pid);
    }
    sc->sidecar_pid = 0;
}

int memory_sidecar_is_healthy(const memory_sidecar_t *sc)
{
    return sc ? sc->healthy : 0;
}

void memory_sidecar_free(memory_sidecar_t *sc)
{
    free(sc);
}
