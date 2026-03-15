#include "Unity/unity.h"
#include "telegram.h"
#include "mocks.h"
#include <unistd.h>

/* ── Test: Parse text message from update JSON ──────────────────────── */
static void test_telegram_parse_text_message(void)
{
    const char *json =
        "{\"update_id\":123,"
        "\"message\":{\"message_id\":456,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\",\"first_name\":\"John\"},"
        "\"text\":\"hello kai\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("123456789", msg.chat_id);
    TEST_ASSERT_EQUAL_STRING("hello kai", msg.text);
    TEST_ASSERT_EQUAL_STRING("johnk", msg.username);
    TEST_ASSERT_EQUAL_INT(456, msg.message_id);
    TEST_ASSERT_EQUAL_INT(0, msg.is_command);
}

/* ── Test: Parse command message ────────────────────────────────────── */
static void test_telegram_parse_command(void)
{
    const char *json =
        "{\"update_id\":124,"
        "\"message\":{\"message_id\":457,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"text\":\"/clear\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("/clear", msg.text);
    TEST_ASSERT_EQUAL_INT(1, msg.is_command);
}

/* ── Test: Parse update with no message (e.g., edited_message) ──────── */
static void test_telegram_parse_no_message(void)
{
    const char *json =
        "{\"update_id\":125,"
        "\"edited_message\":{\"text\":\"edited\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
}

/* ── Test: Parse malformed JSON ─────────────────────────────────────── */
static void test_telegram_parse_malformed(void)
{
    telegram_message_t msg;
    int rc = telegram_parse_update("{bad json{{", &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

/* ── Test: Authorized user check (DM) ───────────────────────────────── */
static void test_telegram_authorized_user(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* DM: chat_id == authorized_user — from_id is irrelevant */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "123456789", NULL));
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "999999999", NULL));
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "", NULL));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: Group chat — only authorized sender passes (Threat 3.1 fix) ─ */
static void test_telegram_authorized_group_chat(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n"
        "telegram_group_chat_id = -100123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* DM is always user-specific regardless of from_id */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "123456789", NULL));

    /* Group + authorized sender → allow */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "-100123456789", "123456789"));

    /* Group + different group member → deny */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100123456789", "999999999"));

    /* Group + empty from_id → fail-closed (deny) */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100123456789", ""));

    /* Group + NULL from_id → fail-closed (deny) */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100123456789", NULL));

    /* Unknown chat still rejected */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "999999999", NULL));
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100987654321", "123456789"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: No group chat configured ─────────────────────────────────── */
static void test_telegram_no_group_chat(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* DM should be authorized */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "123456789", NULL));

    /* Group not configured → always deny, even with matching from_id */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100123456789", "123456789"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: Parser extracts from.id into msg->from_id ────────────────── */
static void test_telegram_parse_from_id(void)
{
    const char *json =
        "{\"update_id\":600,"
        "\"message\":{\"message_id\":700,"
        "\"chat\":{\"id\":-100123456789},"
        "\"from\":{\"id\":123456789,\"username\":\"johnk\"},"
        "\"text\":\"hey kai\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("-100123456789", msg.chat_id);
    TEST_ASSERT_EQUAL_STRING("123456789", msg.from_id);
    TEST_ASSERT_EQUAL_STRING("johnk", msg.username);
}

/* ── Test: Parser sets from_id to empty when from.id is absent ──────── */
static void test_telegram_parse_from_id_missing(void)
{
    const char *json =
        "{\"update_id\":601,"
        "\"message\":{\"message_id\":701,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"text\":\"hello\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("", msg.from_id);
}

/* ── Test: Message mentions Kai (lowercase) ─────────────────────────── */
static void test_telegram_message_mentions_agent_lowercase(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "hey kai, what do you think?");
    TEST_ASSERT_EQUAL_INT(1, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Message mentions Kai (uppercase) ─────────────────────────── */
static void test_telegram_message_mentions_agent_uppercase(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "KAI can you help?");
    TEST_ASSERT_EQUAL_INT(1, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Message mentions @kai (with @) ────────────────────────────── */
static void test_telegram_message_mentions_agent_at_symbol(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "@kai what's up");
    TEST_ASSERT_EQUAL_INT(1, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Message doesn't mention Kai ──────────────────────────────── */
static void test_telegram_message_no_mention(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "hey everyone, how's it going?");
    TEST_ASSERT_EQUAL_INT(0, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Message with "kai" as part of another word ───────────────── */
static void test_telegram_message_kai_substring(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "I'm talking about kaizenproject here");
    /* Should NOT match — "kai" must be a separate word */
    TEST_ASSERT_EQUAL_INT(0, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Empty message doesn't mention Kai ────────────────────────── */
static void test_telegram_message_empty_no_mention(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.text[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, telegram_message_mentions_agent(&msg, "Kai"));
}

/* ── Test: Mention detection with custom agent name ──────────────────── */
static void test_telegram_message_mentions_custom_agent(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "hey buddy, what do you think?");
    TEST_ASSERT_EQUAL_INT(1, telegram_message_mentions_agent(&msg, "Buddy"));
}

/* ── Test: Custom agent name does NOT false-positive on Kai ──────────── */
static void test_telegram_message_custom_agent_no_kai(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "hey kai, what do you think?");
    /* When agent is "Buddy", mentioning "kai" should NOT match */
    TEST_ASSERT_EQUAL_INT(0, telegram_message_mentions_agent(&msg, "Buddy"));
}

/* ── Test: Message chunking — short message ─────────────────────────── */
static void test_telegram_chunk_short(void)
{
    char chunks[4][RELAY_TELEGRAM_CHUNK + 1];
    int n = telegram_chunk_message("short message", chunks, 4);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("short message", chunks[0]);
}

/* ── Test: Message chunking — long message splits at newlines ───────── */
static void test_telegram_chunk_long(void)
{
    /* Build a message with multiple lines totaling > 4096 chars */
    char long_msg[8192];
    memset(long_msg, 0, sizeof(long_msg));

    /* Fill with lines of 100 chars each */
    int offset = 0;
    for (int i = 0; i < 60; i++) {
        char line[110];
        memset(line, 'A' + (i % 26), 99);
        line[99] = '\n';
        line[100] = '\0';
        memcpy(long_msg + offset, line, 100);
        offset += 100;
    }
    long_msg[offset] = '\0';

    char chunks[8][RELAY_TELEGRAM_CHUNK + 1];
    int n = telegram_chunk_message(long_msg, chunks, 8);
    TEST_ASSERT_GREATER_THAN(1, n);

    /* Each chunk should be <= 4096 chars */
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_LESS_OR_EQUAL(RELAY_TELEGRAM_CHUNK, (int)strlen(chunks[i]));
    }
}

/* ── Test: API URL building ─────────────────────────────────────────── */
static void test_telegram_api_url(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = mytoken123\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    char url[RELAY_MAX_URL];
    telegram_api_url(tg, "sendMessage", url, sizeof(url));
    TEST_ASSERT_EQUAL_STRING(
        "https://api.telegram.org/botmytoken123/sendMessage", url);

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: Parse photo message ──────────────────────────────────────── */
static void test_telegram_parse_photo_message(void)
{
    const char *json =
        "{\"update_id\":200,"
        "\"message\":{\"message_id\":500,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"photo\":[{\"file_id\":\"photo123\",\"width\":1280,\"height\":720}],"
        "\"caption\":\"Check this out\"}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("123456789", msg.chat_id);
    TEST_ASSERT_EQUAL_STRING("photo123", msg.photo_file_id);
    TEST_ASSERT_EQUAL_STRING("Check this out", msg.caption);
    TEST_ASSERT_EQUAL_INT(1, msg.has_photo);
    TEST_ASSERT_EQUAL_INT(0, msg.is_command);
}

/* ── Test: Parse photo message without caption ──────────────────────── */
static void test_telegram_parse_photo_no_caption(void)
{
    const char *json =
        "{\"update_id\":201,"
        "\"message\":{\"message_id\":501,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"photo\":[{\"file_id\":\"photo456\"}]}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("photo456", msg.photo_file_id);
    TEST_ASSERT_EQUAL_STRING("", msg.caption);
    TEST_ASSERT_EQUAL_INT(1, msg.has_photo);
}

/* ── Test: Parse document message ───────────────────────────────────── */
static void test_telegram_parse_document(void)
{
    const char *json =
        "{\"update_id\":202,"
        "\"message\":{\"message_id\":502,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"document\":{\"file_id\":\"doc789\",\"file_name\":\"report.pdf\"}}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("doc789", msg.document_file_id);
    TEST_ASSERT_EQUAL_INT(1, msg.has_document);
}

/* ── Test: Parse video message ──────────────────────────────────────── */
static void test_telegram_parse_video(void)
{
    const char *json =
        "{\"update_id\":203,"
        "\"message\":{\"message_id\":503,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"video\":{\"file_id\":\"vid999\",\"duration\":45}}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("vid999", msg.video_file_id);
    TEST_ASSERT_EQUAL_INT(1, msg.has_video);
}

/* ── Test: Parse voice message ──────────────────────────────────────── */
static void test_telegram_parse_voice(void)
{
    const char *json =
        "{\"update_id\":204,"
        "\"message\":{\"message_id\":504,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"voice\":{\"file_id\":\"voice555\",\"duration\":10}}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("voice555", msg.voice_file_id);
    TEST_ASSERT_EQUAL_INT(1, msg.has_voice);
}

/* ── Test: Parse sticker message ────────────────────────────────────── */
static void test_telegram_parse_sticker(void)
{
    const char *json =
        "{\"update_id\":205,"
        "\"message\":{\"message_id\":505,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"sticker\":{\"file_id\":\"sticker777\"}}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("sticker777", msg.sticker_file_id);
    TEST_ASSERT_EQUAL_INT(1, msg.has_sticker);
}

/* ── Test: Photo message has empty text (crash root cause) ──────────── */
static void test_telegram_photo_message_has_empty_text(void)
{
    const char *json =
        "{\"update_id\":300,"
        "\"message\":{\"message_id\":600,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"photo\":[{\"file_id\":\"abc123\",\"width\":640,\"height\":480}]}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, msg.has_photo);
    TEST_ASSERT_EQUAL_STRING("", msg.text);  /* text is empty for photo-only messages */
}

/* ── Test: telegram_is_media_message detects photo ─────────────────── */
static void test_telegram_is_media_photo(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.has_photo = 1;
    TEST_ASSERT_EQUAL_INT(1, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message detects document ──────────────── */
static void test_telegram_is_media_document(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.has_document = 1;
    TEST_ASSERT_EQUAL_INT(1, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message detects video ─────────────────── */
static void test_telegram_is_media_video(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.has_video = 1;
    TEST_ASSERT_EQUAL_INT(1, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message detects voice ─────────────────── */
static void test_telegram_is_media_voice(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.has_voice = 1;
    TEST_ASSERT_EQUAL_INT(1, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message detects sticker ───────────────── */
static void test_telegram_is_media_sticker(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.has_sticker = 1;
    TEST_ASSERT_EQUAL_INT(1, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message returns 0 for text-only ─────── */
static void test_telegram_is_media_text_only(void)
{
    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.text, sizeof(msg.text), "hello");
    TEST_ASSERT_EQUAL_INT(0, telegram_is_media_message(&msg));
}

/* ── Test: telegram_is_media_message returns 0 for NULL ──────────── */
static void test_telegram_is_media_null(void)
{
    TEST_ASSERT_EQUAL_INT(0, telegram_is_media_message(NULL));
}

/* ── Test: Photo array selects last element (highest resolution) ─────── */
static void test_telegram_parse_photo_selects_last(void)
{
    const char *json =
        "{\"update_id\":400,"
        "\"message\":{\"message_id\":700,"
        "\"chat\":{\"id\":123456789},"
        "\"from\":{\"username\":\"johnk\"},"
        "\"photo\":["
        "{\"file_id\":\"small_id\",\"width\":90,\"height\":90},"
        "{\"file_id\":\"medium_id\",\"width\":320,\"height\":320},"
        "{\"file_id\":\"large_id\",\"width\":1280,\"height\":720}"
        "]}}";

    telegram_message_t msg;
    memset(&msg, 0, sizeof(msg));

    int rc = telegram_parse_update(json, &msg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, msg.has_photo);
    TEST_ASSERT_EQUAL_STRING("large_id", msg.photo_file_id);
}

/* ── Test: telegram_get_file_path extracts file_path from API ───────── */
static void test_telegram_get_file_path(void)
{
    mock_http_reset();
    mock_http_set_response(
        "{\"ok\":true,\"result\":{\"file_id\":\"abc123\","
        "\"file_path\":\"photos/file_0.jpg\"}}");

    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    char file_path[RELAY_MAX_VALUE];
    int rc = telegram_get_file_path(tg, "abc123", file_path, sizeof(file_path));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("photos/file_0.jpg", file_path);

    /* Verify the URL was correct */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url,
        "https://api.telegram.org/bottest_token/getFile?file_id=abc123"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: telegram_download_file calls get_to_file with correct URL ── */
static void test_telegram_download_file(void)
{
    mock_http_reset();

    const char *cfg_text =
        "telegram_bot_token = mytoken\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    int rc = telegram_download_file(tg, "photos/file_0.jpg",
                                     TEST_TMP_DIR "/test_photo.jpg");
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* Verify the download URL was correct */
    TEST_ASSERT_EQUAL_STRING(
        "https://api.telegram.org/file/botmytoken/photos/file_0.jpg",
        g_mock_http_last_url);

    /* Verify the local path was passed correctly */
    TEST_ASSERT_EQUAL_STRING(TEST_TMP_DIR "/test_photo.jpg",
                             g_mock_http_last_file_path);

    /* Clean up the mock file that get_to_file created */
    unlink(TEST_TMP_DIR "/test_photo.jpg");

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: Parse thumbs-up reaction ─────────────────────────────────── */
static void test_telegram_parse_reaction_thumbs_up(void)
{
    const char *json =
        "{\"update_id\":500,"
        "\"message_reaction\":{"
        "\"chat\":{\"id\":123456789},"
        "\"message_id\":100,"
        "\"user\":{\"username\":\"johnk\"},"
        "\"new_reaction\":[{\"type\":\"emoji\",\"emoji\":\"\xf0\x9f\x91\x8d\"}],"
        "\"old_reaction\":[]}}";

    telegram_reaction_t reaction;
    memset(&reaction, 0, sizeof(reaction));

    int rc = telegram_parse_reaction(json, &reaction);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("123456789", reaction.chat_id);
    TEST_ASSERT_EQUAL_INT(100, reaction.message_id);
    TEST_ASSERT_EQUAL_STRING("\xf0\x9f\x91\x8d", reaction.emoji);
}

/* ── Test: Parse reaction removal (empty new_reaction) ──────────────── */
static void test_telegram_parse_reaction_removal(void)
{
    const char *json =
        "{\"update_id\":501,"
        "\"message_reaction\":{"
        "\"chat\":{\"id\":123456789},"
        "\"message_id\":101,"
        "\"user\":{\"username\":\"johnk\"},"
        "\"new_reaction\":[],"
        "\"old_reaction\":[{\"type\":\"emoji\",\"emoji\":\"\xf0\x9f\x91\x8d\"}]}}";

    telegram_reaction_t reaction;
    memset(&reaction, 0, sizeof(reaction));

    int rc = telegram_parse_reaction(json, &reaction);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_INT(101, reaction.message_id);
    TEST_ASSERT_EQUAL_STRING("", reaction.emoji);
}

/* ── Test: Non-reaction update returns RELAY_ERR_NOTFOUND ────────────── */
static void test_telegram_parse_reaction_no_reaction_key(void)
{
    const char *json =
        "{\"update_id\":502,"
        "\"message\":{\"message_id\":200,"
        "\"chat\":{\"id\":123456789},"
        "\"text\":\"hello\"}}";

    telegram_reaction_t reaction;
    memset(&reaction, 0, sizeof(reaction));

    int rc = telegram_parse_reaction(json, &reaction);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_NOTFOUND, rc);
}

/* ── Test: Parent user DM is authorized ─────────────────────────────── */
static void test_telegram_parent_user_authorized_dm(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 111111111\n"
        "parent_telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* Child user is still authorized */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "111111111", NULL));

    /* Parent DM is authorized */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "123456789", NULL));

    /* Random user still denied */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "999999999", NULL));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: Parent user in group is NOT authorized ────────────────────── */
static void test_telegram_parent_user_not_authorized_in_group(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 111111111\n"
        "parent_telegram_user_id = 123456789\n"
        "telegram_group_chat_id = -100123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* Parent in group is denied — parent interacts via DM only */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "-100123456789", "123456789"));

    /* Child user via group still works as before */
    TEST_ASSERT_EQUAL_INT(1, telegram_is_authorized(tg, "-100123456789", "111111111"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: telegram_get_parent_user returns configured parent ID ──────── */
static void test_telegram_get_parent_user(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 111111111\n"
        "parent_telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    const char *parent = telegram_get_parent_user(tg);
    TEST_ASSERT_NOT_NULL(parent);
    TEST_ASSERT_EQUAL_STRING("123456789", parent);

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: telegram_get_parent_user returns "" when not configured ────── */
static void test_telegram_get_parent_user_not_configured(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    const char *parent = telegram_get_parent_user(tg);
    TEST_ASSERT_NOT_NULL(parent);
    TEST_ASSERT_EQUAL_STRING("", parent);

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: register_commands sends POST to setMyCommands URL ─────────── */
static void test_telegram_register_commands_happy_path(void)
{
    mock_http_reset();
    mock_http_set_response("{\"ok\":true,\"result\":true}");

    const char *cfg_text =
        "telegram_bot_token = testtoken\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    int rc = telegram_register_commands(tg);
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);

    /* URL must be the setMyCommands endpoint */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url,
        "https://api.telegram.org/bottesttoken/setMyCommands"));

    /* Body must contain all handled commands */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"start\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"help\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"clear\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"close\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"workspace\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"space\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"spaces\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"sessions\""));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "Select session"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: register_commands includes /reload command ───────────────── */
static void test_telegram_register_commands_includes_reload(void)
{
    mock_http_reset();
    mock_http_set_response("{\"ok\":true,\"result\":true}");

    const char *cfg_text =
        "telegram_bot_token = testtoken\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    telegram_register_commands(tg);

    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"reload\""));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Test: register_commands returns error on HTTP failure ───────────── */
static void test_telegram_register_commands_http_error(void)
{
    mock_http_reset();
    g_mock_http_status = RELAY_ERR_NETWORK;

    const char *cfg_text =
        "telegram_bot_token = testtoken\n"
        "telegram_user_id = 12345\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    int rc = telegram_register_commands(tg);
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);

    telegram_free(tg);
    config_free(cfg);
}

/* ── Tests: command authorization gate ──────────────────────────────── */

/* Unauthorized DM sender trying to send /status must be denied. */
static void test_telegram_unauthorized_command_dm_denied(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* An attacker sends /status from a different DM */
    TEST_ASSERT_EQUAL_INT(0, telegram_is_authorized(tg, "9999999999", NULL));

    telegram_free(tg);
    config_free(cfg);
}

/* Unauthorized group member trying to send /restart must be denied. */
static void test_telegram_unauthorized_command_group_denied(void)
{
    mock_http_reset();
    const char *cfg_text =
        "telegram_bot_token = test_token\n"
        "telegram_user_id = 123456789\n"
        "telegram_group_chat_id = -100123456789\n";

    config_t *cfg = config_load_string(cfg_text);
    telegram_t *tg = telegram_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(tg);

    /* Group member who is NOT the authorized user tries /restart */
    TEST_ASSERT_EQUAL_INT(0,
        telegram_is_authorized(tg, "-100123456789", "9999999999"));

    /* Authorized user in same group is allowed */
    TEST_ASSERT_EQUAL_INT(1,
        telegram_is_authorized(tg, "-100123456789", "123456789"));

    telegram_free(tg);
    config_free(cfg);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_telegram_suite(void)
{
    RUN_TEST(test_telegram_parse_text_message);
    RUN_TEST(test_telegram_parse_command);
    RUN_TEST(test_telegram_parse_no_message);
    RUN_TEST(test_telegram_parse_malformed);
    RUN_TEST(test_telegram_authorized_user);
    RUN_TEST(test_telegram_authorized_group_chat);
    RUN_TEST(test_telegram_no_group_chat);
    RUN_TEST(test_telegram_parse_from_id);
    RUN_TEST(test_telegram_parse_from_id_missing);
    RUN_TEST(test_telegram_message_mentions_agent_lowercase);
    RUN_TEST(test_telegram_message_mentions_agent_uppercase);
    RUN_TEST(test_telegram_message_mentions_agent_at_symbol);
    RUN_TEST(test_telegram_message_no_mention);
    RUN_TEST(test_telegram_message_kai_substring);
    RUN_TEST(test_telegram_message_empty_no_mention);
    RUN_TEST(test_telegram_message_mentions_custom_agent);
    RUN_TEST(test_telegram_message_custom_agent_no_kai);
    RUN_TEST(test_telegram_chunk_short);
    RUN_TEST(test_telegram_chunk_long);
    RUN_TEST(test_telegram_api_url);
    RUN_TEST(test_telegram_parse_photo_message);
    RUN_TEST(test_telegram_parse_photo_no_caption);
    RUN_TEST(test_telegram_parse_document);
    RUN_TEST(test_telegram_parse_video);
    RUN_TEST(test_telegram_parse_voice);
    RUN_TEST(test_telegram_parse_sticker);
    RUN_TEST(test_telegram_photo_message_has_empty_text);
    RUN_TEST(test_telegram_is_media_photo);
    RUN_TEST(test_telegram_is_media_document);
    RUN_TEST(test_telegram_is_media_video);
    RUN_TEST(test_telegram_is_media_voice);
    RUN_TEST(test_telegram_is_media_sticker);
    RUN_TEST(test_telegram_is_media_text_only);
    RUN_TEST(test_telegram_is_media_null);
    RUN_TEST(test_telegram_parse_photo_selects_last);
    RUN_TEST(test_telegram_get_file_path);
    RUN_TEST(test_telegram_download_file);
    RUN_TEST(test_telegram_parse_reaction_thumbs_up);
    RUN_TEST(test_telegram_parse_reaction_removal);
    RUN_TEST(test_telegram_parse_reaction_no_reaction_key);
    /* Parent user authorization */
    RUN_TEST(test_telegram_parent_user_authorized_dm);
    RUN_TEST(test_telegram_parent_user_not_authorized_in_group);
    RUN_TEST(test_telegram_get_parent_user);
    RUN_TEST(test_telegram_get_parent_user_not_configured);
    /* Command registration */
    RUN_TEST(test_telegram_register_commands_happy_path);
    RUN_TEST(test_telegram_register_commands_includes_reload);
    RUN_TEST(test_telegram_register_commands_http_error);
    /* Command authorization gate */
    RUN_TEST(test_telegram_unauthorized_command_dm_denied);
    RUN_TEST(test_telegram_unauthorized_command_group_denied);
}
