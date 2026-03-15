#include "Unity/unity.h"
#include "mocks.h"
#include "memory_sidecar.h"
#include "config.h"
#include "log.h"

/* ── Mock process-control ────────────────────────────────────────────── */
/* Note: g_mock_proc is already defined in mocks.h (relay_proc_t).
 * We use g_mock_sc_proc to avoid the name collision. */

static int   g_spawn_call_count = 0;
static int   g_kill_call_count  = 0;
static pid_t g_mock_spawned_pid = 42;
static int   g_mock_pid_alive   = 0;

/* Captured spawn args for assertions */
#define MAX_CAPTURED_ARGS 16
static char g_captured_args[MAX_CAPTURED_ARGS][256];
static int  g_captured_arg_count = 0;

static pid_t mock_sc_spawn_bg(const char *bin, const char **args)
{
    (void)bin;
    g_spawn_call_count++;
    g_captured_arg_count = 0;
    if (args) {
        for (int i = 0; args[i] != NULL && i < MAX_CAPTURED_ARGS; i++) {
            snprintf(g_captured_args[i], sizeof(g_captured_args[i]), "%s", args[i]);
            g_captured_arg_count++;
        }
    }
    return g_mock_spawned_pid;
}

static int mock_sc_pid_alive(pid_t pid)
{
    (void)pid;
    return g_mock_pid_alive;
}

static void mock_sc_kill_pid(pid_t pid)
{
    (void)pid;
    g_kill_call_count++;
}

static const memory_sidecar_proc_t g_mock_sc_proc = {
    .spawn_bg  = mock_sc_spawn_bg,
    .pid_alive = mock_sc_pid_alive,
    .kill_pid  = mock_sc_kill_pid,
};

static void reset_mock_sc_proc(void)
{
    g_spawn_call_count   = 0;
    g_kill_call_count    = 0;
    g_mock_spawned_pid   = 42;
    g_mock_pid_alive     = 0;
    g_captured_arg_count = 0;
}

/* ── Config helpers ──────────────────────────────────────────────────── */

static config_t *make_sc_cfg(int autostart)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = %d\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n",
        autostart);
    return config_load_string(buf);
}

static config_t *make_sc_cfg_with_url(int autostart, const char *url)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = %d\n"
        "memory_service_url = %s\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n",
        autostart, url);
    return config_load_string(buf);
}

static config_t *make_sc_cfg_with_max_respawn(int autostart, int max_respawn)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = %d\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n"
        "memory_max_respawn = %d\n",
        autostart, max_respawn);
    return config_load_string(buf);
}

/* Forward declaration — defined below with stable threshold tests */
static config_t *make_sc_cfg_with_stable(int autostart, int max_respawn,
                                          int stable_threshold);

/* ── Tests ───────────────────────────────────────────────────────────── */

/* Probe healthy → startup does NOT spawn. */
static void test_probe_healthy_skips_spawn(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\",\"index_size\":0}");

    config_t *cfg = make_sc_cfg(1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);

    TEST_ASSERT_EQUAL_INT(0, g_spawn_call_count);
    TEST_ASSERT_EQUAL_INT(1, memory_sidecar_is_healthy(sc));

    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Probe unhealthy + autostart=1 → spawn called once. */
static void test_probe_unhealthy_autostart_spawns(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;

    config_t *cfg = make_sc_cfg(1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);

    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);
    TEST_ASSERT_EQUAL_INT(0, memory_sidecar_is_healthy(sc));

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Probe unhealthy + autostart=0 → spawn NOT called. */
static void test_probe_unhealthy_no_autostart_no_spawn(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;

    config_t *cfg = make_sc_cfg(0);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);

    TEST_ASSERT_EQUAL_INT(0, g_spawn_call_count);
    TEST_ASSERT_EQUAL_INT(0, memory_sidecar_is_healthy(sc));

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* stop() sends SIGTERM to a pid we spawned. */
static void test_stop_kills_spawned_pid(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;

    config_t *cfg = make_sc_cfg(1);
    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    memory_sidecar_stop(sc);
    TEST_ASSERT_EQUAL_INT(1, g_kill_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* watch() respawns when sidecar is dead and interval has elapsed. */
static void test_watch_respawns_dead_sidecar(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    config_t *cfg = make_sc_cfg(1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* Advance past the 1-second watch interval */
    time_t future = time(NULL) + 2;
    memory_sidecar_watch(sc, future, log);

    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* spawn passes --host and --port extracted from memory_service_url */
static void test_spawn_passes_host_and_port(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR; /* force spawn */

    config_t *cfg = make_sc_cfg_with_url(1, "http://localhost:8766");
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);

    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* Find --port in captured args */
    int found_port = 0;
    for (int i = 0; i < g_captured_arg_count - 1; i++) {
        if (strcmp(g_captured_args[i], "--port") == 0) {
            TEST_ASSERT_EQUAL_STRING("8766", g_captured_args[i + 1]);
            found_port = 1;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, found_port, "--port not passed to sidecar");

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* ── Tests: respawn cap (memory_max_respawn) ─────────────────────────── */

/* Respawn attempts under the cap all proceed. */
static void test_watch_respawn_under_cap(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    config_t *cfg = make_sc_cfg_with_max_respawn(1, 3);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* 2 watch calls — both under cap (3) → 2 spawns (with backoff time) */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);   /* spawn 1, backoff→2s */
    t += 3;                              /* 3s > 2s backoff */
    memory_sidecar_watch(sc, t, log);   /* spawn 2 */

    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* After hitting the cap, no further spawns occur. */
static void test_watch_respawn_hits_cap_stops_spawning(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    config_t *cfg = make_sc_cfg_with_max_respawn(1, 2);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* Large time gaps to exceed any backoff; cap=2 → only 2 spawns */
    time_t t = 10;
    for (int i = 0; i < 4; i++) {
        memory_sidecar_watch(sc, t, log);
        t += 100; /* always exceeds backoff */
    }

    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Respawn counter resets after sustained healthy probes — sidecar can recover. */
static void test_watch_respawn_counter_resets_after_recovery(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* stable_threshold defaults to 3; use threshold=1 for this legacy test */
    config_t *cfg = make_sc_cfg_with_stable(1, 3, 1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* 2 failures (counter = 2) — need enough time for backoff */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);      /* spawn 1, backoff→2s */
    t += 3;                                 /* 3s > 2s backoff */
    memory_sidecar_watch(sc, t, log);      /* spawn 2, backoff→4s */
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* Recovery — 1 healthy probe resets counter (threshold=1) */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 5;                                 /* 5s > 4s backoff */
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count); /* no spawn on healthy */
    TEST_ASSERT_EQUAL_INT(1, memory_sidecar_is_healthy(sc));

    /* Fail again — counter and backoff were reset, base interval (1s) applies */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 2;                                 /* 2s > 1s base interval */
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* ── Tests: respawn backoff ───────────────────────────────────────────── */

static config_t *make_sc_cfg_with_backoff(int autostart, int max_respawn,
                                           int backoff_max_ms)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = %d\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n"
        "memory_max_respawn = %d\n"
        "memory_respawn_backoff_max_ms = %d\n",
        autostart, max_respawn, backoff_max_ms);
    return config_load_string(buf);
}

/* After a failed respawn, the next attempt is delayed by 2x the watch interval. */
static void test_backoff_doubles_after_failure(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* watch_interval = 1s, backoff_max = 60s, max_respawn = 10 */
    config_t *cfg = make_sc_cfg_with_backoff(1, 10, 60000);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* First watch call at t=10: should spawn (interval=1s, first attempt) */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);
    /* After first failure, backoff = 2s */

    /* t=11 (1s later): backoff is 2s, only 1s elapsed → no spawn */
    t = 11;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* t=13 (3s from last_watch=10): backoff=2s, 3s elapsed → spawn */
    t = 13;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);
    /* After second failure, backoff = 4s */

    /* t=16 (3s from last_watch=13): backoff is 4s → no spawn */
    t = 16;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* t=18 (5s from last_watch=13): backoff=4s, 5s elapsed → spawn */
    t = 18;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Backoff never exceeds the configured max. */
static void test_backoff_caps_at_max(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* watch_interval=1s, backoff_max=3s, max_respawn=20 */
    config_t *cfg = make_sc_cfg_with_backoff(1, 20, 3000);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* Drive multiple failures so backoff would be 1→2→4→8... but capped at 3 */
    time_t t = 2;

    /* Attempt 1: interval=1s */
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* Attempt 2: backoff=2s */
    t += 3;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* Attempt 3: backoff would be 4s but capped at 3s */
    t += 4;  /* well past 3s cap */
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    /* Attempt 4: backoff still capped at 3s */
    t += 4;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(4, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Backoff resets to base watch interval on recovery. */
static void test_backoff_resets_on_recovery(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* Use backoff_max=60s, stable_threshold=1 so single probe resets */
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = 1\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n"
        "memory_max_respawn = 10\n"
        "memory_respawn_backoff_max_ms = 60000\n"
        "memory_stable_threshold = 1\n");
    config_t *cfg = config_load_string(buf);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* Drive 2 failures to build up backoff */
    time_t t = 2;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);
    t += 3;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* Recovery (threshold=1, so 1 probe resets backoff) */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 5;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, memory_sidecar_is_healthy(sc));

    /* Fail again — backoff should be reset to base (1s), not 4s */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 2;  /* 2s > base 1s → should spawn */
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* ── Tests: stable health threshold ──────────────────────────────────── */

static config_t *make_sc_cfg_with_stable(int autostart, int max_respawn,
                                          int stable_threshold)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "workspace_path = /tmp/test-relay\n"
        "memory_service_autostart = %d\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_service_script = /tmp/test-relay/apps/memory-py/memory_http.py\n"
        "memory_service_watch_interval_ms = 1000\n"
        "memory_max_respawn = %d\n"
        "memory_stable_threshold = %d\n",
        autostart, max_respawn, stable_threshold);
    return config_load_string(buf);
}

/* A single healthy probe does NOT reset respawn_count when threshold > 1 */
static void test_stable_single_healthy_no_reset(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* stable_threshold=3, max_respawn=5 */
    config_t *cfg = make_sc_cfg_with_stable(1, 5, 3);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* Drive 2 failures */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);    /* spawn 1 */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 2 */
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* 1 healthy probe — not enough for threshold=3 */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 100;
    memory_sidecar_watch(sc, t, log);

    /* Fail again — respawn_count was NOT reset, so we're at 3 now */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 3 */
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    /* 2 more failures should hit the cap (5): spawns 4, 5 */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 4 */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 5 — hits cap */
    TEST_ASSERT_EQUAL_INT(5, g_spawn_call_count);

    /* No more spawns — cap reached */
    t += 100;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(5, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* N consecutive healthy probes DO reset respawn_count */
static void test_stable_n_healthy_resets(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* stable_threshold=2, max_respawn=3 */
    config_t *cfg = make_sc_cfg_with_stable(1, 3, 2);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* Drive 2 failures (respawn_count=2, cap=3) */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);
    t += 100;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* 2 healthy probes → meets threshold=2, resets respawn_count */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 100;
    memory_sidecar_watch(sc, t, log);
    t += 100;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, memory_sidecar_is_healthy(sc));

    /* Fail again — respawn_count was reset, so 3 more spawns before cap */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 3 (first after reset) */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 4 */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 5 — hits cap */
    TEST_ASSERT_EQUAL_INT(5, g_spawn_call_count);

    /* Cap hit again — no more spawns */
    t += 100;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(5, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* An unhealthy probe resets stable_count back to 0 */
static void test_stable_unhealthy_resets_stable_count(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    /* stable_threshold=3, max_respawn=10 */
    config_t *cfg = make_sc_cfg_with_stable(1, 10, 3);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);

    /* 1 failure */
    time_t t = 10;
    memory_sidecar_watch(sc, t, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* 2 healthy probes (stable_count=2, threshold=3 → not yet) */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 100;
    memory_sidecar_watch(sc, t, log);
    t += 100;
    memory_sidecar_watch(sc, t, log);

    /* 1 unhealthy probe → stable_count resets to 0 */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 2 */
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* Need 3 more healthy probes to reset respawn_count (not 1 more) */
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\"}");
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* stable=1 */
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* stable=2 */

    /* Fail — still haven't hit threshold */
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    t += 100;
    memory_sidecar_watch(sc, t, log);    /* spawn 3 (respawn_count still accumulating) */
    TEST_ASSERT_EQUAL_INT(3, g_spawn_call_count);

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* ── Tests: bearer token auth ────────────────────────────────────────── */

/* Auth token is generated and passed to sidecar as --auth-token arg */
static void test_spawn_passes_auth_token(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR; /* force spawn */

    config_t *cfg = make_sc_cfg(1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* Find --auth-token in captured args */
    int found_flag = 0;
    for (int i = 0; i < g_captured_arg_count - 1; i++) {
        if (strcmp(g_captured_args[i], "--auth-token") == 0) {
            found_flag = 1;
            /* Token should be at least 32 hex chars */
            TEST_ASSERT_GREATER_OR_EQUAL(32, (int)strlen(g_captured_args[i + 1]));
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, found_flag,
        "--auth-token not passed to sidecar");

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* Health probe URL includes auth token as query parameter */
static void test_probe_url_includes_auth(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    mock_http_set_response("{\"status\":\"ok\",\"index_size\":0}");

    config_t *cfg = make_sc_cfg(0); /* no autostart — just probe */
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    memory_sidecar_probe(sc);

    /* URL should contain ?auth= */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_mock_http_last_url, "?auth="),
        "Health probe URL must include ?auth= query parameter");

    memory_sidecar_free(sc);
    config_free(cfg);
}

/* Auth token is regenerated on each respawn */
static void test_auth_token_regenerated_on_respawn(void)
{
    reset_mock_sc_proc();
    mock_http_reset();
    g_mock_http_status = RELAY_ERR;
    g_mock_pid_alive   = 0;

    config_t *cfg = make_sc_cfg(1);
    TEST_ASSERT_NOT_NULL(cfg);

    memory_sidecar_t *sc = memory_sidecar_create(&g_mock_http, &g_mock_sc_proc, cfg);
    TEST_ASSERT_NOT_NULL(sc);

    relay_log_t *log = log_create(NULL, LOG_INFO, NULL);
    memory_sidecar_startup(sc, log);
    TEST_ASSERT_EQUAL_INT(1, g_spawn_call_count);

    /* Capture first token */
    char first_token[256] = "";
    for (int i = 0; i < g_captured_arg_count - 1; i++) {
        if (strcmp(g_captured_args[i], "--auth-token") == 0) {
            snprintf(first_token, sizeof(first_token), "%s",
                     g_captured_args[i + 1]);
            break;
        }
    }
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(first_token));

    /* Force respawn via watch */
    time_t future = time(NULL) + 100;
    memory_sidecar_watch(sc, future, log);
    TEST_ASSERT_EQUAL_INT(2, g_spawn_call_count);

    /* Capture second token */
    char second_token[256] = "";
    for (int i = 0; i < g_captured_arg_count - 1; i++) {
        if (strcmp(g_captured_args[i], "--auth-token") == 0) {
            snprintf(second_token, sizeof(second_token), "%s",
                     g_captured_args[i + 1]);
            break;
        }
    }
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(second_token));

    /* Tokens must differ */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(first_token, second_token),
        "Auth token must be regenerated on respawn");

    g_mock_http_status = 0;
    memory_sidecar_free(sc);
    config_free(cfg);
    log_close(log);
}

/* ── Suite ───────────────────────────────────────────────────────────── */

void test_memory_sidecar_c_suite(void)
{
    RUN_TEST(test_probe_healthy_skips_spawn);
    RUN_TEST(test_probe_unhealthy_autostart_spawns);
    RUN_TEST(test_probe_unhealthy_no_autostart_no_spawn);
    RUN_TEST(test_stop_kills_spawned_pid);
    RUN_TEST(test_watch_respawns_dead_sidecar);
    RUN_TEST(test_spawn_passes_host_and_port);
    RUN_TEST(test_watch_respawn_under_cap);
    RUN_TEST(test_watch_respawn_hits_cap_stops_spawning);
    RUN_TEST(test_watch_respawn_counter_resets_after_recovery);
    RUN_TEST(test_backoff_doubles_after_failure);
    RUN_TEST(test_backoff_caps_at_max);
    RUN_TEST(test_backoff_resets_on_recovery);
    RUN_TEST(test_stable_single_healthy_no_reset);
    RUN_TEST(test_stable_n_healthy_resets);
    RUN_TEST(test_stable_unhealthy_resets_stable_count);
    /* bearer token auth */
    RUN_TEST(test_spawn_passes_auth_token);
    RUN_TEST(test_probe_url_includes_auth);
    RUN_TEST(test_auth_token_regenerated_on_respawn);
}
