#ifndef RELAY_BUS_DEAD_DROP_H
#define RELAY_BUS_DEAD_DROP_H

#include "agent_bus.h"
#include <stddef.h>

/* ── Dead Drop Inbox — persist bus messages for offline agents ────────────
 *
 * When a bus send fails (target daemon down), the message is written to
 * ~/.relay.d/inbox/{target}/pending.jsonl. The target daemon processes
 * these on startup.
 * ─────────────────────────────────────────────────────────────────────── */

/* Save a message to the dead drop for target_name.
 * Creates the inbox directory structure if needed.
 * Returns RELAY_OK or RELAY_ERR. */
int bus_dead_drop_save(const char *ad_dir, const char *target_name,
                       const agent_bus_message_t *msg);

/* Load pending messages for self_name into a formatted catch-up block.
 * Writes into out (up to max bytes).
 * Returns the number of pending messages (0 = none). */
int bus_dead_drop_load(const char *ad_dir, const char *self_name,
                       char *out, size_t max);

/* Clear the dead drop inbox for self_name.
 * Returns RELAY_OK or RELAY_ERR_NOTFOUND. */
int bus_dead_drop_clear(const char *ad_dir, const char *self_name);

#endif /* RELAY_BUS_DEAD_DROP_H */
