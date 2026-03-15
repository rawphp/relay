#include "Unity/unity.h"

/* Unity requires setUp/tearDown even if empty */
void setUp(void) {}
void tearDown(void) {}

/* Test suite declarations */
extern void test_config_suite(void);
extern void test_config_validator_suite(void);
extern void test_llm_format_suite(void);
extern void test_log_suite(void);
extern void test_session_suite(void);
extern void test_claude_suite(void);
extern void test_openai_codex_suite(void);
extern void test_gemini_suite(void);
extern void test_llm_provider_suite(void);
extern void test_telegram_suite(void);
extern void test_transcript_suite(void);
extern void test_health_suite(void);
extern void test_vision_suite(void);
extern void test_interruption_suite(void);
extern void test_memory_search_suite(void);
extern void test_fs_permissions_suite(void);
extern void group_chat_context_suite(void);
extern void pending_bus_messages_suite(void);
extern void test_llm_prompt_suite(void);
extern void test_pending_response_suite(void);
extern void test_profiler_suite(void);
extern void test_proc_log_partial_suite(void);
extern void test_stream_timeout_suite(void);
extern void test_agent_bus_suite(void);
extern void test_cmd_workspace_suite(void);
extern void test_workspace_resolver_suite(void);
extern void test_path_util_suite(void);
extern void test_event_loop_suite(void);
extern void test_memory_sidecar_c_suite(void);
extern void test_memory_curator_suite(void);
extern void test_pid_file_suite(void);
extern void test_telegram_offset_suite(void);
extern void test_claudecode_env_suite(void);
extern void test_health_alert_suite(void);
extern void test_spawn_diag_suite(void);
extern void test_voice_pipeline_suite(void);
extern void test_session_discovery_suite(void);
extern void test_cmd_sessions_suite(void);
extern void test_peer_registry_suite(void);
extern void test_bus_directive_suite(void);
extern void test_agent_advertise_suite(void);

int main(void)
{
    UNITY_BEGIN();

    test_config_suite();
    test_config_validator_suite();
    test_llm_format_suite();
    test_log_suite();
    test_session_suite();
    test_claude_suite();
    test_openai_codex_suite();
    test_gemini_suite();
    test_llm_provider_suite();
    test_telegram_suite();
    test_transcript_suite();
    test_health_suite();
    test_vision_suite();
    test_interruption_suite();
    test_memory_search_suite();
    test_fs_permissions_suite();
    group_chat_context_suite();
    pending_bus_messages_suite();
    test_llm_prompt_suite();
    test_pending_response_suite();
    test_profiler_suite();
    test_proc_log_partial_suite();
    test_stream_timeout_suite();
    test_agent_bus_suite();
    test_cmd_workspace_suite();
    test_workspace_resolver_suite();
    test_path_util_suite();
    test_event_loop_suite();
    test_memory_sidecar_c_suite();
    test_memory_curator_suite();
    test_pid_file_suite();
    test_telegram_offset_suite();
    test_claudecode_env_suite();
    test_health_alert_suite();
    test_spawn_diag_suite();
    test_voice_pipeline_suite();
    test_session_discovery_suite();
    test_cmd_sessions_suite();
    test_peer_registry_suite();
    test_bus_directive_suite();
    test_agent_advertise_suite();

    return UNITY_END();
}
