#include "telegram.h"

#include <cJSON/cJSON.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ─────────────────────────────────────────────────── */

struct telegram {
    relay_http_t *http;
    char bot_token[RELAY_MAX_TOKEN];
    char authorized_user[RELAY_MAX_USER_ID];
    char authorized_group[RELAY_MAX_USER_ID];
    char parent_user[RELAY_MAX_USER_ID];   /* optional parent/guardian — DM only */
    int last_update_id;
};

/* ── Public API ─────────────────────────────────────────────────────── */

telegram_t *telegram_create(relay_http_t *http, const config_t *cfg)
{
    if (!http || !cfg) {
        return NULL;
    }

    telegram_t *tg = calloc(1, sizeof(telegram_t));
    if (!tg) {
        return NULL;
    }

    tg->http = http;
    snprintf(tg->bot_token, RELAY_MAX_TOKEN, "%s",
             config_get(cfg, "telegram_bot_token", ""));
    snprintf(tg->authorized_user, RELAY_MAX_USER_ID, "%s",
             config_get(cfg, "telegram_user_id", ""));
    snprintf(tg->authorized_group, RELAY_MAX_USER_ID, "%s",
             config_get(cfg, "telegram_group_chat_id", ""));
    snprintf(tg->parent_user, RELAY_MAX_USER_ID, "%s",
             config_get(cfg, "parent_telegram_user_id", ""));
    tg->last_update_id = 0;

    return tg;
}

int telegram_api_url(telegram_t *tg, const char *method,
                     char *buf, size_t max)
{
    if (!tg || !method || !buf) {
        return RELAY_ERR;
    }

    snprintf(buf, max, "https://api.telegram.org/bot%s/%s",
             tg->bot_token, method);
    return RELAY_OK;
}

int telegram_parse_update(const char *json, telegram_message_t *msg)
{
    if (!json || !msg) {
        return RELAY_ERR_PARSE;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    cJSON *message = cJSON_GetObjectItem(root, "message");
    if (!cJSON_IsObject(message)) {
        cJSON_Delete(root);
        return RELAY_ERR_NOTFOUND;
    }

    /* chat.id */
    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
    if (cJSON_IsNumber(chat_id)) {
        snprintf(msg->chat_id, RELAY_MAX_USER_ID, "%.0f",
                 chat_id->valuedouble);
    } else {
        msg->chat_id[0] = '\0';
    }

    /* text */
    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (cJSON_IsString(text)) {
        snprintf(msg->text, RELAY_MAX_MSG, "%s", text->valuestring);
    } else {
        msg->text[0] = '\0';
    }

    /* caption (for photos, videos, documents) */
    cJSON *caption = cJSON_GetObjectItem(message, "caption");
    if (cJSON_IsString(caption)) {
        snprintf(msg->caption, RELAY_MAX_MSG, "%s", caption->valuestring);
    } else {
        msg->caption[0] = '\0';
    }

    /* photo (array of PhotoSize objects, we take the last = highest res) */
    cJSON *photo = cJSON_GetObjectItem(message, "photo");
    if (cJSON_IsArray(photo) && cJSON_GetArraySize(photo) > 0) {
        int photo_count = cJSON_GetArraySize(photo);
        cJSON *best_photo = cJSON_GetArrayItem(photo, photo_count - 1);
        cJSON *file_id = cJSON_GetObjectItem(best_photo, "file_id");
        if (cJSON_IsString(file_id)) {
            snprintf(msg->photo_file_id, RELAY_MAX_VALUE, "%s", file_id->valuestring);
            msg->has_photo = 1;
        }
    } else {
        msg->photo_file_id[0] = '\0';
        msg->has_photo = 0;
    }

    /* document */
    cJSON *document = cJSON_GetObjectItem(message, "document");
    if (cJSON_IsObject(document)) {
        cJSON *file_id = cJSON_GetObjectItem(document, "file_id");
        if (cJSON_IsString(file_id)) {
            snprintf(msg->document_file_id, RELAY_MAX_VALUE, "%s", file_id->valuestring);
            msg->has_document = 1;
        }
    } else {
        msg->document_file_id[0] = '\0';
        msg->has_document = 0;
    }

    /* video */
    cJSON *video = cJSON_GetObjectItem(message, "video");
    if (cJSON_IsObject(video)) {
        cJSON *file_id = cJSON_GetObjectItem(video, "file_id");
        if (cJSON_IsString(file_id)) {
            snprintf(msg->video_file_id, RELAY_MAX_VALUE, "%s", file_id->valuestring);
            msg->has_video = 1;
        }
    } else {
        msg->video_file_id[0] = '\0';
        msg->has_video = 0;
    }

    /* voice */
    cJSON *voice = cJSON_GetObjectItem(message, "voice");
    if (cJSON_IsObject(voice)) {
        cJSON *file_id = cJSON_GetObjectItem(voice, "file_id");
        if (cJSON_IsString(file_id)) {
            snprintf(msg->voice_file_id, RELAY_MAX_VALUE, "%s", file_id->valuestring);
            msg->has_voice = 1;
        }
    } else {
        msg->voice_file_id[0] = '\0';
        msg->has_voice = 0;
    }

    /* sticker */
    cJSON *sticker = cJSON_GetObjectItem(message, "sticker");
    if (cJSON_IsObject(sticker)) {
        cJSON *file_id = cJSON_GetObjectItem(sticker, "file_id");
        if (cJSON_IsString(file_id)) {
            snprintf(msg->sticker_file_id, RELAY_MAX_VALUE, "%s", file_id->valuestring);
            msg->has_sticker = 1;
        }
    } else {
        msg->sticker_file_id[0] = '\0';
        msg->has_sticker = 0;
    }

    /* from.username and from.id */
    cJSON *from = cJSON_GetObjectItem(message, "from");
    cJSON *username = from ? cJSON_GetObjectItem(from, "username") : NULL;
    if (cJSON_IsString(username)) {
        snprintf(msg->username, RELAY_MAX_VALUE, "%s", username->valuestring);
    } else {
        msg->username[0] = '\0';
    }
    cJSON *from_id_json = from ? cJSON_GetObjectItem(from, "id") : NULL;
    if (cJSON_IsNumber(from_id_json)) {
        snprintf(msg->from_id, RELAY_MAX_USER_ID, "%.0f", from_id_json->valuedouble);
    } else {
        msg->from_id[0] = '\0';
    }

    /* message_id */
    cJSON *mid = cJSON_GetObjectItem(message, "message_id");
    msg->message_id = cJSON_IsNumber(mid) ? (int)mid->valuedouble : 0;

    /* is_command */
    msg->is_command = (msg->text[0] == '/') ? 1 : 0;

    cJSON_Delete(root);
    return RELAY_OK;
}

int telegram_parse_reaction(const char *json, telegram_reaction_t *reaction)
{
    if (!json || !reaction) {
        return RELAY_ERR_PARSE;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    cJSON *mr = cJSON_GetObjectItem(root, "message_reaction");
    if (!cJSON_IsObject(mr)) {
        cJSON_Delete(root);
        return RELAY_ERR_NOTFOUND;
    }

    /* chat.id */
    cJSON *chat = cJSON_GetObjectItem(mr, "chat");
    cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
    if (cJSON_IsNumber(chat_id)) {
        snprintf(reaction->chat_id, RELAY_MAX_USER_ID, "%.0f",
                 chat_id->valuedouble);
    } else {
        reaction->chat_id[0] = '\0';
    }

    /* message_id */
    cJSON *mid = cJSON_GetObjectItem(mr, "message_id");
    reaction->message_id = cJSON_IsNumber(mid) ? (int)mid->valuedouble : 0;

    /* new_reaction[0].emoji */
    reaction->emoji[0] = '\0';
    cJSON *new_reaction = cJSON_GetObjectItem(mr, "new_reaction");
    if (cJSON_IsArray(new_reaction) && cJSON_GetArraySize(new_reaction) > 0) {
        cJSON *first = cJSON_GetArrayItem(new_reaction, 0);
        cJSON *emoji = first ? cJSON_GetObjectItem(first, "emoji") : NULL;
        if (cJSON_IsString(emoji)) {
            snprintf(reaction->emoji, sizeof(reaction->emoji), "%s",
                     emoji->valuestring);
        }
    }

    cJSON_Delete(root);
    return RELAY_OK;
}

int telegram_is_media_message(const telegram_message_t *msg)
{
    if (!msg) {
        return 0;
    }
    return msg->has_photo || msg->has_document || msg->has_video ||
           msg->has_voice || msg->has_sticker;
}

int telegram_message_mentions_agent(const telegram_message_t *msg, const char *agent_name)
{
    if (!msg || msg->text[0] == '\0' || !agent_name || agent_name[0] == '\0') {
        return 0;
    }

    /* Convert message to lowercase for case-insensitive matching */
    char lower[RELAY_MAX_MSG];
    size_t len = strlen(msg->text);
    if (len >= RELAY_MAX_MSG) {
        len = RELAY_MAX_MSG - 1;
    }

    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)msg->text[i]);
    }
    lower[len] = '\0';

    /* Convert agent_name to lowercase for comparison */
    char name_lower[RELAY_MAX_VALUE];
    size_t name_len = strlen(agent_name);
    if (name_len >= RELAY_MAX_VALUE) {
        name_len = RELAY_MAX_VALUE - 1;
    }
    for (size_t i = 0; i < name_len; i++) {
        name_lower[i] = (char)tolower((unsigned char)agent_name[i]);
    }
    name_lower[name_len] = '\0';

    /* Look for agent_name as a separate word */
    const char *pos = strstr(lower, name_lower);
    while (pos != NULL) {
        /* Check word boundary before name */
        int before_ok = (pos == lower) || !isalnum((unsigned char)pos[-1]);

        /* Check word boundary after name */
        int after_ok = (pos[name_len] == '\0') || !isalnum((unsigned char)pos[name_len]);

        if (before_ok && after_ok) {
            return 1;
        }

        /* Keep searching */
        pos = strstr(pos + 1, name_lower);
    }

    return 0;
}

int telegram_is_group_chat(const char *chat_id)
{
    if (!chat_id || chat_id[0] == '\0') {
        return 0;
    }

    /* Group chats have negative IDs (start with '-') */
    return chat_id[0] == '-';
}

int telegram_is_authorized(telegram_t *tg, const char *chat_id, const char *from_id)
{
    if (!tg || !chat_id || chat_id[0] == '\0') {
        return 0;
    }

    /* DM: chat_id IS the user's ID -- already user-specific, safe */
    if (strcmp(tg->authorized_user, chat_id) == 0) {
        return 1;
    }

    /* Parent/guardian DM: allowed for direct oversight, but NOT in group chats
     * (any group member could spoof a parent message via forwarding). */
    if (tg->parent_user[0] != '\0' &&
        strcmp(tg->parent_user, chat_id) == 0 &&
        chat_id[0] != '-') {
        return 1;
    }

    /* Group chat: verify both the group AND that the sender is the authorized user.
     * Any member of the group can send a message, so we must check from.id. */
    if (tg->authorized_group[0] != '\0' &&
        strcmp(tg->authorized_group, chat_id) == 0) {
        if (from_id && from_id[0] != '\0' &&
            strcmp(tg->authorized_user, from_id) == 0) {
            return 1;
        }
        return 0;  /* Group member but not the authorized user */
    }

    return 0;
}

int telegram_chunk_message(const char *text,
                           char chunks[][RELAY_TELEGRAM_CHUNK + 1],
                           int max_chunks)
{
    if (!text || !chunks || max_chunks <= 0) {
        return 0;
    }

    size_t len = strlen(text);
    if (len <= RELAY_TELEGRAM_CHUNK) {
        snprintf(chunks[0], RELAY_TELEGRAM_CHUNK + 1, "%s", text);
        return 1;
    }

    int chunk_count = 0;
    size_t offset = 0;

    while (offset < len && chunk_count < max_chunks) {
        size_t remaining = len - offset;
        if (remaining <= RELAY_TELEGRAM_CHUNK) {
            snprintf(chunks[chunk_count], RELAY_TELEGRAM_CHUNK + 1,
                     "%s", text + offset);
            chunk_count++;
            break;
        }

        /* Find last newline within chunk limit */
        size_t split = RELAY_TELEGRAM_CHUNK;
        while (split > 0 && text[offset + split] != '\n') {
            split--;
        }

        if (split == 0) {
            /* No newline found — hard split */
            split = RELAY_TELEGRAM_CHUNK;
        } else {
            split++; /* Include the newline in this chunk */
        }

        memcpy(chunks[chunk_count], text + offset, split);
        chunks[chunk_count][split] = '\0';
        chunk_count++;
        offset += split;
    }

    return chunk_count;
}

int telegram_get_file_path(telegram_t *tg, const char *file_id,
                           char *file_path_out, size_t max)
{
    if (!tg || !file_id || !file_path_out) {
        return RELAY_ERR;
    }

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "getFile", url, sizeof(url));

    /* Append ?file_id=... */
    size_t len = strlen(url);
    snprintf(url + len, sizeof(url) - len, "?file_id=%s", file_id);

    char resp[RELAY_MAX_MSG];
    int rc = tg->http->get(url, resp, sizeof(resp));
    if (rc != RELAY_OK) {
        return rc;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *fp = result ? cJSON_GetObjectItem(result, "file_path") : NULL;
    if (!cJSON_IsString(fp)) {
        cJSON_Delete(root);
        return RELAY_ERR_NOTFOUND;
    }

    snprintf(file_path_out, max, "%s", fp->valuestring);
    cJSON_Delete(root);
    return RELAY_OK;
}

int telegram_download_file(telegram_t *tg, const char *file_path,
                            const char *local_path)
{
    if (!tg || !file_path || !local_path) {
        return RELAY_ERR;
    }

    char url[RELAY_MAX_URL];
    snprintf(url, sizeof(url), "https://api.telegram.org/file/bot%s/%s",
             tg->bot_token, file_path);

    return tg->http->get_to_file(url, local_path);
}

int telegram_send_document(telegram_t *tg, const char *chat_id,
                            const char *local_path, const char *caption)
{
    if (!tg || !chat_id || !local_path) {
        return RELAY_ERR;
    }

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "sendDocument", url, sizeof(url));

    /* Build form fields */
    const char *form_fields[5];
    int idx = 0;

    form_fields[idx++] = "chat_id";
    form_fields[idx++] = chat_id;

    if (caption && caption[0] != '\0') {
        form_fields[idx++] = "caption";
        form_fields[idx++] = caption;
    }

    form_fields[idx] = NULL;

    char resp[RELAY_MAX_MSG];
    return tg->http->post_file(url, local_path, "document",
                                form_fields, resp, sizeof(resp));
}

int telegram_edit_message(telegram_t *tg, const char *chat_id,
                           int message_id, const char *new_text)
{
    if (!tg || !chat_id || message_id <= 0 || !new_text) {
        return -1;
    }

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "editMessageText", url, sizeof(url));

    /* Build JSON body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddNumberToObject(body, "message_id", message_id);
    cJSON_AddStringToObject(body, "text", new_text);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    /* Send request */
    char resp[RELAY_MAX_MSG];
    int rc = tg->http->post(url, json, resp, sizeof(resp));
    free(json);

    if (rc != RELAY_OK) {
        /* Check if it's a rate limit error (429) */
        if (strstr(resp, "Too Many Requests") || strstr(resp, "429")) {
            return -1;  /* Rate limited */
        }
        return -1;
    }

    return message_id;
}

int telegram_send_text(telegram_t *tg, const char *chat_id, const char *text)
{
    if (!tg || !chat_id || !text) {
        return -1;
    }

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "sendMessage", url, sizeof(url));

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    int message_id = -1;
    if (json) {
        char resp[RELAY_MAX_MSG];
        if (tg->http->post(url, json, resp, sizeof(resp)) == RELAY_OK) {
            cJSON *root = cJSON_Parse(resp);
            if (root) {
                cJSON *result = cJSON_GetObjectItem(root, "result");
                if (result) {
                    cJSON *msg_id = cJSON_GetObjectItem(result, "message_id");
                    if (msg_id && cJSON_IsNumber(msg_id)) {
                        message_id = msg_id->valueint;
                    }
                }
                cJSON_Delete(root);
            }
        }
        free(json);
    }
    return message_id;
}

const char *telegram_get_parent_user(const telegram_t *tg)
{
    return tg ? tg->parent_user : "";
}

const char *telegram_get_authorized_user(const telegram_t *tg)
{
    return tg ? tg->authorized_user : "";
}

int telegram_register_commands(telegram_t *tg)
{
    if (!tg) {
        return RELAY_ERR;
    }

    /* Commands the daemon actually handles (matches event_loop dispatch) */
    static const struct {
        const char *command;
        const char *description;
    } commands[] = {
        { "start",     "Get started"                        },
        { "help",      "Show available commands"            },
        { "status",    "Show daemon and memory status"      },
        { "restart",   "Restart the daemon"                 },
        { "clear",     "Start a new conversation"           },
        { "close",     "Close the active space"             },
        { "space",     "Switch space (/space <name>)"       },
        { "spaces",    "List all spaces"                    },
        { "sessions",  "Browse resumable sessions"          },
        { "workspace", "Show current workspace info"        },
        { "reload",    "Reload config without restart"      },
    };
    int ncommands = (int)(sizeof(commands) / sizeof(commands[0]));

    /* Build JSON: {"commands":[{"command":"...","description":"..."},...]} */
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "commands");
    for (int i = 0; i < ncommands; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "command",     commands[i].command);
        cJSON_AddStringToObject(entry, "description", commands[i].description);
        cJSON_AddItemToArray(arr, entry);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return RELAY_ERR_NOMEM;
    }

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "setMyCommands", url, sizeof(url));

    char resp[RELAY_MAX_MSG];
    int rc = tg->http->post(url, json, resp, sizeof(resp));
    free(json);

    return rc;
}

void telegram_free(telegram_t *tg)
{
    free(tg);
}
