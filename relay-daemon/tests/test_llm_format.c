#include "Unity/unity.h"
#include "llm_format.h"
#include <string.h>

/* ── Test: Append to empty buffer ─────────────────────────── */
static void test_append_to_empty_buffer(void)
{
    char buf[1024] = {0};

    llm_append_structured_format_instructions(buf, sizeof(buf));

    TEST_ASSERT_TRUE(strlen(buf) > 0);
    TEST_ASSERT_TRUE(strstr(buf, "Formatting instructions:") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "## Section Title") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "**1. Subheading**") != NULL);
}

/* ── Test: Append to buffer with existing content ─────────────────────────── */
static void test_append_to_existing_content(void)
{
    char buf[1024] = "Existing prompt text";

    llm_append_structured_format_instructions(buf, sizeof(buf));

    TEST_ASSERT_TRUE(strstr(buf, "Existing prompt text") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\n\nFormatting instructions:") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "## Section Title") != NULL);
}

/* ── Test: NULL buffer handled gracefully ─────────────────────────── */
static void test_null_buffer(void)
{
    llm_append_structured_format_instructions(NULL, 1024);
    // Should not crash
    TEST_ASSERT_TRUE(1);
}

/* ── Test: Zero max size handled gracefully ─────────────────────────── */
static void test_zero_max_size(void)
{
    char buf[1024] = "test";

    llm_append_structured_format_instructions(buf, 0);

    // Buffer should be unchanged
    TEST_ASSERT_EQUAL_STRING("test", buf);
}

/* ── Test: Buffer almost full - should still append if room ─────────────────────────── */
static void test_buffer_almost_full(void)
{
    char buf[100] = {0};
    // Fill buffer to near capacity
    memset(buf, 'x', 95);
    buf[95] = '\0';

    llm_append_structured_format_instructions(buf, sizeof(buf));

    // Should append partial content (has 4 bytes remaining)
    TEST_ASSERT_GREATER_OR_EQUAL(95, strlen(buf));  // At least original size
    TEST_ASSERT_LESS_OR_EQUAL(99, strlen(buf));     // Should not overflow (max len is max-1)
}

/* ── Test: Buffer at max-1 should not append ─────────────────────────── */
static void test_buffer_exactly_at_limit(void)
{
    char buf[100] = {0};
    memset(buf, 'x', 99);  // max - 1
    buf[99] = '\0';

    llm_append_structured_format_instructions(buf, sizeof(buf));

    // Should not append (used >= max - 1)
    TEST_ASSERT_EQUAL_INT(99, strlen(buf));
}

/* ── Test: Small buffer truncates gracefully ─────────────────────────── */
static void test_small_buffer_truncation(void)
{
    char buf[50] = "prompt";
    size_t initial_len = strlen(buf);

    llm_append_structured_format_instructions(buf, sizeof(buf));

    size_t final_len = strlen(buf);

    // Should append something (even if truncated)
    TEST_ASSERT_GREATER_THAN(initial_len, final_len);
    // Should not overflow (max valid strlen is sizeof(buf)-1)
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(buf) - 1, final_len);
}

/* ── Test: Content includes required formatting elements ─────────────────────────── */
static void test_required_formatting_elements(void)
{
    char buf[2048] = {0};

    llm_append_structured_format_instructions(buf, sizeof(buf));

    // Check for key formatting elements
    TEST_ASSERT_TRUE(strstr(buf, "##") != NULL);              // Headings
    TEST_ASSERT_TRUE(strstr(buf, "**") != NULL);              // Bold subheadings
    TEST_ASSERT_TRUE(strstr(buf, "-") != NULL);               // Bullets
    TEST_ASSERT_TRUE(strstr(buf, "Rules:") != NULL);          // Rules section
    TEST_ASSERT_TRUE(strstr(buf, "imperative") != NULL);      // Instructions
    TEST_ASSERT_TRUE(strstr(buf, "bullet") != NULL);          // Key term
}

/* ── Test: Multiple appends don't duplicate ─────────────────────────── */
static void test_multiple_appends(void)
{
    char buf[2048] = "Original content";

    llm_append_structured_format_instructions(buf, sizeof(buf));
    size_t len_after_first = strlen(buf);

    // Trying to append again should still work, appending more instructions
    llm_append_structured_format_instructions(buf, sizeof(buf));
    size_t len_after_second = strlen(buf);

    // Second append should add more content (not a no-op)
    TEST_ASSERT_GREATER_THAN(len_after_first, len_after_second);
}

/* ── Test: Empty string appends correctly ─────────────────────────── */
static void test_empty_string_buffer(void)
{
    char buf[1024];
    buf[0] = '\0';  // Explicit empty string

    llm_append_structured_format_instructions(buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0, strlen(buf));
    TEST_ASSERT_TRUE(strstr(buf, "Formatting instructions:") != NULL);
}

/* ── Test suite ─────────────────────────────────────────────── */
void test_llm_format_suite(void)
{
    RUN_TEST(test_append_to_empty_buffer);
    RUN_TEST(test_append_to_existing_content);
    RUN_TEST(test_null_buffer);
    RUN_TEST(test_zero_max_size);
    RUN_TEST(test_buffer_almost_full);
    RUN_TEST(test_buffer_exactly_at_limit);
    RUN_TEST(test_small_buffer_truncation);
    RUN_TEST(test_required_formatting_elements);
    RUN_TEST(test_multiple_appends);
    RUN_TEST(test_empty_string_buffer);
}
