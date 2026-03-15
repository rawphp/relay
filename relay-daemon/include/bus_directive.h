#ifndef RELAY_BUS_DIRECTIVE_H
#define RELAY_BUS_DIRECTIVE_H

#include <stddef.h>

/* Process AGENT_BUS_SEND directives in an LLM response.
 *
 * Scans `text` for patterns like:
 *   [AGENT_BUS_SEND to=<name>] <message>
 *
 * For each directive found:
 * - Looks up the target in the peer registry
 * - If found and alive, sends via agent_bus_send()
 * - Strips the directive from the output
 * - Appends a status note (e.g. "(sent message to ash)")
 *
 * Writes the cleaned text into `out` (up to out_max bytes).
 * Returns the number of directives processed. */
int bus_directive_process(const char *text, char *out, size_t out_max,
                          const char *self_name, const char *self_socket);

#endif /* RELAY_BUS_DIRECTIVE_H */
