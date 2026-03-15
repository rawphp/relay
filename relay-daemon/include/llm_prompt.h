#ifndef RELAY_LLM_PROMPT_H
#define RELAY_LLM_PROMPT_H

#include <stddef.h>

/* Build the standard system prompt with time context and user message.
 * Used by providers that prepend context to the message (Gemini, Codex).
 * Uses the process TZ (set at startup from config "timezone"). */
void llm_build_system_prompt(char *buf, size_t max,
                          const char *workspace, const char *message,
                          const char *agent_name, const char *user_name);

/* Load the memory compaction prompt from
 * {workspace}/data/prompts/compaction.txt.  Substitutes {date} with
 * date_str (e.g. "2026-03-01").  Falls back to a hardcoded default when
 * the file is absent or empty. */
void llm_load_compaction_prompt(char *buf, size_t max,
                                const char *workspace,
                                const char *date_str);

#endif /* RELAY_LLM_PROMPT_H */
