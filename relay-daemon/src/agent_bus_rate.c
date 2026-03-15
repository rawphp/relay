#include "agent_bus_rate.h"

#include <string.h>

void bus_rate_init(bus_rate_limiter_t *rl, int max_per_sec)
{
    if (!rl) return;
    memset(rl, 0, sizeof(*rl));
    if (max_per_sec < 1) max_per_sec = 1;
    if (max_per_sec > BUS_RATE_MAX) max_per_sec = BUS_RATE_MAX;
    rl->max_per_sec = max_per_sec;
}

int bus_rate_allow(bus_rate_limiter_t *rl, time_t now)
{
    if (!rl) return 0;

    /* The circular buffer holds the last max_per_sec timestamps.
     * If the oldest entry in the window is within the same second,
     * we've exceeded the rate. */
    int oldest_idx = (rl->head + BUS_RATE_MAX - rl->max_per_sec) % BUS_RATE_MAX;
    time_t oldest = rl->timestamps[oldest_idx];

    if (oldest > 0 && (now - oldest) < 1) {
        return 0; /* rate limited */
    }

    /* Record this connection */
    rl->timestamps[rl->head] = now;
    rl->head = (rl->head + 1) % BUS_RATE_MAX;
    return 1;
}
