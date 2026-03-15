#ifndef RELAY_HEALTH_H
#define RELAY_HEALTH_H

/* ── Health Monitor ─────────────────────────────────────────────────── */
/* Tracks consecutive failures per component. Exponential backoff.       */

/* Component identifiers */
typedef enum {
    HEALTH_TELEGRAM = 0,
    HEALTH_LLM      = 1,
    HEALTH_CLAUDE   = HEALTH_LLM,  /* backward compatibility */
    HEALTH_HEARTBEAT = 2,
    HEALTH_COUNT    = 3
} health_component_t;

/* Opaque health type */
typedef struct health health_t;

/* Create health monitor. threshold = failures before "persistent". */
health_t *health_create(int threshold);

/* Record a failure for a component. */
void health_failure(health_t *h, health_component_t component);

/* Record a success (resets counter). */
void health_success(health_t *h, health_component_t component);

/* Get consecutive failure count. */
int health_failures(health_t *h, health_component_t component);

/* Check if component has persistent failure (>= threshold). */
int health_is_persistent(health_t *h, health_component_t component);

/* Get backoff delay in seconds (exponential: 2^failures, capped at 300). */
int health_backoff(health_t *h, health_component_t component);

/* Free health monitor. */
void health_free(health_t *h);

#endif /* RELAY_HEALTH_H */
