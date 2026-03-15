#include "Unity/unity.h"
#include "memory_curator.h"
#include "llm_provider.h"
#include "mocks.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Valid Claude JSON response wrapping the given result text */
#define CLAUDE_JSON(result) \
    "{\"type\":\"result\",\"session_id\":\"\",\"result\":" result ",\"duration_ms\":0}"

/* Stage-1 response: one fact */
#define STAGE1_ONE_FACT \
    CLAUDE_JSON("\"[\\\"John drinks coffee\\\"]\"")

/* Stage-1 response: empty facts array (Stage 2 skipped) */
#define STAGE1_EMPTY \
    CLAUDE_JSON("\"[]\"")

/* Stage-2 response: ADD with high confidence */
#define STAGE2_ADD_HIGH \
    CLAUDE_JSON("\"[{\\\"action\\\":\\\"ADD\\\",\\\"text\\\":\\\"John drinks coffee\\\",\\\"confidence\\\":0.9,\\\"reasoning\\\":\\\"New fact\\\"}]\"")

/* Stage-2 response: ADD with low confidence */
#define STAGE2_ADD_LOW \
    CLAUDE_JSON("\"[{\\\"action\\\":\\\"ADD\\\",\\\"text\\\":\\\"John drinks coffee\\\",\\\"confidence\\\":0.5,\\\"reasoning\\\":\\\"Uncertain\\\"}]\"")

/* Stage-2 response: DELETE with high confidence */
#define STAGE2_DELETE_HIGH \
    CLAUDE_JSON("\"[{\\\"action\\\":\\\"DELETE\\\",\\\"memory_id\\\":\\\"mem-abc\\\",\\\"text\\\":\\\"\\\",\\\"confidence\\\":0.9,\\\"reasoning\\\":\\\"Outdated\\\"}]\"")

static const char *k_cfg_base =
    "claude_binary = /usr/bin/claude\n"
    "workspace_path = /tmp/agent\n"
    "memory_service_url = http://localhost:8765\n";

/* ── Test: Stage-1 prompt contains the conversation turns ───────────── */
static void test_fact_extraction_prompt_contains_turns(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_fs_reset();

    /* Flush on every turn so Stage 1 fires immediately */
    const char *cfg_text =
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /tmp/agent\n"
        "memory_flush_every_n_turns = 1\n";

    /* Stage 1 returns empty facts → Stage 2 skipped */
    mock_proc_set_output(STAGE1_EMPTY);

    config_t *cfg = config_load_string(cfg_text);
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    memory_curator_t *mc = memory_curator_create(llm, &g_mock_http, &g_mock_fs, cfg);
    TEST_ASSERT_NOT_NULL(mc);

    memory_curator_on_turn(mc, "I love coffee", "Good to know!", "/tmp/agent");

    /* Proc was called once (Stage 1) */
    TEST_ASSERT_EQUAL_INT(1, mock_proc_call_count());
    /* Stage 1 prompt passed as input to proc must contain the user message */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_proc_last_input, "coffee"));

    memory_curator_free(mc);
    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Test: Low-confidence operation goes to review queue ─────────────── */
static void test_low_confidence_goes_to_review_queue(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_fs_reset();

    /* Stage 1 (call 1): returns one fact */
    mock_proc_set_output(STAGE1_ONE_FACT);
    /* Stage 2 (call 2+): returns low-confidence ADD */
    mock_proc_set_output_after_n_calls(1, STAGE2_ADD_LOW);

    /* Sidecar search: no existing memories */
    mock_http_set_response("No results found.");

    config_t *cfg = config_load_string(k_cfg_base);
    config_t *cfg1 = config_load_string(
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /tmp/agent\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_flush_every_n_turns = 1\n");
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg1);
    memory_curator_t *mc = memory_curator_create(llm, &g_mock_http, &g_mock_fs, cfg1);
    TEST_ASSERT_NOT_NULL(mc);

    memory_curator_on_turn(mc, "John drinks coffee", "Noted!", "/tmp/agent");

    /* Review queue should have been written */
    TEST_ASSERT_NOT_NULL(
        mock_fs_find("/tmp/agent/data/memory/review_queue.jsonl"));
    /* No upsert or delete should have been called (last HTTP is search) */
    TEST_ASSERT_NULL(strstr(g_mock_http_last_url, "upsert"));
    TEST_ASSERT_NULL(strstr(g_mock_http_last_url, "delete"));

    memory_curator_free(mc);
    llm_provider_free(llm);
    config_free(cfg);
    config_free(cfg1);
}

/* ── Test: ADD operation with high confidence calls /upsert ─────────── */
static void test_add_operation_calls_upsert(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_fs_reset();

    /* Stage 1 (call 1): returns one fact */
    mock_proc_set_output(STAGE1_ONE_FACT);
    /* Stage 2 (call 2+): returns high-confidence ADD */
    mock_proc_set_output_after_n_calls(1, STAGE2_ADD_HIGH);

    mock_http_set_response("No results found.");

    config_t *cfg = config_load_string(
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /tmp/agent\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_flush_every_n_turns = 1\n");
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    memory_curator_t *mc = memory_curator_create(llm, &g_mock_http, &g_mock_fs, cfg);
    TEST_ASSERT_NOT_NULL(mc);

    memory_curator_on_turn(mc, "John drinks coffee", "Got it!", "/tmp/agent");

    /* Last HTTP call should be /upsert */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url, "upsert"));

    memory_curator_free(mc);
    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Test: DELETE operation with high confidence calls /delete ───────── */
static void test_delete_operation_calls_delete(void)
{
    mock_proc_reset();
    mock_http_reset();
    mock_fs_reset();

    /* Stage 1 (call 1): returns one fact */
    mock_proc_set_output(STAGE1_ONE_FACT);
    /* Stage 2 (call 2+): returns high-confidence DELETE */
    mock_proc_set_output_after_n_calls(1, STAGE2_DELETE_HIGH);

    mock_http_set_response("No results found.");

    config_t *cfg = config_load_string(
        "claude_binary = /usr/bin/claude\n"
        "workspace_path = /tmp/agent\n"
        "memory_service_url = http://localhost:8765\n"
        "memory_flush_every_n_turns = 1\n");
    llm_provider_t *llm = llm_provider_create(&g_mock_proc, cfg);
    memory_curator_t *mc = memory_curator_create(llm, &g_mock_http, &g_mock_fs, cfg);
    TEST_ASSERT_NOT_NULL(mc);

    memory_curator_on_turn(mc, "Old info", "Understood!", "/tmp/agent");

    /* Last HTTP call should be /delete */
    TEST_ASSERT_NOT_NULL(strstr(g_mock_http_last_url, "delete"));

    memory_curator_free(mc);
    llm_provider_free(llm);
    config_free(cfg);
}

/* ── Suite registration ─────────────────────────────────────────────── */
void test_memory_curator_suite(void)
{
    RUN_TEST(test_fact_extraction_prompt_contains_turns);
    RUN_TEST(test_low_confidence_goes_to_review_queue);
    RUN_TEST(test_add_operation_calls_upsert);
    RUN_TEST(test_delete_operation_calls_delete);
}
