#ifndef RELAY_PENDING_RESPONSE_H
#define RELAY_PENDING_RESPONSE_H

#include <stddef.h>

/*
 * pending_response — crash-recovery journal for in-flight user responses.
 *
 * Before spawning a worker thread to respond to a user message, call
 * pending_response_write().  On completion (success or error), call
 * pending_response_delete().
 *
 * On daemon start, call pending_response_load().  If it returns 1, a previous
 * worker was killed mid-response.  Send the user a recovery prompt and delete
 * the record.
 *
 * File location: <workspace>/data/state/pending-response.json
 * Format: {"chat_id":"<id>","text":"<escaped text>","ts":<unix timestamp>}
 */

/* Write (or overwrite) the pending-response record. No-op if workspace is NULL. */
void pending_response_write(const char *workspace,
                            const char *chat_id,
                            const char *text);

/* Delete the pending-response record.  No-op if file doesn't exist. */
void pending_response_delete(const char *workspace);

/*
 * Load the pending-response record.
 * Returns 1 and populates chat_id_out / text_out if the file exists and is valid.
 * Returns 0 if no file found or parse fails.
 */
int pending_response_load(const char *workspace,
                          char *chat_id_out, size_t chat_id_len,
                          char *text_out,    size_t text_len);

#endif /* RELAY_PENDING_RESPONSE_H */
