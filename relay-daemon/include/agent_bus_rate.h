#ifndef RELAY_AGENT_BUS_RATE_H
#define RELAY_AGENT_BUS_RATE_H

#include <time.h>

/* Simple token-bucket rate limiter for agent bus connections.
 *
 * Tracks the last N accept timestamps in a circular buffer.
 * A new connection is allowed only if the oldest timestamp in the
 * window is more than 1 second ago (i.e., fewer than max_per_sec
 * connections in the last second). */

#define BUS_RATE_MAX 64 /* max configurable rate per second */

typedef struct {
    time_t timestamps[BUS_RATE_MAX];
    int    head;         /* next write position */
    int    max_per_sec;  /* configured limit */
} bus_rate_limiter_t;

/* Initialize the rate limiter.  max_per_sec <= BUS_RATE_MAX. */
void bus_rate_init(bus_rate_limiter_t *rl, int max_per_sec);

/* Check whether a connection at time `now` should be allowed.
 * Returns 1 if allowed (and records the timestamp), 0 if rate-limited. */
int bus_rate_allow(bus_rate_limiter_t *rl, time_t now);

#endif /* RELAY_AGENT_BUS_RATE_H */
