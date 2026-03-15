#ifndef RELAY_AGENT_ADVERTISE_H
#define RELAY_AGENT_ADVERTISE_H

/* ── Agent Advertisement — self-registration in ~/.relay.d/ ──────────────
 *
 * Each running agent writes a JSON file advertising its name, PID, and
 * bus socket path. Peers scan this directory to discover each other.
 * ─────────────────────────────────────────────────────────────────────── */

/* Write {dir}/{name}.json with this agent's PID and socket path.
 * Creates the directory if it doesn't exist.
 * Returns RELAY_OK or RELAY_ERR. */
int agent_advertise_publish(const char *dir, const char *name,
                            const char *socket_path);

/* Remove {dir}/{name}.json.
 * Returns RELAY_OK or RELAY_ERR_NOTFOUND if file didn't exist. */
int agent_advertise_withdraw(const char *dir, const char *name);

#endif /* RELAY_AGENT_ADVERTISE_H */
