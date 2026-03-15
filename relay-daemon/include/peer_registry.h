#ifndef RELAY_PEER_REGISTRY_H
#define RELAY_PEER_REGISTRY_H

/* ── Peer Registry — discover other relay agents via ~/.relay ────────────
 *
 * Reads the agent registry file (one "name=home_path" per line) and
 * exposes a list of known peers with derived socket paths.
 * ─────────────────────────────────────────────────────────────────────── */

#define PEER_REGISTRY_MAX 16

typedef struct {
    char name[64];              /* Agent slug (e.g. "ash") */
    char home_path[512];        /* Install directory (e.g. "/Users/tom/ash") */
    char socket_path[512];      /* Derived bus socket (e.g. ".../data/relay.sock") */
    int  is_alive;              /* 1 = last probe succeeded, 0 = unreachable */
} peer_entry_t;

/* Read registry_path, parse entries, exclude self_name.
 * Returns RELAY_OK always (missing file = 0 peers, not an error). */
int peer_registry_init(const char *registry_path, const char *self_name);

/* Number of discovered peers (excluding self). */
int peer_registry_count(void);

/* Get peer by index. Returns NULL if out of bounds. */
const peer_entry_t *peer_registry_get(int index);

/* Probe a single peer's socket for liveness.
 * Returns 1 if reachable, 0 if not. Updates is_alive on the entry. */
int peer_registry_probe(int index);

/* Probe all peers and update their is_alive status. */
void peer_registry_probe_all(void);

/* Release internal state. */
void peer_registry_destroy(void);

#endif /* RELAY_PEER_REGISTRY_H */
