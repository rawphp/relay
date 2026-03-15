/* platform.c — Platform abstraction implementation */

#include "platform.h"
#include "transcript.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Platform Manager ───────────────────────────────────────────────────── */

platform_manager_t *platform_create(relay_http_t *http, const config_t *cfg)
{
    if (!http || !cfg) {
        return NULL;
    }

    platform_manager_t *mgr = calloc(1, sizeof(platform_manager_t));
    if (!mgr) {
        return NULL;
    }

    /* Initialize Telegram if configured */
    const char *tg_token = config_get(cfg, "telegram_bot_token", NULL);
    if (tg_token && tg_token[0] != '\0') {
        mgr->telegram = telegram_create(http, cfg);
        if (mgr->telegram) {
            mgr->telegram_enabled = 1;
        }
    }

    /* Ensure at least one platform is enabled */
    if (!mgr->telegram_enabled) {
        platform_free(mgr);
        return NULL;
    }

    return mgr;
}

void platform_free(platform_manager_t *mgr)
{
    if (!mgr) {
        return;
    }

    if (mgr->telegram) {
        telegram_free(mgr->telegram);
    }

    free(mgr);
}

/* ── Message Polling ────────────────────────────────────────────────────── */

static int telegram_to_platform_message(const telegram_message_t *tg,
                                        const char *agent_name,
                                        platform_message_t *msg) __attribute__((unused));
static int telegram_to_platform_message(const telegram_message_t *tg,
                                        const char *agent_name,
                                        platform_message_t *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->platform = PLATFORM_TELEGRAM;

    snprintf(msg->chat_id, sizeof(msg->chat_id), "%s", tg->chat_id);
    snprintf(msg->user_id, sizeof(msg->user_id), "%s", tg->chat_id); /* Telegram doesn't track user_id separately */
    snprintf(msg->text, sizeof(msg->text), "%s", tg->text);
    snprintf(msg->message_id, sizeof(msg->message_id), "%d", tg->message_id);

    msg->is_dm = !telegram_is_group_chat(tg->chat_id);
    msg->is_mention = telegram_message_mentions_agent(tg, agent_name);

    memcpy(&msg->raw.telegram, tg, sizeof(telegram_message_t));

    return RELAY_OK;
}

int platform_poll_message(platform_manager_t *mgr, platform_message_t *msg)
{
    if (!mgr || !msg) {
        return RELAY_ERR_INVALID;
    }

    /* TODO: Implement telegram polling wrapper
     * Current architecture has event_loop directly call Telegram APIs
     * Need to either:
     * 1. Add telegram_poll() function to telegram.c
     * 2. Keep Telegram in event_loop and only add Slack here
     * 3. Refactor event_loop to use platform abstraction
     */

    return RELAY_ERR_NOTFOUND;
}

/* ── Reaction Polling ───────────────────────────────────────────────────── */

static int telegram_to_platform_reaction(const telegram_reaction_t *tg,
                                         platform_reaction_t *reaction) __attribute__((unused));
static int telegram_to_platform_reaction(const telegram_reaction_t *tg,
                                         platform_reaction_t *reaction)
{
    memset(reaction, 0, sizeof(*reaction));
    reaction->platform = PLATFORM_TELEGRAM;

    snprintf(reaction->chat_id, sizeof(reaction->chat_id), "%s", tg->chat_id);
    snprintf(reaction->message_id, sizeof(reaction->message_id), "%d",
             tg->message_id);
    snprintf(reaction->emoji, sizeof(reaction->emoji), "%s", tg->emoji);

    return RELAY_OK;
}

int platform_poll_reaction(platform_manager_t *mgr, platform_reaction_t *reaction)
{
    if (!mgr || !reaction) {
        return RELAY_ERR_INVALID;
    }

    /* TODO: Same as platform_poll_message - needs architecture decision */

    return RELAY_ERR_NOTFOUND;
}

/* ── Message Sending ────────────────────────────────────────────────────── */

int platform_send_message(platform_manager_t *mgr, platform_type_t platform,
                          const char *chat_id, const char *text,
                          const char *reply_to)
{
    if (!mgr || !chat_id || !text) {
        return RELAY_ERR_INVALID;
    }

    switch (platform) {
    case PLATFORM_TELEGRAM:
        if (!mgr->telegram_enabled || !mgr->telegram) {
            return RELAY_ERR_INVALID;
        }
        /* TODO: telegram_send_message doesn't exist - use telegram_send_chunked in event_loop */
        (void)reply_to;
        return RELAY_ERR_INVALID;

    default:
        return RELAY_ERR_INVALID;
    }
}

/* ── Authorization ──────────────────────────────────────────────────────── */

int platform_is_authorized(platform_manager_t *mgr, platform_type_t platform,
                           const char *user_id)
{
    if (!mgr || !user_id) {
        return 0;
    }

    switch (platform) {
    case PLATFORM_TELEGRAM:
        /* user_id here is chat_id; no from_id available at this level — fail-closed for groups */
        return mgr->telegram ? telegram_is_authorized(mgr->telegram, user_id, NULL) : 0;

    default:
        return 0;
    }
}

/* ── Utilities ──────────────────────────────────────────────────────────── */

const char *platform_name(platform_type_t platform)
{
    switch (platform) {
    case PLATFORM_TELEGRAM:
        return "Telegram";
    default:
        return "Unknown";
    }
}
