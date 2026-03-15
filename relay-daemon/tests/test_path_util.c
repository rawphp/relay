#include "Unity/unity.h"
#include "path_util.h"
#include "relay.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Test: binary-relative config found → returned ───────────────────── */

static void test_resolve_default_config_path_found(void)
{
    /* Build a tmp dir tree: tmp/rtest_bin/bin/ and tmp/rtest_bin/config/ */
    const char *base  = TEST_TMP_DIR "/rtest_bin";
    const char *bindir = TEST_TMP_DIR "/rtest_bin/bin";
    const char *cfgdir = TEST_TMP_DIR "/rtest_bin/config";
    const char *argv0  = TEST_TMP_DIR "/rtest_bin/bin/relay";
    const char *cfgfile = TEST_TMP_DIR "/rtest_bin/config/relay.conf";

    /* Clean up from any prior run */
    unlink(argv0);
    unlink(cfgfile);
    rmdir(bindir);
    rmdir(cfgdir);
    rmdir(base);

    /* Create the directory tree */
    mkdir(base, 0755);
    mkdir(bindir, 0755);
    mkdir(cfgdir, 0755);

    /* Create dummy binary file and config file */
    FILE *f = fopen(argv0, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("dummy", f);
    fclose(f);

    f = fopen(cfgfile, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("dummy_config", f);
    fclose(f);

    /* Call the helper */
    char result[1024];
    resolve_default_config_path(argv0, result, sizeof(result));

    /* Should resolve to the binary-relative config, not the CWD fallback */
    TEST_ASSERT_NOT_EQUAL(0, strcmp(result, "config/relay.conf"));
    TEST_ASSERT_EQUAL_INT(0, access(result, F_OK));

    /* Clean up */
    unlink(argv0);
    unlink(cfgfile);
    rmdir(bindir);
    rmdir(cfgdir);
    rmdir(base);
}

/* ── Test: binary-relative config absent → CWD fallback returned ─────── */

static void test_resolve_default_config_path_fallback(void)
{
    /* Use a path whose ../config/relay.conf definitely does not exist */
    const char *argv0 = "/nonexistent/bin/relay";

    char result[1024];
    resolve_default_config_path(argv0, result, sizeof(result));

    /* Should fall back to the literal default */
    TEST_ASSERT_EQUAL_STRING("config/relay.conf", result);
}

/* ── REQ-134: Claude path encode/decode ───────────────────────────────── */

static void test_encode_claude_dir_normal_path(void)
{
    char out[256];
    path_util_encode_claude_dir("/Users/tom/project", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-Users-tom-project", out);
}

static void test_encode_claude_dir_root_path(void)
{
    char out[256];
    path_util_encode_claude_dir("/", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-", out);
}

static void test_encode_claude_dir_empty_string(void)
{
    char out[256];
    path_util_encode_claude_dir("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_encode_claude_dir_trailing_slash(void)
{
    char out[256];
    path_util_encode_claude_dir("/Users/tom/project/", out, sizeof(out));
    /* Trailing slash stripped before encoding */
    TEST_ASSERT_EQUAL_STRING("-Users-tom-project", out);
}

static void test_decode_claude_dir_normal(void)
{
    char out[256];
    path_util_decode_claude_dir("-Users-tom-project", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/Users/tom/project", out);
}

static void test_decode_claude_dir_root(void)
{
    char out[256];
    path_util_decode_claude_dir("-", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("/", out);
}

static void test_decode_claude_dir_empty(void)
{
    char out[256];
    path_util_decode_claude_dir("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_encode_decode_round_trip(void)
{
    const char *original = "/Users/tomkaczocha/EA/projects/relay";
    char encoded[256];
    char decoded[256];
    path_util_encode_claude_dir(original, encoded, sizeof(encoded));
    path_util_decode_claude_dir(encoded, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL_STRING(original, decoded);
}

/* ── REQ-140: install dir resolution ──────────────────────────────────── */

static void test_install_dir_normal(void)
{
    char out[256];
    int rc = path_util_install_dir("/home/kai/config/relay.conf", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("/home/kai", out);
}

static void test_install_dir_deep_path(void)
{
    char out[256];
    int rc = path_util_install_dir("/Users/tom/agents/kai/config/relay.conf", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
    TEST_ASSERT_EQUAL_STRING("/Users/tom/agents/kai", out);
}

static void test_install_dir_wrong_suffix(void)
{
    char out[256];
    int rc = path_util_install_dir("/home/kai/other.conf", out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
}

static void test_install_dir_null(void)
{
    char out[256];
    int rc = path_util_install_dir(NULL, out, sizeof(out));
    TEST_ASSERT_NOT_EQUAL(RELAY_OK, rc);
}

/* ── Suite ────────────────────────────────────────────────────────────── */

void test_path_util_suite(void)
{
    RUN_TEST(test_resolve_default_config_path_found);
    RUN_TEST(test_resolve_default_config_path_fallback);
    /* REQ-134 */
    RUN_TEST(test_encode_claude_dir_normal_path);
    RUN_TEST(test_encode_claude_dir_root_path);
    RUN_TEST(test_encode_claude_dir_empty_string);
    RUN_TEST(test_encode_claude_dir_trailing_slash);
    RUN_TEST(test_decode_claude_dir_normal);
    RUN_TEST(test_decode_claude_dir_root);
    RUN_TEST(test_decode_claude_dir_empty);
    RUN_TEST(test_encode_decode_round_trip);
    /* REQ-140 */
    RUN_TEST(test_install_dir_normal);
    RUN_TEST(test_install_dir_deep_path);
    RUN_TEST(test_install_dir_wrong_suffix);
    RUN_TEST(test_install_dir_null);
}
