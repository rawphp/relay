#include "memory_curator.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Constants ───────────────────────────────────────────────────────── */

#define MAX_CURATOR_TURNS   10      /* ring buffer depth */
#define MAX_CURATOR_TURN_LEN 512    /* max bytes stored per user/agent msg */
#define MAX_CURATOR_FACTS   20      /* max facts extracted per flush */
#define MAX_CURATOR_FACT_LEN 512    /* max bytes per extracted fact */
#define CONFIDENCE_THRESHOLD 0.7    /* min confidence to apply immediately */

/* ── Internal struct ─────────────────────────────────────────────────── */

struct memory_curator {
    llm_provider_t *llm;
    relay_http_t   *http;
    relay_fs_t     *fs;
    char search_url[RELAY_MAX_URL];   /* memory_service_url from config */

    /* Ring buffer of recent conversation turns */
    char user_msgs[MAX_CURATOR_TURNS][MAX_CURATOR_TURN_LEN];
    char agent_msgs[MAX_CURATOR_TURNS][MAX_CURATOR_TURN_LEN];
    int  stored_turns;   /* number of turns currently buffered (0..MAX) */

    int  turn_count;     /* total turns observed since creation */
    int  flush_every_n;  /* flush when turn_count % flush_every_n == 0 */
};

/* ── Simple FNV-1a hash for stable doc_id generation ─────────────────── */

static unsigned int fnv1a_32(const char *s)
{
    unsigned int h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned int)(unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

/* ── Stage 1: fact extraction ─────────────────────────────────────────── */

/* Build the fact-extraction prompt and call the LLM.
 * Returns number of facts extracted (0 on failure or no facts).
 * facts[][MAX_CURATOR_FACT_LEN] must have at least MAX_CURATOR_FACTS rows. */
static int stage1_extract_facts(memory_curator_t *mc,
                                 char facts[][MAX_CURATOR_FACT_LEN])
{
    /* Prompt: header + conversation turns */
    char prompt[16384];
    int w = 0;

    w += snprintf(prompt + w, sizeof(prompt) - (size_t)w,
        "Extract discrete, atomic, reusable facts from these conversation "
        "turns.\nReply with ONLY a JSON string array of facts. "
        "Example: [\"The user prefers dark mode\", \"Alice is 10 years old\"]\n"
        "If no new facts, reply with: []\n\n");

    for (int i = 0; i < mc->stored_turns && w < (int)sizeof(prompt) - 1; i++) {
        w += snprintf(prompt + w, sizeof(prompt) - (size_t)w,
                      "[user]: %s\n[agent]: %s\n\n",
                      mc->user_msgs[i], mc->agent_msgs[i]);
    }

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(mc->llm, prompt, NULL, &resp);
    if (rc != RELAY_OK || resp.is_error || resp.result[0] == '\0') {
        return 0;
    }

    /* Parse JSON array from resp.result */
    cJSON *root = cJSON_Parse(resp.result);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return 0;
    }

    int count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsString(item) && count < MAX_CURATOR_FACTS) {
            snprintf(facts[count], MAX_CURATOR_FACT_LEN,
                     "%s", item->valuestring);
            count++;
        }
    }
    cJSON_Delete(root);
    return count;
}

/* ── Stage 2: memory decision ─────────────────────────────────────────── */

/* Search the sidecar for existing memories similar to fact.
 * Fills search_out (must be RELAY_MAX_MSG bytes) and returns 1 if results
 * found, 0 if "No results found." or HTTP error. */
static int sidecar_search(memory_curator_t *mc, const char *fact,
                           char *search_out, size_t search_max)
{
    char url[RELAY_MAX_URL];
    char body[MAX_CURATOR_FACT_LEN + 32];

    if (!mc->http || mc->search_url[0] == '\0') {
        search_out[0] = '\0';
        return 0;
    }

    snprintf(url, sizeof(url), "%s/search", mc->search_url);
    snprintf(body, sizeof(body),
             "{\"query\":\"%s\",\"top_k\":5}", fact);

    int rc = mc->http->post(url, body, search_out, search_max);
    if (rc != RELAY_OK) {
        search_out[0] = '\0';
        return 0;
    }
    if (strstr(search_out, "No results found") != NULL) {
        search_out[0] = '\0';
        return 0;
    }
    return 1;
}

/* Apply a single memory operation returned by the decision LLM.
 * High-confidence ops are applied to the sidecar; low-confidence ops
 * are queued to review_queue.jsonl. */
static void apply_operation(memory_curator_t *mc, cJSON *op,
                             const char *fact, const char *agent_home)
{
    cJSON *action_j     = cJSON_GetObjectItem(op, "action");
    cJSON *text_j       = cJSON_GetObjectItem(op, "text");
    cJSON *memory_id_j  = cJSON_GetObjectItem(op, "memory_id");
    cJSON *confidence_j = cJSON_GetObjectItem(op, "confidence");
    cJSON *reasoning_j  = cJSON_GetObjectItem(op, "reasoning");

    if (!cJSON_IsString(action_j)) {
        return;
    }

    const char *action    = action_j->valuestring;
    const char *text      = cJSON_IsString(text_j) ? text_j->valuestring : fact;
    const char *memory_id = cJSON_IsString(memory_id_j)
                            ? memory_id_j->valuestring : NULL;
    double confidence     = cJSON_IsNumber(confidence_j)
                            ? confidence_j->valuedouble : 0.0;
    const char *reasoning = cJSON_IsString(reasoning_j)
                            ? reasoning_j->valuestring : "";

    if (confidence >= CONFIDENCE_THRESHOLD && mc->http &&
        mc->search_url[0] != '\0') {
        /* Apply immediately */
        char url[RELAY_MAX_URL];
        char body[MAX_CURATOR_FACT_LEN * 2 + 256];
        char resp_buf[512];

        if (strcmp(action, "ADD") == 0 || strcmp(action, "UPDATE") == 0) {
            /* Clamp importance to [0.75, 1.0] */
            double importance = confidence < 0.75 ? 0.75 :
                                confidence > 1.0  ? 1.0 : confidence;

            /* Generate a stable doc_id from the text */
            char doc_id[48];
            if (memory_id && memory_id[0] != '\0') {
                snprintf(doc_id, sizeof(doc_id), "%s", memory_id);
            } else {
                snprintf(doc_id, sizeof(doc_id),
                         "curator-%08x", fnv1a_32(text));
            }

            snprintf(url, sizeof(url), "%s/upsert", mc->search_url);
            snprintf(body, sizeof(body),
                     "{\"doc_id\":\"%s\",\"text\":\"%s\","
                     "\"metadata\":{\"source\":\"curator\","
                     "\"importance\":%.2f}}",
                     doc_id, text, importance);
            mc->http->post(url, body, resp_buf, sizeof(resp_buf));

        } else if (strcmp(action, "DELETE") == 0 && memory_id &&
                   memory_id[0] != '\0') {
            snprintf(url, sizeof(url), "%s/delete", mc->search_url);
            snprintf(body, sizeof(body),
                     "{\"doc_id\":\"%s\"}", memory_id);
            mc->http->post(url, body, resp_buf, sizeof(resp_buf));
        }

    } else if (confidence < CONFIDENCE_THRESHOLD && mc->fs &&
               agent_home && agent_home[0] != '\0') {
        /* Queue for human review */
        char queue_path[RELAY_MAX_PATH];
        char line[MAX_CURATOR_FACT_LEN + 256];

        snprintf(queue_path, sizeof(queue_path),
                 "%s/data/memory/review_queue.jsonl", agent_home);

        /* Escape quotes in text and reasoning (minimal escaping) */
        snprintf(line, sizeof(line),
                 "{\"timestamp\":%ld,\"fact\":\"%s\","
                 "\"operation\":{\"action\":\"%s\",\"text\":\"%s\","
                 "\"confidence\":%.2f,\"reasoning\":\"%s\"}}\n",
                 (long)time(NULL), fact, action, text,
                 confidence, reasoning);

        mc->fs->append_file(queue_path, line);
    }
}

/* Run Stage 2 for a single fact.
 * Searches sidecar, calls LLM for decision, applies or queues each op. */
static void stage2_process_fact(memory_curator_t *mc,
                                 const char *fact,
                                 const char *agent_home)
{
    /* Search existing memories */
    char search_results[RELAY_MAX_MSG];
    search_results[0] = '\0';
    sidecar_search(mc, fact, search_results, sizeof(search_results));

    /* Build decision prompt */
    char prompt[8192];
    snprintf(prompt, sizeof(prompt),
        "You are a memory manager for an AI agent.\n"
        "Given a candidate fact and existing memories, decide what to do.\n\n"
        "Candidate fact: \"%s\"\n\n"
        "Existing similar memories:\n%s\n\n"
        "Respond with a JSON array of memory operations. Each operation has:\n"
        "- \"action\": \"ADD\", \"UPDATE\", \"DELETE\", or \"NONE\"\n"
        "- \"text\": the memory text (for ADD/UPDATE)\n"
        "- \"memory_id\": existing doc_id to update/delete (for UPDATE/DELETE)\n"
        "- \"confidence\": float 0.0-1.0\n"
        "- \"reasoning\": brief explanation\n\n"
        "Example: [{\"action\":\"ADD\",\"text\":\"The user drinks coffee\","
        "\"confidence\":0.9,\"reasoning\":\"New fact not in memory\"}]",
        fact,
        search_results[0] != '\0' ? search_results : "None");

    llm_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = llm_provider_send(mc->llm, prompt, NULL, &resp);
    if (rc != RELAY_OK || resp.is_error || resp.result[0] == '\0') {
        return; /* fail-open */
    }

    /* Parse operations array */
    cJSON *root = cJSON_Parse(resp.result);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *op = NULL;
    cJSON_ArrayForEach(op, root) {
        if (cJSON_IsObject(op)) {
            apply_operation(mc, op, fact, agent_home);
        }
    }
    cJSON_Delete(root);
}

/* ── Public API ──────────────────────────────────────────────────────── */

memory_curator_t *memory_curator_create(llm_provider_t *llm,
                                         relay_http_t   *http,
                                         relay_fs_t     *fs,
                                         const config_t *cfg)
{
    if (!llm || !cfg) {
        return NULL;
    }

    memory_curator_t *mc = calloc(1, sizeof(*mc));
    if (!mc) {
        return NULL;
    }

    mc->llm  = llm;
    mc->http = http;
    mc->fs   = fs;

    snprintf(mc->search_url, sizeof(mc->search_url), "%s",
             config_get(cfg, "memory_service_url", "http://localhost:8765"));

    mc->flush_every_n = config_get_int(cfg, "memory_flush_every_n_turns", 5);
    if (mc->flush_every_n <= 0) {
        mc->flush_every_n = 5;
    }

    return mc;
}

void memory_curator_on_turn(memory_curator_t *mc,
                             const char *user_msg,
                             const char *agent_msg,
                             const char *agent_home)
{
    if (!mc || !user_msg || !agent_msg) {
        return;
    }

    /* Store in ring buffer (overwrite oldest when full) */
    int slot = mc->stored_turns < MAX_CURATOR_TURNS
               ? mc->stored_turns
               : MAX_CURATOR_TURNS - 1;

    if (mc->stored_turns >= MAX_CURATOR_TURNS) {
        /* Shift entries left to make room for the newest */
        memmove(&mc->user_msgs[0], &mc->user_msgs[1],
                (MAX_CURATOR_TURNS - 1) * MAX_CURATOR_TURN_LEN);
        memmove(&mc->agent_msgs[0], &mc->agent_msgs[1],
                (MAX_CURATOR_TURNS - 1) * MAX_CURATOR_TURN_LEN);
    } else {
        mc->stored_turns++;
    }

    snprintf(mc->user_msgs[slot], MAX_CURATOR_TURN_LEN, "%s", user_msg);
    snprintf(mc->agent_msgs[slot], MAX_CURATOR_TURN_LEN, "%s", agent_msg);
    mc->turn_count++;

    /* Auto-flush when count reaches N */
    if (mc->turn_count % mc->flush_every_n == 0) {
        memory_curator_flush(mc, agent_home);
    }
}

int memory_curator_flush(memory_curator_t *mc, const char *agent_home)
{
    if (!mc || mc->stored_turns == 0) {
        return RELAY_OK;
    }

    /* Stage 1: extract facts */
    char facts[MAX_CURATOR_FACTS][MAX_CURATOR_FACT_LEN];
    int nfacts = stage1_extract_facts(mc, facts);

    /* Stage 2: process each fact */
    for (int i = 0; i < nfacts; i++) {
        stage2_process_fact(mc, facts[i], agent_home);
    }

    /* Reset ring buffer after flush */
    mc->stored_turns = 0;

    return RELAY_OK;
}

void memory_curator_free(memory_curator_t *mc)
{
    if (!mc) {
        return;
    }
    free(mc);
}
