#include "stream_timeout.h"
#include <limits.h>

/*
 * compute_select_timeout — return the smaller of the two remaining windows.
 *
 * wall_remaining = deadline - now
 * idle_remaining = last_data + idle_sec - now
 *
 * Returns 0 if either window has already expired.
 */
int compute_select_timeout(time_t deadline, time_t last_data,
                           int idle_sec, time_t now)
{
    int wall_remaining = (int)(deadline - now);
    int idle_remaining = (int)(last_data + (time_t)idle_sec - now);

    if (wall_remaining <= 0 || idle_remaining <= 0) {
        return 0;
    }

    return wall_remaining < idle_remaining ? wall_remaining : idle_remaining;
}

/*
 * stream_idle_expired — returns 1 if idle_sec seconds have elapsed since
 * last_data, else 0.
 */
int stream_idle_expired(time_t last_data, int idle_sec, time_t now)
{
    return (now - last_data) >= (time_t)idle_sec ? 1 : 0;
}
