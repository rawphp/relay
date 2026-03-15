/* platform.h — Platform abstraction for Telegram */

#ifndef RELAY_PLATFORM_H
#define RELAY_PLATFORM_H

#include "relay.h"
#include "config.h"
#include "telegram.h"

/* ── Platform Types ─────────────────────────────────────────────────────── */

typedef enum {
    PLATFORM_TELEGRAM
} platform_type_t;

/* ── Unified Message Structure ──────────────────────────────────────────── */

typedef struct {
    platform_type_t platform;           /* Source platform */
    char chat_id[RELAY_MAX_USER_ID];     /* Channel/chat ID */
    char user_id[RELAY_MAX_USER_ID];     /* User who sent message */
    char text[RELAY_MAX_MSG];        /* Message text */
    char message_id[64];                /* Platform-specific message ID */
    int is_mention;                     /* 1 if agent was mentioned */
    int is_dm;                          /* 1 if direct message */

    /* Platform-specific data */
    union {
        telegram_message_t telegram;
    } raw;
} platform_message_t;

/* ── Unified Reaction Structure ─────────────────────────────────────────── */

typedef struct {
    platform_type_t platform;
    char chat_id[RELAY_MAX_USER_ID];
    char message_id[64];                /* ID of message that was reacted to */
    char emoji[32];                     /* Emoji name or unicode */
    char user_id[RELAY_MAX_USER_ID];
    char quoted_text[RELAY_MAX_MSG]; /* Text of message that was reacted to */
} platform_reaction_t;

/* ── Platform Manager ───────────────────────────────────────────────────── */

typedef struct {
    /* Telegram */
    int telegram_enabled;
    telegram_t *telegram;
} platform_manager_t;

/* ── API Functions ──────────────────────────────────────────────────────── */

/**
 * Create platform manager
 * @param http HTTP client
 * @param cfg Configuration
 * @return Allocated platform_manager_t or NULL on failure
 */
platform_manager_t *platform_create(relay_http_t *http, const config_t *cfg);

/**
 * Free platform manager and all clients
 */
void platform_free(platform_manager_t *mgr);

/**
 * Poll for messages from any enabled platform (non-blocking)
 * @param msg Output unified message
 * @return RELAY_OK if message received, RELAY_ERR_NOTFOUND if none available
 */
int platform_poll_message(platform_manager_t *mgr, platform_message_t *msg);

/**
 * Poll for reactions from any enabled platform (non-blocking)
 * @param reaction Output unified reaction
 * @return RELAY_OK if reaction received, RELAY_ERR_NOTFOUND if none available
 */
int platform_poll_reaction(platform_manager_t *mgr, platform_reaction_t *reaction);

/**
 * Send message to a platform
 * @param platform Target platform (PLATFORM_TELEGRAM)
 * @param chat_id Channel/chat ID
 * @param text Message text
 * @param reply_to Optional message ID to reply to
 * @return RELAY_OK on success
 */
int platform_send_message(platform_manager_t *mgr, platform_type_t platform,
                          const char *chat_id, const char *text,
                          const char *reply_to);

/**
 * Check if user is authorized on a platform
 */
int platform_is_authorized(platform_manager_t *mgr, platform_type_t platform,
                           const char *user_id);

/**
 * Get platform name as string (for logging)
 */
const char *platform_name(platform_type_t platform);

#endif /* RELAY_PLATFORM_H */
