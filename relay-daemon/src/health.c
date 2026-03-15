#include "health.h"

#include <stdlib.h>

/* ── Internal types ─────────────────────────────────────────────────── */

#define MAX_BACKOFF 300

struct health {
    int failures[HEALTH_COUNT];
    int threshold;
};

/* ── Public API ─────────────────────────────────────────────────────── */

health_t *health_create(int threshold)
{
    health_t *h = calloc(1, sizeof(health_t));
    if (!h) {
        return NULL;
    }
    h->threshold = threshold > 0 ? threshold : 5;
    return h;
}

void health_failure(health_t *h, health_component_t component)
{
    if (!h || component < 0 || component >= HEALTH_COUNT) {
        return;
    }
    h->failures[component]++;
}

void health_success(health_t *h, health_component_t component)
{
    if (!h || component < 0 || component >= HEALTH_COUNT) {
        return;
    }
    h->failures[component] = 0;
}

int health_failures(health_t *h, health_component_t component)
{
    if (!h || component < 0 || component >= HEALTH_COUNT) {
        return 0;
    }
    return h->failures[component];
}

int health_is_persistent(health_t *h, health_component_t component)
{
    if (!h || component < 0 || component >= HEALTH_COUNT) {
        return 0;
    }
    return h->failures[component] >= h->threshold;
}

int health_backoff(health_t *h, health_component_t component)
{
    if (!h || component < 0 || component >= HEALTH_COUNT) {
        return 1;
    }

    int n = h->failures[component];
    int delay = 1;

    for (int i = 0; i < n && delay < MAX_BACKOFF; i++) {
        delay *= 2;
    }

    return delay > MAX_BACKOFF ? MAX_BACKOFF : delay;
}

void health_free(health_t *h)
{
    free(h);
}
