#ifndef RELAY_SESSION_DISCOVERY_H
#define RELAY_SESSION_DISCOVERY_H

#include "relay.h"
#include <time.h>

/* A discovered Claude Code session from ~/.claude/projects/ */
typedef struct {
    char session_id[RELAY_MAX_SESSION_ID];
    char summary[81];   /* First user message, truncated */
    time_t last_activity;
} relay_cc_session_t;

/* Scan ~/.claude/projects/<encoded-workspace>/ for .jsonl session files.
 * workspace_path: the absolute workspace path (e.g. "/Users/tom/project")
 * home: the user's home directory (for resolving ~/.claude/)
 * out: array to fill with discovered sessions
 * max: size of the out array
 * count: output — number of sessions found
 * Returns RELAY_OK always (missing dirs → count=0, not an error). */
int session_discovery_scan(relay_fs_t *fs, const char *workspace_path,
                           const char *home,
                           relay_cc_session_t *out, int max, int *count);

#endif /* RELAY_SESSION_DISCOVERY_H */
