#ifndef RELAY_MEMORY_CURATOR_H
#define RELAY_MEMORY_CURATOR_H

#include "relay.h"
#include "config.h"
#include "llm_provider.h"

/* ── memory_curator ──────────────────────────────────────────────────── */
/* Two-stage LLM curation pipeline (mem0 pattern).                       */
/*                                                                        */
/* Stage 1: Fact extraction — after every N conversation turns, collect  */
/*          last N message pairs and send to LLM to extract atomic facts.*/
/*                                                                        */
/* Stage 2: Memory decision — for each candidate fact, search the memory */
/*          index for top-5 similar entries, then ask LLM to decide:     */
/*          ADD / UPDATE / DELETE / NONE.  Operations with confidence ≥  */
/*          0.7 are applied immediately via the sidecar HTTP API.        */
/*          Operations with confidence < 0.7 are written to              */
/*          data/memory/review_queue.jsonl for human review.             */

typedef struct memory_curator memory_curator_t;

/* Create a curator.
 * llm, http, and fs are borrowed (not freed by memory_curator_free).
 * Returns NULL on allocation failure. */
memory_curator_t *memory_curator_create(llm_provider_t *llm,
                                         relay_http_t   *http,
                                         relay_fs_t     *fs,
                                         const config_t *cfg);

/* Record a conversation turn and auto-flush when the turn count reaches
 * memory_flush_every_n_turns.  agent_home is the relay runtime home
 * directory (e.g. ~/relay) used to locate review_queue.jsonl. */
void memory_curator_on_turn(memory_curator_t *mc,
                             const char *user_msg,
                             const char *agent_msg,
                             const char *agent_home);

/* Manually trigger a full curation flush (Stage 1 + Stage 2).
 * Fail-open: any LLM or HTTP error is logged and skipped, not returned.
 * Returns RELAY_OK on success, RELAY_ERR if a fatal allocation failure
 * prevented the flush entirely. */
int memory_curator_flush(memory_curator_t *mc, const char *agent_home);

/* Free all resources. Safe to call with NULL. */
void memory_curator_free(memory_curator_t *mc);

#endif /* RELAY_MEMORY_CURATOR_H */
