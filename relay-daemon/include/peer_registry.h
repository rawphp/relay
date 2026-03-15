#ifndef RELAY_PEER_REGISTRY_H
#define RELAY_PEER_REGISTRY_H

/* ── Peer Registry — discover other relay agents via ~/.relay.d/ ─────────
 *
 * Scans the advertisement directory for JSON files written by running
 * agents. Each file contains name, PID, socket path, and start time.
 * ─────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include <sys/types.h>

#define PEER_REGISTRY_MAX 16

typedef struct {
    char name[64];              /* Agent slug (e.g. "ash") */
    char socket_path[512];      /* Bus socket (from advertisement JSON) */
    pid_t pid;                  /* Advertised PID for liveness check */
    int  is_alive;              /* 1 = last probe succeeded, 0 = unreachable */
} peer_entry_t;

/* Scan ad_dir for *.json advertisement files, exclude self_name.
 * Stale entries (dead PID) are automatically cleaned up.
 * Returns RELAY_OK always (missing dir = 0 peers, not an error). */
int peer_registry_init(const char *ad_dir, const char *self_name);

/* Number of discovered peers (excluding self). */
int peer_registry_count(void);

/* Get peer by index. Returns NULL if out of bounds. */
const peer_entry_t *peer_registry_get(int index);

/* Probe a single peer for liveness.
 * Uses kill(pid, 0) as primary check, socket connect as fallback.
 * Returns 1 if alive, 0 if not. Updates is_alive on the entry. */
int peer_registry_probe(int index);

/* Probe all peers and update their is_alive status. */
void peer_registry_probe_all(void);

/* Build a peer awareness context block for LLM injection.
 * Writes into buf (up to max bytes). Returns number of bytes written
 * (excluding NUL), or 0 if no peers are available. */
int peer_registry_build_context(char *buf, size_t max);

/* Look up a peer by name. Returns pointer or NULL if not found. */
const peer_entry_t *peer_registry_find(const char *name);

/* Release internal state. */
void peer_registry_destroy(void);

#endif /* RELAY_PEER_REGISTRY_H */
