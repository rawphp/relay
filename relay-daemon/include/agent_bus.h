#ifndef RELAY_AGENT_BUS_H
#define RELAY_AGENT_BUS_H

/* ── Agent Bus — Unix domain socket inter-agent messaging ──────────────
 *
 * Each agent daemon listens on {workspace_path}/relay.sock.
 * Agents can send messages to each other's socket; the receiving daemon
 * feeds the message to the LLM and sends the response back.
 *
 * Socket is non-blocking (O_NONBLOCK) — accept() returns immediately
 * with EAGAIN if no client is queued. Checked once per event loop cycle.
 * ─────────────────────────────────────────────────────────────────────── */

typedef struct {
    char from[64];           /* Sender agent name */
    char from_socket[256];   /* Sender's socket path for the reply */
    char text[4096];         /* Message content */
    long long ts;            /* Unix timestamp */
    char msg_id[64];         /* Unique message ID */
    int depth;               /* Conversation depth (circuit breaker) */
    int is_autonomous;       /* 1 = AI-initiated, 0 = user-initiated */
    char addressed_to[64];   /* "all"/"team"=all respond; agent name=only they respond; ""=all */
    char participants[256];  /* Comma-separated list of all chat participants, e.g. "human,agent1,agent2" */
} agent_bus_message_t;

/* Bind listening socket at socket_path.
 * Removes any stale socket file before binding.
 * Returns RELAY_OK or RELAY_ERR. */
int agent_bus_init(const char *socket_path);

/* Return the listening socket fd, or -1 if not initialized. */
int agent_bus_get_fd(void);

/* Accept one pending connection and parse the JSON message into *out.
 * Non-blocking: returns RELAY_ERR_NOTFOUND if no connection is waiting.
 * Returns RELAY_OK on success, RELAY_ERR on read/parse failure. */
int agent_bus_accept_message(agent_bus_message_t *out);

/* Connect to target_socket, write a JSON message, and close.
 * Fire-and-forget: does not wait for a response.
 * Returns RELAY_OK or RELAY_ERR. */
int agent_bus_send(const char *target_socket, const char *from_name,
                   const char *from_socket, const char *text,
                   int depth, int is_autonomous, const char *addressed_to);

/* Append a JSONL entry to {log_dir}/agent-bus.jsonl.
 * direction should be "in" (received) or "out" (sent).
 * response may be NULL for inbound-only log entries. */
void agent_bus_log(const char *log_dir, const char *direction,
                   const agent_bus_message_t *msg, const char *response);

/* Set the connection rate limit (max connections per second).
 * Must be called before agent_bus_init() to take effect.
 * Default: 10/sec. */
void agent_bus_set_rate_limit(int max_per_sec);

/* Close listening socket and remove the socket file. */
void agent_bus_destroy(void);

#endif /* RELAY_AGENT_BUS_H */
