#ifndef RELAY_TELEGRAM_H
#define RELAY_TELEGRAM_H

#include "relay.h"
#include "config.h"

/* ── Telegram Channel ───────────────────────────────────────────────── */
/* Implements relay_channel_t for Telegram Bot API via long polling.      */

/* Parsed Telegram message */
typedef struct {
    char chat_id[RELAY_MAX_USER_ID];
    char text[RELAY_MAX_MSG];
    char caption[RELAY_MAX_MSG];         /* caption for photo/video/document */
    char username[RELAY_MAX_VALUE];
    char from_id[RELAY_MAX_USER_ID];     /* sender's numeric user ID (from.id) */
    char photo_file_id[RELAY_MAX_VALUE];
    char document_file_id[RELAY_MAX_VALUE];
    char video_file_id[RELAY_MAX_VALUE];
    char voice_file_id[RELAY_MAX_VALUE];
    char sticker_file_id[RELAY_MAX_VALUE];
    int message_id;
    int is_command;             /* starts with / */
    int has_photo;
    int has_document;
    int has_video;
    int has_voice;
    int has_sticker;
} telegram_message_t;

/* Parsed Telegram reaction */
typedef struct {
    char chat_id[RELAY_MAX_USER_ID];
    int  message_id;
    char emoji[32];   /* empty string if reaction was removed */
} telegram_reaction_t;

/* Opaque telegram type */
typedef struct telegram telegram_t;

/* Create Telegram client. Returns NULL on error. */
telegram_t *telegram_create(relay_http_t *http, const config_t *cfg);

/* Parse a Telegram update JSON into a message struct.
 * Returns RELAY_OK if a text message was found. */
int telegram_parse_update(const char *json, telegram_message_t *msg);

/* Parse a Telegram message_reaction update into a reaction struct.
 * Returns RELAY_OK if a reaction was found, RELAY_ERR_NOTFOUND otherwise. */
int telegram_parse_reaction(const char *json, telegram_reaction_t *reaction);

/* Check if a chat_id is the authorized user or group.
 * For group chats, also verifies from_id matches the authorized user.
 * Parent/guardian DMs (parent_telegram_user_id) are also authorized. */
int telegram_is_authorized(telegram_t *tg, const char *chat_id, const char *from_id);

/* Return the configured parent/guardian user ID, or "" if not set. */
const char *telegram_get_parent_user(const telegram_t *tg);

/* Return the configured authorized user chat ID, or "" if not set. */
const char *telegram_get_authorized_user(const telegram_t *tg);

/* Check if a message mentions agent_name (case-insensitive, word boundary).
 * Returns 1 if mentioned, 0 otherwise. */
int telegram_message_mentions_agent(const telegram_message_t *msg, const char *agent_name);

/* Check if a chat_id is a group chat (negative ID).
 * Returns 1 if group, 0 if private DM. */
int telegram_is_group_chat(const char *chat_id);

/* Check if a message contains media (photo/document/video/voice/sticker).
 * Returns 1 if any media flag is set, 0 otherwise. */
int telegram_is_media_message(const telegram_message_t *msg);

/* Split a message into chunks <= 4096 chars, splitting at newlines.
 * Returns number of chunks written to chunks array.
 * Each chunk is null-terminated within the provided buffer. */
int telegram_chunk_message(const char *text, char chunks[][RELAY_TELEGRAM_CHUNK + 1],
                           int max_chunks);

/* Build a Telegram API URL. Writes to buf. */
int telegram_api_url(telegram_t *tg, const char *method,
                     char *buf, size_t max);

/* Get the file_path for a Telegram file by file_id.
 * Calls the getFile API. Writes file_path to file_path_out.
 * Returns RELAY_OK on success. */
int telegram_get_file_path(telegram_t *tg, const char *file_id,
                           char *file_path_out, size_t max);

/* Download a Telegram file to a local path.
 * file_path is the value from telegram_get_file_path().
 * Returns RELAY_OK on success. */
int telegram_download_file(telegram_t *tg, const char *file_path,
                            const char *local_path);

/* Send a document to a Telegram chat.
 * local_path is the file to upload.
 * caption is optional (can be NULL).
 * Returns RELAY_OK on success. */
int telegram_send_document(telegram_t *tg, const char *chat_id,
                            const char *local_path, const char *caption);

/* Edit an existing message.
 * chat_id: target chat
 * message_id: ID of message to edit
 * new_text: new message content
 * Returns message_id on success, -1 on error (including rate limits). */
int telegram_edit_message(telegram_t *tg, const char *chat_id,
                           int message_id, const char *new_text);

/* Send a text message to a chat.
 * chat_id: target chat
 * text: message content
 * Returns message_id on success, -1 on error. */
int telegram_send_text(telegram_t *tg, const char *chat_id, const char *text);

/* Register bot commands with Telegram via setMyCommands so the "/" picker
 * is populated automatically. Call once on daemon startup.
 * Returns RELAY_OK on success. On error, logs and returns the error code
 * but callers should treat this as non-fatal. */
int telegram_register_commands(telegram_t *tg);

/* Free Telegram client. */
void telegram_free(telegram_t *tg);

#endif /* RELAY_TELEGRAM_H */
