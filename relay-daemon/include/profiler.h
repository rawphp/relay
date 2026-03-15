#ifndef RELAY_PROFILER_H
#define RELAY_PROFILER_H

#include "relay.h"

#include <time.h>

typedef struct {
    struct timespec start;
} profiler_timer_t;

int profiler_init(const char *path, int enabled);
void profiler_close(void);
int profiler_enabled(void);

void profiler_set_context(const char *request_id, const char *chat_id,
                          const char *provider);
void profiler_clear_context(void);

void profiler_timer_start(profiler_timer_t *timer);
long profiler_timer_elapsed_ms(const profiler_timer_t *timer);

void profiler_emit_event(const char *stage, long duration_ms,
                         const char *status, const char *detail);
void profiler_emit_event_ctx(const char *request_id, const char *chat_id,
                             const char *provider, const char *stage,
                             long duration_ms, const char *status,
                             const char *detail);

#endif /* RELAY_PROFILER_H */
