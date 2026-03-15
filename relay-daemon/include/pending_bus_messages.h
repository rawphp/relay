#ifndef RELAY_PENDING_BUS_MESSAGES_H
#define RELAY_PENDING_BUS_MESSAGES_H

#include "agent_bus.h"
#include <stddef.h>

/**
 * Append a failed agent bus message to the pending queue.
 * File: {workspace}/data/pending_bus_messages.jsonl
 * Automatically trims to the most recent 20 entries.
 */
void pending_bus_save(const char *workspace, const agent_bus_message_t *msg);

/**
 * Read all queued messages and format a catch-up prompt prefix into buf.
 * Returns 1 if there were queued messages, 0 if the queue is empty/missing.
 * On return 0, buf is set to "".
 */
int pending_bus_load(const char *workspace, char *buf, size_t buf_len);

/**
 * Delete the pending queue file (call after a successful LLM response).
 * Safe to call even when the file does not exist.
 */
void pending_bus_clear(const char *workspace);

#endif /* RELAY_PENDING_BUS_MESSAGES_H */
