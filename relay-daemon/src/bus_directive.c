#include "bus_directive.h"
#include "agent_bus.h"
#include "bus_dead_drop.h"
#include "peer_registry.h"
#include "relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Directive pattern: [AGENT_BUS_SEND to=<name>] <message>
 * The name is everything between "to=" and "]".
 * The message is everything after "] " to end of line. */

static const char *DIRECTIVE_PREFIX = "[AGENT_BUS_SEND to=";
#define PREFIX_LEN 19  /* strlen("[AGENT_BUS_SEND to=") */

int bus_directive_process(const char *text, char *out, size_t out_max,
                          const char *self_name, const char *self_socket)
{
    if (!text || !out || out_max == 0) return 0;

    int count = 0;
    size_t out_off = 0;
    /* Buffer for status notes, appended at the end */
    char notes[1024];
    size_t notes_off = 0;

    const char *p = text;
    while (*p) {
        /* Find end of current line */
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

        /* Check if this line starts with the directive prefix */
        const char *dir = strstr(p, DIRECTIVE_PREFIX);
        if (dir && dir == p) {
            /* Parse: [AGENT_BUS_SEND to=<name>] <message> */
            const char *name_start = dir + PREFIX_LEN;
            const char *bracket = strchr(name_start, ']');

            if (bracket) {
                /* Extract target name */
                size_t name_len = (size_t)(bracket - name_start);
                char target_name[64];
                if (name_len > 0 && name_len < sizeof(target_name)) {
                    memcpy(target_name, name_start, name_len);
                    target_name[name_len] = '\0';

                    /* Extract message text (after "] ") */
                    const char *msg_start = bracket + 1;
                    if (*msg_start == ' ') msg_start++;
                    size_t msg_len = line_len - (size_t)(msg_start - p);
                    char msg_text[4096];
                    if (msg_len >= sizeof(msg_text)) msg_len = sizeof(msg_text) - 1;
                    memcpy(msg_text, msg_start, msg_len);
                    msg_text[msg_len] = '\0';

                    /* Look up peer */
                    const peer_entry_t *peer = peer_registry_find(target_name);
                    if (!peer) {
                        notes_off += (size_t)snprintf(
                            notes + notes_off, sizeof(notes) - notes_off,
                            "(agent \"%s\" not found)\n", target_name);
                    } else if (!peer->is_alive) {
                        /* Agent offline — save to dead drop */
                        agent_bus_message_t drop_msg;
                        memset(&drop_msg, 0, sizeof(drop_msg));
                        snprintf(drop_msg.from, sizeof(drop_msg.from), "%s", self_name);
                        snprintf(drop_msg.text, sizeof(drop_msg.text), "%s", msg_text);
                        drop_msg.ts = (long long)time(NULL);
                        snprintf(drop_msg.msg_id, sizeof(drop_msg.msg_id),
                                 "%lld-%d", drop_msg.ts, (int)getpid());
                        if (self_socket)
                            snprintf(drop_msg.from_socket, sizeof(drop_msg.from_socket),
                                     "%s", self_socket);

                        const char *home = getenv("HOME");
                        if (home) {
                            char ad_dir[512];
                            snprintf(ad_dir, sizeof(ad_dir), "%s/.relay.d", home);
                            bus_dead_drop_save(ad_dir, target_name, &drop_msg);
                        }
                        notes_off += (size_t)snprintf(
                            notes + notes_off, sizeof(notes) - notes_off,
                            "(agent \"%s\" is offline — message saved for when they return)\n",
                            target_name);
                    } else {
                        /* Send the message */
                        int send_rc = agent_bus_send(peer->socket_path, self_name,
                                                     self_socket, msg_text,
                                                     0, 0, target_name);
                        if (send_rc == RELAY_OK) {
                            notes_off += (size_t)snprintf(
                                notes + notes_off, sizeof(notes) - notes_off,
                                "(sent message to %s)\n", target_name);
                        } else {
                            /* Send failed — save to dead drop */
                            agent_bus_message_t drop_msg;
                            memset(&drop_msg, 0, sizeof(drop_msg));
                            snprintf(drop_msg.from, sizeof(drop_msg.from), "%s", self_name);
                            snprintf(drop_msg.text, sizeof(drop_msg.text), "%s", msg_text);
                            drop_msg.ts = (long long)time(NULL);
                            snprintf(drop_msg.msg_id, sizeof(drop_msg.msg_id),
                                     "%lld-%d", drop_msg.ts, (int)getpid());

                            const char *home = getenv("HOME");
                            if (home) {
                                char ad_dir[512];
                                snprintf(ad_dir, sizeof(ad_dir), "%s/.relay.d", home);
                                bus_dead_drop_save(ad_dir, target_name, &drop_msg);
                            }
                            notes_off += (size_t)snprintf(
                                notes + notes_off, sizeof(notes) - notes_off,
                                "(failed to reach %s — message saved for when they return)\n",
                                target_name);
                        }
                    }

                    count++;
                    /* Skip this line (don't copy to output) */
                    p += line_len;
                    if (eol) p++; /* skip newline */
                    continue;
                }
            }
        }

        /* Copy this line to output */
        if (out_off + line_len + 1 < out_max) {
            memcpy(out + out_off, p, line_len);
            out_off += line_len;
            if (eol) {
                out[out_off++] = '\n';
            }
        }

        p += line_len;
        if (eol) p++;
    }

    /* Append status notes */
    if (notes_off > 0 && out_off + notes_off + 2 < out_max) {
        if (out_off > 0 && out[out_off - 1] != '\n') {
            out[out_off++] = '\n';
        }
        memcpy(out + out_off, notes, notes_off);
        out_off += notes_off;
    }

    /* Remove trailing newline for clean output */
    while (out_off > 0 && out[out_off - 1] == '\n') {
        out_off--;
    }

    out[out_off] = '\0';
    return count;
}
