#ifndef RELAY_MEMORY_SEARCH_H
#define RELAY_MEMORY_SEARCH_H

#include "relay.h"
#include "config.h"

/* ── Semantic Memory Search ────────────────────────────────────────── */
/* Calls the search-memory CLI to retrieve relevant memories for a     */
/* given query. Designed to be fail-open: any error returns 0 results  */
/* and the caller proceeds without memory context.                     */

/* Max size for memory context injected into messages.
 * 4KB is ~1000 tokens — enough for 3 search results. */
#define RELAY_MAX_MEMORY_CONTEXT 4096

/* Opaque memory search type */
typedef struct memory_search memory_search_t;

/* Create memory searcher from config.
 * Reads: workspace_path, memory_search_enabled, memory_search_top_k,
 *        memory_search_threshold, memory_search_timeout, memory_search_url.
 * http may be NULL; if memory_search_url is set AND http is non-NULL the
 * HTTP path is used instead of spawning a subprocess (much faster).
 * Returns NULL on allocation failure or if disabled. */
memory_search_t *memory_search_create(relay_proc_t *proc, relay_http_t *http,
                                       const config_t *cfg);

/* Search for memories relevant to the query.
 * Writes formatted context string to `context` (max `context_size`).
 * Returns number of results found (0 = no results or error).
 * On failure/timeout, context[0] = '\0' and returns 0 (fail-open). */
int memory_search_query(memory_search_t *ms, const char *query,
                        char *context, size_t context_size);

/* Hot-reload config values. Safe to call while running. */
void memory_search_update_config(memory_search_t *ms, const config_t *cfg);

/* Free resources. Safe to call with NULL. */
void memory_search_free(memory_search_t *ms);

#endif /* RELAY_MEMORY_SEARCH_H */
