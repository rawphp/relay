#ifndef RELAY_STREAM_TIMEOUT_H
#define RELAY_STREAM_TIMEOUT_H

#include <time.h>

/* ── stream_timeout ─────────────────────────────────────────────────── */
/* Helpers for the per-idle-read sub-timeout in proc_spawn_streaming.   */
/*                                                                       */
/* Without an idle timeout, a streaming session that emits one token    */
/* every 119 seconds never trips a 120-second wall-clock limit.         */
/* compute_select_timeout() returns the tighter of:                     */
/*   - remaining wall-clock time (deadline - now)                       */
/*   - remaining idle window (last_data + idle_sec - now)               */
/* The caller passes the smaller value to select().                     */

/* Seconds of inactivity after the last received byte before the idle
 * sub-timeout fires.  120 s gives long tool calls (e.g. `make test`)
 * time to complete before producing output.  30 s was killing user queries
 * (observed 19:08 and 19:15 2026-03-01 with ~108 s and ~92 s wall times);
 * tool execution pauses emit no streaming bytes. */
#define RELAY_STREAM_IDLE_TIMEOUT_SEC 120

/*
 * compute_select_timeout — return the select() timeout in whole seconds.
 *
 *   deadline   : wall-clock UNIX time when the overall session expires
 *   last_data  : wall-clock UNIX time when the last byte was received
 *   idle_sec   : idle-read sub-timeout (e.g. RELAY_STREAM_IDLE_TIMEOUT_SEC)
 *   now        : current UNIX time
 *
 * Returns the smaller of the two remaining windows, clamped to [0, INT_MAX].
 * A return value of 0 means at least one limit has already expired.
 */
int compute_select_timeout(time_t deadline, time_t last_data,
                           int idle_sec, time_t now);

/*
 * stream_idle_expired — returns 1 if the idle window has elapsed, else 0.
 */
int stream_idle_expired(time_t last_data, int idle_sec, time_t now);

#endif /* RELAY_STREAM_TIMEOUT_H */
