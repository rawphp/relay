#ifndef RELAY_TRANSCRIPT_H
#define RELAY_TRANSCRIPT_H

#include "relay.h"

/* ── Transcript Logger ──────────────────────────────────────────────── */
/* JSONL conversation logging with daily file rotation.                  */

/* Opaque transcript type */
typedef struct transcript transcript_t;

/* Create transcript logger. */
transcript_t *transcript_create(relay_fs_t *fs, relay_clock_t *clock,
                                 const char *dir);

/* Log an inbound message. */
int transcript_log_inbound(transcript_t *tx, const char *chat_id,
                           const char *text);

/* Log an outbound response. Pass message_id > 0 to persist the Telegram
 * message_id for later reaction lookup; pass 0 if not applicable. */
int transcript_log_outbound(transcript_t *tx, const char *chat_id,
                            const char *text, const char *session_id,
                            int duration_ms, int message_id);

/* Log a Telegram reaction. reacted_to may be NULL if the message text is
 * unknown. */
int transcript_log_reaction(transcript_t *tx, const char *chat_id,
                            const char *emoji, const char *reacted_to);

/* Find an outbound message's text by its Telegram message_id. Scans today's
 * and yesterday's transcript files. Returns 1 and fills text_out on success,
 * 0 if not found. */
int transcript_find_by_message_id(transcript_t *tx, int message_id,
                                  char *text_out, size_t max);

/* Log a probe outcome event. response_time_sec < 0 means N/A. */
int transcript_log_probe_outcome(transcript_t *tx, const char *chat_id,
                                  const char *thread_ts, const char *probe_ts,
                                  const char *outcome, int response_time_sec);

/* Read the most recent N entries for a chat_id from today's transcript.
 * Writes a human-readable conversation snippet into buf.
 * Returns RELAY_OK on success (empty transcript = OK with empty buf). */
int transcript_read_recent(transcript_t *tx, const char *chat_id,
                            int max_entries, char *buf, size_t buf_max);

/* Free transcript logger. */
void transcript_free(transcript_t *tx);

#endif /* RELAY_TRANSCRIPT_H */
