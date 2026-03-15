#ifndef RELAY_LLM_FORMAT_H
#define RELAY_LLM_FORMAT_H

#include <stddef.h>

/* Appends standardized formatting guidance to an existing prompt string.
 * buf must already contain the prompt text; this function strncat-appends
 * instructions that tell the model to use structured output (headers,
 * bullet points, code blocks). max is the total buffer capacity including
 * the existing content. No-ops if buf is NULL or already full. */
void llm_append_structured_format_instructions(char *buf, size_t max);

#endif /* RELAY_LLM_FORMAT_H */
