#include "Unity/unity.h"
#include "llm_prompt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/* ── helpers ─────────────────────────────────────────────────────────── */

static void mkdirs_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

/* ── Test: compaction prompt loaded from file, {date} substituted ───── */
static void test_compaction_prompt_from_file(void)
{
    const char *workspace = TEST_TMP_DIR "/test_compaction_ws";
    char prompts_dir[256];
    snprintf(prompts_dir, sizeof(prompts_dir), "%s/data/prompts", workspace);
    mkdirs_p(prompts_dir);

    char prompt_path[512];
    snprintf(prompt_path, sizeof(prompt_path), "%s/compaction.txt", prompts_dir);
    write_file(prompt_path, "Write memory to data/memory/{date}.md. Keep it sharp.");

    char buf[1024];
    llm_load_compaction_prompt(buf, sizeof(buf), workspace, "2026-03-01");

    /* Should contain the substituted date, not the placeholder */
    TEST_ASSERT_NOT_NULL(strstr(buf, "2026-03-01"));
    TEST_ASSERT_NULL(strstr(buf, "{date}"));

    /* Should contain the file's distinctive text */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Keep it sharp"));

    /* Clean up */
    remove(prompt_path);
    rmdir(prompts_dir);
    rmdir(TEST_TMP_DIR "/test_compaction_ws/data");
    rmdir(TEST_TMP_DIR "/test_compaction_ws");
}

/* ── Test: fallback when file doesn't exist ─────────────────────────── */
static void test_compaction_prompt_fallback(void)
{
    char buf[1024];
    llm_load_compaction_prompt(buf, sizeof(buf), "/nonexistent/path", "2026-03-01");

    /* Fallback must be non-empty */
    TEST_ASSERT_TRUE(strlen(buf) > 10);

    /* Fallback should still mention the date */
    TEST_ASSERT_NOT_NULL(strstr(buf, "2026-03-01"));

    /* Should not contain unfilled placeholder */
    TEST_ASSERT_NULL(strstr(buf, "{date}"));
}

/* ── Test: empty compaction file falls back to default ─────────────── */
static void test_compaction_prompt_empty_file_fallback(void)
{
    const char *workspace = TEST_TMP_DIR "/test_compaction_empty_ws";
    char prompts_dir[256];
    snprintf(prompts_dir, sizeof(prompts_dir), "%s/data/prompts", workspace);
    mkdirs_p(prompts_dir);

    char prompt_path[512];
    snprintf(prompt_path, sizeof(prompt_path), "%s/compaction.txt", prompts_dir);
    write_file(prompt_path, "");

    char buf[1024];
    llm_load_compaction_prompt(buf, sizeof(buf), workspace, "2026-03-01");

    /* Fallback must be non-empty */
    TEST_ASSERT_TRUE(strlen(buf) > 10);

    /* Should still contain date */
    TEST_ASSERT_NOT_NULL(strstr(buf, "2026-03-01"));

    remove(prompt_path);
    rmdir(prompts_dir);
    rmdir(TEST_TMP_DIR "/test_compaction_empty_ws/data");
    rmdir(TEST_TMP_DIR "/test_compaction_empty_ws");
}

/* ── Suite ───────────────────────────────────────────────────────────── */

void test_llm_prompt_suite(void)
{
    RUN_TEST(test_compaction_prompt_from_file);
    RUN_TEST(test_compaction_prompt_fallback);
    RUN_TEST(test_compaction_prompt_empty_file_fallback);
}
