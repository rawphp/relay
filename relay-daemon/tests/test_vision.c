#include "Unity/unity.h"
#include "vision.h"
#include "mocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Parse tests (pure unit — no I/O) ──────────────────────────────── */

static void test_vision_parse_valid(void)
{
    char out[512];
    int rc = vision_parse_response(
        "{\"model\":\"moondream\","
        "\"response\":\"A fluffy orange cat sitting on a couch\","
        "\"done\":true}",
        out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("A fluffy orange cat sitting on a couch", out);
}

static void test_vision_parse_null_input(void)
{
    char out[64];
    int rc = vision_parse_response(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

static void test_vision_parse_null_out(void)
{
    int rc = vision_parse_response("{\"response\":\"hi\"}", NULL, 0);
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

static void test_vision_parse_missing_response_field(void)
{
    char out[64];
    int rc = vision_parse_response(
        "{\"model\":\"moondream\",\"done\":true}", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

static void test_vision_parse_malformed_json(void)
{
    char out[64];
    int rc = vision_parse_response("{not valid json{{", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

static void test_vision_parse_error_field(void)
{
    char out[64];
    int rc = vision_parse_response(
        "{\"error\":\"model 'foo' not found\"}", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);
}

static void test_vision_parse_empty_response_field(void)
{
    char out[64];
    int rc = vision_parse_response(
        "{\"model\":\"moondream\",\"response\":\"\",\"done\":true}",
        out, sizeof(out));
    /* Empty response string is not useful — should be a parse error */
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);
}

/* ── Create tests ───────────────────────────────────────────────────── */

static void test_vision_create_null_http(void)
{
    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(NULL, cfg);
    TEST_ASSERT_NULL(v);
    config_free(cfg);
}

static void test_vision_create_null_cfg(void)
{
    vision_t *v = vision_create(&g_mock_http, NULL);
    TEST_ASSERT_NULL(v);
}

static void test_vision_create_with_model_enabled(void)
{
    const char *cfg_text =
        "vision_model = moondream\n"
        "vision_ollama_url = http://localhost:11434\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(1, vision_is_enabled(v));
    vision_free(v);
    config_free(cfg);
}

static void test_vision_create_without_model_disabled(void)
{
    /* No vision_model key → disabled */
    const char *cfg_text = "llm_provider = openai_codex\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(0, vision_is_enabled(v));
    vision_free(v);
    config_free(cfg);
}

static void test_vision_is_enabled_null(void)
{
    TEST_ASSERT_EQUAL_INT(0, vision_is_enabled(NULL));
}

/* ── Describe tests ─────────────────────────────────────────────────── */

static void test_vision_describe_null_args(void)
{
    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    char out[256];

    TEST_ASSERT_EQUAL_INT(RELAY_ERR,
        vision_describe(NULL, TEST_TMP_DIR "/img.jpg", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR,
        vision_describe(v, NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR,
        vision_describe(v, TEST_TMP_DIR "/img.jpg", NULL, sizeof(out)));

    vision_free(v);
    config_free(cfg);
}

static void test_vision_describe_success(void)
{
    /* Write a small fake "image" file */
    const char *tmp = TEST_TMP_DIR "/relay_vision_test.dat";
    FILE *fp = fopen(tmp, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    /* Minimal JPEG-like header bytes + some padding */
    const unsigned char fake_img[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x74, 0x65, 0x73, 0x74, 0x2d, 0x69, 0x6d, 0x61, 0x67, 0x65, 0x2d, 0x64
    };
    fwrite(fake_img, 1, sizeof(fake_img), fp);
    fclose(fp);

    mock_http_reset();
    mock_http_set_response(
        "{\"model\":\"moondream\","
        "\"response\":\"A test image with some data\","
        "\"done\":true}");

    const char *cfg_text =
        "vision_model = moondream\n"
        "vision_ollama_url = http://localhost:11434\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[512];
    int rc = vision_describe(v, tmp, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("A test image with some data", out);

    /* Verify the HTTP call used the correct endpoint */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url, "/api/generate"));

    /* Verify the POST body contains the model name and images field */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "moondream"));
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_body, "\"images\""));

    vision_free(v);
    config_free(cfg);
    remove(tmp);
}

static void test_vision_describe_http_failure(void)
{
    const char *tmp = TEST_TMP_DIR "/relay_vision_test2.dat";
    FILE *fp = fopen(tmp, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite("test", 1, 4, fp);
    fclose(fp);

    mock_http_reset();
    g_mock_http_status = RELAY_ERR;

    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[256];
    int rc = vision_describe(v, tmp, out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);

    vision_free(v);
    config_free(cfg);
    remove(tmp);
}

static void test_vision_describe_file_not_found(void)
{
    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[256];
    int rc = vision_describe(v,
        TEST_TMP_DIR "/relay_nonexistent_file_xyz_99999.jpg", out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);

    vision_free(v);
    config_free(cfg);
}

static void test_vision_describe_bad_json_response(void)
{
    const char *tmp = TEST_TMP_DIR "/relay_vision_test3.dat";
    FILE *fp = fopen(tmp, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite("px", 1, 2, fp);
    fclose(fp);

    mock_http_reset();
    mock_http_set_response("not valid json at all{{{");

    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[256];
    int rc = vision_describe(v, tmp, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_PARSE, rc);

    vision_free(v);
    config_free(cfg);
    remove(tmp);
}

static void test_vision_describe_ollama_error_response(void)
{
    const char *tmp = TEST_TMP_DIR "/relay_vision_test4.dat";
    FILE *fp = fopen(tmp, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite("data", 1, 4, fp);
    fclose(fp);

    mock_http_reset();
    mock_http_set_response("{\"error\":\"model 'moondream' not found, try pulling it first\"}");

    const char *cfg_text = "vision_model = moondream\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[256];
    int rc = vision_describe(v, tmp, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_ERR, rc);

    vision_free(v);
    config_free(cfg);
    remove(tmp);
}

static void test_vision_describe_uses_configured_url(void)
{
    const char *tmp = TEST_TMP_DIR "/relay_vision_test5.dat";
    FILE *fp = fopen(tmp, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite("img", 1, 3, fp);
    fclose(fp);

    mock_http_reset();
    mock_http_set_response(
        "{\"model\":\"llava\",\"response\":\"A picture.\",\"done\":true}");

    const char *cfg_text =
        "vision_model = llava\n"
        "vision_ollama_url = http://192.168.1.100:11434\n";
    config_t *cfg = config_load_string(cfg_text);
    vision_t *v = vision_create(&g_mock_http, cfg);
    TEST_ASSERT_NOT_NULL(v);

    char out[256];
    int rc = vision_describe(v, tmp, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    /* URL should contain the configured host */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url, "192.168.1.100"));

    vision_free(v);
    config_free(cfg);
    remove(tmp);
}

/* ── Suite registration ─────────────────────────────────────────────── */

void test_vision_suite(void)
{
    RUN_TEST(test_vision_parse_valid);
    RUN_TEST(test_vision_parse_null_input);
    RUN_TEST(test_vision_parse_null_out);
    RUN_TEST(test_vision_parse_missing_response_field);
    RUN_TEST(test_vision_parse_malformed_json);
    RUN_TEST(test_vision_parse_error_field);
    RUN_TEST(test_vision_parse_empty_response_field);
    RUN_TEST(test_vision_create_null_http);
    RUN_TEST(test_vision_create_null_cfg);
    RUN_TEST(test_vision_create_with_model_enabled);
    RUN_TEST(test_vision_create_without_model_disabled);
    RUN_TEST(test_vision_is_enabled_null);
    RUN_TEST(test_vision_describe_null_args);
    RUN_TEST(test_vision_describe_success);
    RUN_TEST(test_vision_describe_http_failure);
    RUN_TEST(test_vision_describe_file_not_found);
    RUN_TEST(test_vision_describe_bad_json_response);
    RUN_TEST(test_vision_describe_ollama_error_response);
    RUN_TEST(test_vision_describe_uses_configured_url);
}
