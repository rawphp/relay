#include "peer_registry.h"
#include "relay.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ── Module state ──────────────────────────────────────────────────────── */

static peer_entry_t g_peers[PEER_REGISTRY_MAX];
static int          g_peer_count = 0;

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Trim trailing whitespace (newline, carriage return, spaces) in place */
static void trim_trailing(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' '  || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

/* Skip leading whitespace, return pointer into same buffer */
static const char *skip_leading(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* ── Public API ────────────────────────────────────────────────────────── */

int peer_registry_init(const char *registry_path, const char *self_name)
{
    g_peer_count = 0;
    memset(g_peers, 0, sizeof(g_peers));

    if (!registry_path) return RELAY_OK;

    FILE *f = fopen(registry_path, "r");
    if (!f) return RELAY_OK; /* missing file = 0 peers, not an error */

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim_trailing(line);
        const char *trimmed = skip_leading(line);

        /* Skip blank lines and comments */
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

        /* Find the '=' separator */
        const char *eq = strchr(trimmed, '=');
        if (!eq) continue; /* malformed */

        /* Extract name (before '=') */
        size_t name_len = (size_t)(eq - trimmed);
        if (name_len == 0 || name_len >= 64) continue; /* malformed */

        /* Extract path (after '=') */
        const char *path = eq + 1;
        if (path[0] == '\0') continue; /* no path */

        /* Build name string */
        char name[64];
        memcpy(name, trimmed, name_len);
        name[name_len] = '\0';

        /* Skip self */
        if (self_name && strcmp(name, self_name) == 0) continue;

        /* Check capacity */
        if (g_peer_count >= PEER_REGISTRY_MAX) break;

        /* Store entry */
        peer_entry_t *p = &g_peers[g_peer_count];
        snprintf(p->name, sizeof(p->name), "%s", name);
        snprintf(p->home_path, sizeof(p->home_path), "%s", path);
        snprintf(p->socket_path, sizeof(p->socket_path),
                 "%s/data/relay.sock", path);
        p->is_alive = 0;

        g_peer_count++;
    }

    fclose(f);
    return RELAY_OK;
}

int peer_registry_count(void)
{
    return g_peer_count;
}

const peer_entry_t *peer_registry_get(int index)
{
    if (index < 0 || index >= g_peer_count) return NULL;
    return &g_peers[index];
}

void peer_registry_destroy(void)
{
    g_peer_count = 0;
    memset(g_peers, 0, sizeof(g_peers));
}
