#include "agent_bus.h"
#include "agent_bus_rate.h"
#include "relay.h"

#include <cJSON/cJSON.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ── Module state ──────────────────────────────────────────────────────── */

static int  g_listen_fd   = -1;
static char g_socket_path[256];
static bus_rate_limiter_t g_rate_limiter;
static int  g_rate_limiter_initialized = 0;

/* ── Public API ────────────────────────────────────────────────────────── */

int agent_bus_init(const char *socket_path)
{
    if (!socket_path || socket_path[0] == '\0') return RELAY_ERR;

    /* Remove stale socket from a previous crashed instance */
    unlink(socket_path);

    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0) return RELAY_ERR;

    /* Non-blocking so accept() never stalls the main event loop */
    int flags = fcntl(g_listen_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(g_listen_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return RELAY_ERR;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return RELAY_ERR;
    }

    if (listen(g_listen_fd, 8) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return RELAY_ERR;
    }

    /* Owner-only access */
    chmod(socket_path, 0600);

    snprintf(g_socket_path, sizeof(g_socket_path), "%s", socket_path);

    /* Initialize rate limiter with default if not already configured */
    if (!g_rate_limiter_initialized) {
        bus_rate_init(&g_rate_limiter, 10); /* default: 10/sec */
        g_rate_limiter_initialized = 1;
    }

    return RELAY_OK;
}

void agent_bus_set_rate_limit(int max_per_sec)
{
    bus_rate_init(&g_rate_limiter, max_per_sec);
    g_rate_limiter_initialized = 1;
}

int agent_bus_get_fd(void)
{
    return g_listen_fd;
}

int agent_bus_accept_message(agent_bus_message_t *out)
{
    if (g_listen_fd < 0 || !out) return RELAY_ERR;

    /* Non-blocking accept — returns EAGAIN immediately when no client is queued */
    int client_fd = accept(g_listen_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return RELAY_ERR_NOTFOUND;
        return RELAY_ERR;
    }

    /* Rate limiting — reject connections that exceed the limit */
    if (g_rate_limiter_initialized &&
        !bus_rate_allow(&g_rate_limiter, time(NULL))) {
        close(client_fd);
        return RELAY_ERR_FULL;
    }

    /* 2-second read timeout so a slow or malformed sender doesn't hang the loop */
    struct timeval tv;
    tv.tv_sec  = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Read entire message in one shot (messages are small JSON blobs) */
    char buf[4096 + 512];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    close(client_fd);

    if (n <= 0) return RELAY_ERR;
    buf[n] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    if (!root) return RELAY_ERR;

    memset(out, 0, sizeof(*out));

    cJSON *f;
    f = cJSON_GetObjectItem(root, "from");
    if (cJSON_IsString(f)) snprintf(out->from, sizeof(out->from), "%s", f->valuestring);

    f = cJSON_GetObjectItem(root, "from_socket");
    if (cJSON_IsString(f)) snprintf(out->from_socket, sizeof(out->from_socket), "%s", f->valuestring);

    f = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(f)) snprintf(out->text, sizeof(out->text), "%s", f->valuestring);

    f = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsNumber(f)) out->ts = (long long)f->valuedouble;

    f = cJSON_GetObjectItem(root, "msg_id");
    if (cJSON_IsString(f)) snprintf(out->msg_id, sizeof(out->msg_id), "%s", f->valuestring);

    f = cJSON_GetObjectItem(root, "depth");
    if (cJSON_IsNumber(f)) out->depth = (int)f->valuedouble;

    f = cJSON_GetObjectItem(root, "is_autonomous");
    if (cJSON_IsBool(f)) out->is_autonomous = cJSON_IsTrue(f) ? 1 : 0;

    f = cJSON_GetObjectItem(root, "addressed_to");
    if (cJSON_IsString(f)) snprintf(out->addressed_to, sizeof(out->addressed_to), "%s", f->valuestring);

    f = cJSON_GetObjectItem(root, "participants");
    if (cJSON_IsString(f)) snprintf(out->participants, sizeof(out->participants), "%s", f->valuestring);

    cJSON_Delete(root);

    /* Require at minimum a non-empty from and text */
    return (out->from[0] != '\0' && out->text[0] != '\0') ? RELAY_OK : RELAY_ERR;
}

int agent_bus_send(const char *target_socket, const char *from_name,
                   const char *from_socket, const char *text,
                   int depth, int is_autonomous, const char *addressed_to)
{
    if (!target_socket || !from_name || !text) return RELAY_ERR;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return RELAY_ERR;

    /* 2-second connect/send timeout */
    struct timeval tv;
    tv.tv_sec  = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", target_socket);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return RELAY_ERR;
    }

    /* Build JSON payload */
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "from", from_name);
    cJSON_AddStringToObject(obj, "from_socket", from_socket ? from_socket : g_socket_path);
    cJSON_AddStringToObject(obj, "text", text);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char msg_id[64];
    snprintf(msg_id, sizeof(msg_id), "%lld-%d", (long long)time(NULL), (int)getpid());
    cJSON_AddStringToObject(obj, "msg_id", msg_id);
    cJSON_AddNumberToObject(obj, "depth", depth);
    cJSON_AddBoolToObject(obj, "is_autonomous", is_autonomous);
    if (addressed_to && addressed_to[0] != '\0')
        cJSON_AddStringToObject(obj, "addressed_to", addressed_to);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    int rc = RELAY_ERR;
    if (json) {
        ssize_t written = write(fd, json, strlen(json));
        rc = (written > 0) ? RELAY_OK : RELAY_ERR;
        free(json);
    }

    close(fd);
    return rc;
}

void agent_bus_log(const char *log_dir, const char *direction,
                   const agent_bus_message_t *msg, const char *response)
{
    if (!log_dir || !direction || !msg) return;

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "direction", direction);
    cJSON_AddStringToObject(obj, "from", msg->from);
    cJSON_AddStringToObject(obj, "text", msg->text);
    cJSON_AddNumberToObject(obj, "depth", msg->depth);
    cJSON_AddNumberToObject(obj, "ts", (double)(msg->ts ? msg->ts : (long long)time(NULL)));
    cJSON_AddStringToObject(obj, "msg_id", msg->msg_id);
    if (msg->addressed_to[0] != '\0')
        cJSON_AddStringToObject(obj, "addressed_to", msg->addressed_to);
    if (response && response[0] != '\0') {
        cJSON_AddStringToObject(obj, "response", response);
    }

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!line) return;

    char path[RELAY_MAX_PATH];
    snprintf(path, sizeof(path), "%s/agent-bus.jsonl", log_dir);

    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
    free(line);
}

void agent_bus_destroy(void)
{
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_socket_path[0] != '\0') {
        unlink(g_socket_path);
        g_socket_path[0] = '\0';
    }
    g_rate_limiter_initialized = 0;
}
