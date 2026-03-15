#ifndef RELAY_GROUP_CHAT_CONTEXT_H
#define RELAY_GROUP_CHAT_CONTEXT_H

#include <stddef.h>

/**
 * Load recent group chat context from {workspace}/data/recent_group_chat.txt.
 *
 * Returns 1 if context was loaded (file exists, non-empty, and < 24 hours
 * old), 0 otherwise.  On success, `buf` contains the NUL-terminated file
 * contents (truncated to at most buf_len-1 bytes).  On failure, `buf` is
 * left unchanged.
 */
int group_chat_context_load(const char *workspace, char *buf, size_t buf_len);

#endif /* RELAY_GROUP_CHAT_CONTEXT_H */
