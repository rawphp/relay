#ifndef RELAY_EVENT_LOOP_H
#define RELAY_EVENT_LOOP_H

#include "relay.h"
#include "config.h"
#include "llm_provider.h"
#include "session.h"
#include "telegram.h"
#include "health.h"
#include "transcript.h"
#include "log.h"
#include "vision.h"
#include "memory_sidecar.h"
#include "memory_curator.h"

/* ── Event Loop ─────────────────────────────────────────────────────── */
/* kqueue-based event loop that ties everything together.               */

typedef struct event_loop event_loop_t;

/* All dependencies for the event loop */
typedef struct {
    telegram_t *telegram;
    llm_provider_t *llm;
    session_store_t *sessions;
    health_t *health;
    transcript_t *transcript;
    relay_log_t *log;
    relay_http_t *http;
    vision_t *vision;           /* Optional: local image description (may be NULL) */
    memory_sidecar_t *sidecar;   /* Optional: memory sidecar lifecycle (may be NULL) */
    memory_curator_t *curator;   /* Optional: memory curation pipeline (may be NULL) */
    config_t *cfg;
    relay_proc_t *proc;         /* Process spawner (for voice pipeline etc.) */
    const char *config_path;    /* For config reload */
} event_loop_deps_t;

/* Create event loop with all wired dependencies. */
event_loop_t *event_loop_create(event_loop_deps_t *deps);

/* Run the event loop (blocks). Returns on signal or error. */
int event_loop_run(event_loop_t *loop);

/* Signal the loop to stop. */
void event_loop_stop(event_loop_t *loop);

/* Request config reload (called from signal handler). */
void event_loop_request_reload(event_loop_t *loop);

/* Free event loop. */
void event_loop_free(event_loop_t *loop);

#endif /* RELAY_EVENT_LOOP_H */
