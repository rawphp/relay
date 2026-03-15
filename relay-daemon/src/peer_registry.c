#include "peer_registry.h"
#include "relay.h"

#include <cJSON/cJSON.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ── Module state ──────────────────────────────────────────────────────── */

static peer_entry_t g_peers[PEER_REGISTRY_MAX];
static int          g_peer_count = 0;

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Check if a PID is alive via kill(pid, 0) */
static int pid_is_alive(pid_t pid)
{
    if (pid <= 0) return 0;
    return (kill(pid, 0) == 0) ? 1 : 0;
}

/* Read a file into a malloc'd buffer. Caller must free. */
static char *read_file_contents(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 8192) { fclose(f); return NULL; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* ── Public API ────────────────────────────────────────────────────────── */

int peer_registry_init(const char *ad_dir, const char *self_name)
{
    g_peer_count = 0;
    memset(g_peers, 0, sizeof(g_peers));

    if (!ad_dir) return RELAY_OK;

    DIR *d = opendir(ad_dir);
    if (!d) return RELAY_OK; /* missing dir = 0 peers */

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && g_peer_count < PEER_REGISTRY_MAX) {
        /* Only process .json files */
        size_t nlen = strlen(ent->d_name);
        if (nlen < 6 || strcmp(ent->d_name + nlen - 5, ".json") != 0) continue;

        /* Read the file */
        char path[RELAY_MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", ad_dir, ent->d_name);

        char *content = read_file_contents(path);
        if (!content) continue;

        /* Parse JSON */
        cJSON *root = cJSON_Parse(content);
        free(content);
        if (!root) continue;

        cJSON *j_name   = cJSON_GetObjectItem(root, "name");
        cJSON *j_pid    = cJSON_GetObjectItem(root, "pid");
        cJSON *j_socket = cJSON_GetObjectItem(root, "socket");

        if (!cJSON_IsString(j_name) || !cJSON_IsNumber(j_pid) ||
            !cJSON_IsString(j_socket)) {
            cJSON_Delete(root);
            continue;
        }

        const char *name = j_name->valuestring;
        pid_t pid = (pid_t)j_pid->valuedouble;
        const char *sock = j_socket->valuestring;

        /* Skip self */
        if (self_name && strcmp(name, self_name) == 0) {
            cJSON_Delete(root);
            continue;
        }

        /* Check PID liveness — clean up stale entries */
        if (!pid_is_alive(pid)) {
            unlink(path);
            cJSON_Delete(root);
            continue;
        }

        /* Store entry */
        peer_entry_t *p = &g_peers[g_peer_count];
        snprintf(p->name, sizeof(p->name), "%s", name);
        snprintf(p->socket_path, sizeof(p->socket_path), "%s", sock);
        p->pid = pid;
        p->is_alive = 1; /* PID is alive, assume bus is too */

        g_peer_count++;
        cJSON_Delete(root);
    }

    closedir(d);
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

    /* Primary: PID check (instant, no I/O) */
    if (!pid_is_alive(p->pid)) {
        p->is_alive = 0;
        return 0;
    }

    /* Secondary: socket connect (confirms bus is actually listening) */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        p->is_alive = 1; /* PID alive, can't check socket — assume up */
        return 1;
    }

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
        p->is_alive = 1;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
        if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
            int so_err = 0;
            socklen_t len = sizeof(so_err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len);
            p->is_alive = (so_err == 0) ? 1 : 0;
        } else {
            p->is_alive = 0;
        }
    } else {
        /* PID alive but socket not reachable — bus might be disabled */
        p->is_alive = 0;
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
