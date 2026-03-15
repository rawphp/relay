#include "peer_registry.h"
#include "relay.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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

int peer_registry_probe(int index)
{
    if (index < 0 || index >= g_peer_count) return 0;

    peer_entry_t *p = &g_peers[index];
    p->is_alive = 0;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    /* Non-blocking connect with short timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", p->socket_path);

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        /* Connected immediately */
        p->is_alive = 1;
    } else if (errno == EINPROGRESS) {
        /* Wait briefly for connection to complete */
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 }; /* 500ms */
        if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
            int so_err = 0;
            socklen_t len = sizeof(so_err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len);
            if (so_err == 0) p->is_alive = 1;
        }
    }

    close(fd);
    return p->is_alive;
}

void peer_registry_probe_all(void)
{
    for (int i = 0; i < g_peer_count; i++) {
        peer_registry_probe(i);
    }
}

int peer_registry_build_context(char *buf, size_t max)
{
    if (!buf || max == 0) return 0;
    buf[0] = '\0';

    if (g_peer_count == 0) return 0;

    int off = 0;
    off += snprintf(buf + off, max - (size_t)off,
        "\n## Peer Agents on the Bus\n\n"
        "You can communicate with other agents via the agent bus. "
        "To send a message, include a directive in your response:\n"
        "[AGENT_BUS_SEND to=<name>] <message text>\n\n"
        "Available agents:\n");

    for (int i = 0; i < g_peer_count && (size_t)off < max - 1; i++) {
        const peer_entry_t *p = &g_peers[i];
        off += snprintf(buf + off, max - (size_t)off,
            "- %s (%s)\n",
            p->name, p->is_alive ? "online" : "offline");
    }

    off += snprintf(buf + off, max - (size_t)off,
        "\nOnly online agents will receive your message.\n");

    return off;
}

const peer_entry_t *peer_registry_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < g_peer_count; i++) {
        if (strcmp(g_peers[i].name, name) == 0) {
            return &g_peers[i];
        }
    }
    return NULL;
}

void peer_registry_destroy(void)
{
    g_peer_count = 0;
    memset(g_peers, 0, sizeof(g_peers));
}
