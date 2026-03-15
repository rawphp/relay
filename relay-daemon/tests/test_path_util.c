#include "Unity/unity.h"
#include "path_util.h"

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

/* ── Suite ────────────────────────────────────────────────────────────── */

void test_path_util_suite(void)
{
    RUN_TEST(test_resolve_default_config_path_found);
    RUN_TEST(test_resolve_default_config_path_fallback);
}
