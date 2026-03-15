#include "session.h"

#include <cJSON/cJSON.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ─────────────────────────────────────────────────── */

#define MAX_SESSIONS 64
#define MAX_ACTIVE   64

typedef struct {
    char chat_id[RELAY_MAX_USER_ID];
    char workspace_name[64];
} active_slot_t;

struct session_store {
    session_entry_t entries[MAX_SESSIONS];
    int count;
    active_slot_t active[MAX_ACTIVE];
    int active_count;
    char persist_path[RELAY_MAX_PATH];
    int expiry_hours;
    relay_fs_t *fs;
    relay_clock_t *clock;
    int dirty;  /* Needs persist() on next flush */
};

/* ── Helpers ────────────────────────────────────────────────────────── */

static session_entry_t *find_entry(session_store_t *store, const char *chat_id)
{
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].chat_id, chat_id) == 0) {
            return &store->entries[i];
        }
    }
    return NULL;
}

static int persist(session_store_t *store)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return RELAY_ERR_NOMEM;
    }

    cJSON *sessions = cJSON_CreateArray();
    if (!sessions) {
        cJSON_Delete(root);
        return RELAY_ERR_NOMEM;
    }
    cJSON_AddItemToObject(root, "sessions", sessions);

    for (int i = 0; i < store->count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "chat_id",
                                store->entries[i].chat_id);
        cJSON_AddStringToObject(entry, "session_id",
                                store->entries[i].session_id);
        cJSON_AddStringToObject(entry, "workspace_name",
                                store->entries[i].workspace_name);
        cJSON_AddStringToObject(entry, "workspace_path",
                                store->entries[i].workspace_path);
        cJSON_AddStringToObject(entry, "provider",
                                store->entries[i].provider);
        cJSON_AddNumberToObject(entry, "last_used",
                                (double)store->entries[i].last_used);
        cJSON_AddNumberToObject(entry, "context_tokens",
                                (double)store->entries[i].context_tokens);
        cJSON_AddNumberToObject(entry, "compaction_count",
                                (double)store->entries[i].compaction_count);
        cJSON_AddNumberToObject(entry, "memory_flush_at",
                                (double)store->entries[i].memory_flush_at);
        cJSON_AddNumberToObject(entry, "memory_flush_compaction_count",
                                (double)store->entries[i].memory_flush_compaction_count);
        cJSON_AddItemToArray(sessions, entry);
    }

    /* Persist active workspace selections */
    cJSON *active_arr = cJSON_CreateArray();
    if (active_arr) {
        for (int i = 0; i < store->active_count; i++) {
            cJSON *slot = cJSON_CreateObject();
            cJSON_AddStringToObject(slot, "chat_id",
                                    store->active[i].chat_id);
            cJSON_AddStringToObject(slot, "workspace_name",
                                    store->active[i].workspace_name);
            cJSON_AddItemToArray(active_arr, slot);
        }
        cJSON_AddItemToObject(root, "active", active_arr);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) {
        return RELAY_ERR_NOMEM;
    }

    int rc = store->fs->write_file(store->persist_path, json);
    free(json);
    return rc;
}

/* Flush dirty sessions to disk. Returns RELAY_OK if clean or successfully
 * persisted, RELAY_ERR on write failure. */
static int flush_if_dirty(session_store_t *store)
{
    if (!store || !store->dirty) {
        return RELAY_OK;
    }
    int rc = persist(store);
    if (rc == RELAY_OK) {
        store->dirty = 0;
    }
    return rc;
}

static void load_from_file(session_store_t *store)
{
    char *json = store->fs->read_file(store->persist_path);
    if (!json) {
        return;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return;
    }

    cJSON *sessions = cJSON_GetObjectItem(root, "sessions");
    if (!cJSON_IsArray(sessions)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *entry;
    cJSON_ArrayForEach(entry, sessions) {
        if (store->count >= MAX_SESSIONS) {
            break;
        }

        cJSON *cid = cJSON_GetObjectItem(entry, "chat_id");
        cJSON *sid = cJSON_GetObjectItem(entry, "session_id");
        cJSON *lu  = cJSON_GetObjectItem(entry, "last_used");
        cJSON *ct  = cJSON_GetObjectItem(entry, "context_tokens");
        cJSON *cc  = cJSON_GetObjectItem(entry, "compaction_count");
        cJSON *mfa = cJSON_GetObjectItem(entry, "memory_flush_at");
        cJSON *mfcc = cJSON_GetObjectItem(entry, "memory_flush_compaction_count");

        if (cJSON_IsString(cid) && cJSON_IsString(sid)) {
            snprintf(store->entries[store->count].chat_id,
                     RELAY_MAX_USER_ID, "%s", cid->valuestring);
            snprintf(store->entries[store->count].session_id,
                     RELAY_MAX_SESSION_ID, "%s", sid->valuestring);
            /* Workspace fields — optional, default to empty string */
            cJSON *wn  = cJSON_GetObjectItem(entry, "workspace_name");
            cJSON *wp  = cJSON_GetObjectItem(entry, "workspace_path");
            cJSON *prov = cJSON_GetObjectItem(entry, "provider");
            snprintf(store->entries[store->count].workspace_name,
                     sizeof(store->entries[store->count].workspace_name),
                     "%s", cJSON_IsString(wn) ? wn->valuestring : "");
            snprintf(store->entries[store->count].workspace_path,
                     sizeof(store->entries[store->count].workspace_path),
                     "%s", cJSON_IsString(wp) ? wp->valuestring : "");
            snprintf(store->entries[store->count].provider,
                     sizeof(store->entries[store->count].provider),
                     "%s", cJSON_IsString(prov) ? prov->valuestring : "");
            store->entries[store->count].last_used =
                cJSON_IsNumber(lu) ? (time_t)lu->valuedouble : 0;
            store->entries[store->count].context_tokens =
                cJSON_IsNumber(ct) ? (size_t)ct->valuedouble : 0;
            store->entries[store->count].compaction_count =
                cJSON_IsNumber(cc) ? (size_t)cc->valuedouble : 0;
            store->entries[store->count].memory_flush_at =
                cJSON_IsNumber(mfa) ? (time_t)mfa->valuedouble : 0;
            store->entries[store->count].memory_flush_compaction_count =
                cJSON_IsNumber(mfcc) ? (size_t)mfcc->valuedouble : 0;
            store->count++;
        }
    }

    /* Restore active workspace selections */
    cJSON *active_arr = cJSON_GetObjectItem(root, "active");
    if (cJSON_IsArray(active_arr)) {
        cJSON *slot;
        cJSON_ArrayForEach(slot, active_arr) {
            if (store->active_count >= MAX_ACTIVE) break;
            cJSON *cid = cJSON_GetObjectItem(slot, "chat_id");
            cJSON *wn  = cJSON_GetObjectItem(slot, "workspace_name");
            if (cJSON_IsString(cid) && cJSON_IsString(wn)) {
                snprintf(store->active[store->active_count].chat_id,
                         RELAY_MAX_USER_ID, "%s", cid->valuestring);
                snprintf(store->active[store->active_count].workspace_name,
                         sizeof(store->active[0].workspace_name),
                         "%s", wn->valuestring);
                store->active_count++;
            }
        }
    }

    cJSON_Delete(root);
}

/* ── Public API ─────────────────────────────────────────────────────── */

session_store_t *session_create(relay_fs_t *fs, relay_clock_t *clock,
                                 const char *persist_path,
                                 int expiry_hours)
{
    if (!fs || !clock || !persist_path) {
        return NULL;
    }

    session_store_t *store = calloc(1, sizeof(session_store_t));
    if (!store) {
        return NULL;
    }

    store->fs = fs;
    store->clock = clock;
    store->expiry_hours = expiry_hours;
    snprintf(store->persist_path, RELAY_MAX_PATH, "%s", persist_path);

    /* Load existing sessions if file exists */
    if (fs->file_exists(persist_path)) {
        load_from_file(store);
    }

    return store;
}

const char *session_get(session_store_t *store, const char *chat_id)
{
    if (!store || !chat_id) {
        return NULL;
    }

    session_entry_t *entry = find_entry(store, chat_id);
    return entry ? entry->session_id : NULL;
}

int session_set(session_store_t *store, const char *chat_id,
                const char *session_id)
{
    if (!store || !chat_id || !session_id) {
        return RELAY_ERR;
    }

    session_entry_t *entry = find_entry(store, chat_id);
    if (entry) {
        /* Update existing */
        snprintf(entry->session_id, RELAY_MAX_SESSION_ID, "%s", session_id);
        entry->last_used = store->clock->now();
    } else {
        /* Add new */
        if (store->count >= MAX_SESSIONS) {
            return RELAY_ERR_NOMEM;
        }
        snprintf(store->entries[store->count].chat_id,
                 RELAY_MAX_USER_ID, "%s", chat_id);
        snprintf(store->entries[store->count].session_id,
                 RELAY_MAX_SESSION_ID, "%s", session_id);
        store->entries[store->count].last_used = store->clock->now();
        store->count++;
    }

    /* Mark dirty for later flush instead of blocking on I/O */
    store->dirty = 1;
    return RELAY_OK;
}

int session_clear(session_store_t *store, const char *chat_id)
{
    if (!store || !chat_id) {
        return RELAY_ERR;
    }

    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].chat_id, chat_id) == 0) {
            /* Move last entry into this slot */
            if (i < store->count - 1) {
                store->entries[i] = store->entries[store->count - 1];
            }
            store->count--;
            store->dirty = 1;
            return RELAY_OK;
        }
    }

    return RELAY_ERR_NOTFOUND;
}

int session_expire(session_store_t *store)
{
    if (!store) {
        return RELAY_ERR;
    }

    time_t now = store->clock->now();
    time_t cutoff = now - (time_t)(store->expiry_hours * 3600);
    int removed = 0;

    for (int i = store->count - 1; i >= 0; i--) {
        if (store->entries[i].last_used < cutoff) {
            if (i < store->count - 1) {
                store->entries[i] = store->entries[store->count - 1];
            }
            store->count--;
            removed++;
        }
    }

    if (removed > 0) {
        store->dirty = 1;
    }

    return removed;
}

time_t session_last_used(session_store_t *store, const char *chat_id)
{
    if (!store || !chat_id) {
        return 0;
    }

    session_entry_t *entry = find_entry(store, chat_id);
    return entry ? entry->last_used : 0;
}

size_t session_estimate_tokens(const char *text)
{
    if (!text) {
        return 0;
    }
    /* Heuristic: (word_count × 1.3) + punct_count
     *
     * word_count × 1.3 covers BPE subword splits for English prose (±5–8%).
     * Adding punct_count captures punctuation tokens that BPE emits separately
     * from the surrounding word (commas, periods, exclamations, etc.).
     * Together these improve accuracy for punctuation-heavy prose by ~3–5%.
     *
     * Safety multiplier: if strlen > 2000, multiply by 1.2 to absorb the
     * systematic undercount on code snippets and long/dense messages.
     *
     * NOTE: code content is still underestimated (~20–35%). Callers should
     * use a soft threshold of ~85% of the model limit as additional headroom. */
    size_t words = 0, puncts = 0;
    int in_word = 0;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c)) {
            in_word = 0;
        } else if (ispunct(c)) {
            puncts++;
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    size_t estimate = (words * 13) / 10 + puncts;
    if (strlen(text) > 2000) {
        estimate = estimate * 12 / 10;  /* +20% safety margin */
    }
    return estimate;
}

int session_add_tokens(session_store_t *store, const char *chat_id,
                       size_t tokens)
{
    if (!store || !chat_id) {
        return RELAY_ERR;
    }

    session_entry_t *entry = find_entry(store, chat_id);
    if (!entry) {
        return RELAY_ERR_NOTFOUND;
    }

    entry->context_tokens += tokens;

    /* Mark dirty for later flush instead of blocking on I/O */
    store->dirty = 1;
    return RELAY_OK;
}

size_t session_get_context_tokens(session_store_t *store, const char *chat_id)
{
    if (!store || !chat_id) {
        return 0;
    }

    session_entry_t *entry = find_entry(store, chat_id);
    return entry ? entry->context_tokens : 0;
}

int session_needs_memory_flush(session_store_t *store, const char *chat_id,
                                size_t soft_threshold)
{
    session_entry_t *e = find_entry(store, chat_id);
    if (!e) {
        return 0;  /* No session */
    }

    /* Check if context exceeds soft threshold */
    if (e->context_tokens < soft_threshold) {
        return 0;  /* Below threshold */
    }

    /* Check if already flushed for this compaction cycle
     * If memory_flush_at is 0, we've never flushed, so we need to flush.
     * Otherwise, check if we've already flushed for the current cycle. */
    if (e->memory_flush_at > 0 && e->memory_flush_compaction_count == e->compaction_count) {
        return 0;  /* Already flushed */
    }

    return 1;  /* Needs flush */
}

int session_mark_memory_flushed(session_store_t *store, const char *chat_id)
{
    session_entry_t *e = find_entry(store, chat_id);
    if (!e) {
        return RELAY_ERR;
    }

    e->memory_flush_at = store->clock->now();
    e->memory_flush_compaction_count = e->compaction_count;
    store->dirty = 1;
    return RELAY_OK;
}

int session_flush(session_store_t *store)
{
    return flush_if_dirty(store);
}

void session_free(session_store_t *store)
{
    /* Flush before freeing to ensure no data loss */
    if (store) {
        flush_if_dirty(store);
    }
    free(store);
}

/* ── Workspace API ──────────────────────────────────────────────────── */

int session_set_workspace(session_store_t *store, const char *chat_id,
                          const char *workspace_name,
                          const char *workspace_path,
                          const char *provider)
{
    if (!store || !chat_id) {
        return RELAY_ERR;
    }
    session_entry_t *e = find_entry(store, chat_id);
    if (!e) {
        return RELAY_ERR_NOTFOUND;
    }
    snprintf(e->workspace_name, sizeof(e->workspace_name), "%s",
             workspace_name ? workspace_name : "");
    snprintf(e->workspace_path, sizeof(e->workspace_path), "%s",
             workspace_path ? workspace_path : "");
    snprintf(e->provider, sizeof(e->provider), "%s",
             provider ? provider : "");
    store->dirty = 1;
    return RELAY_OK;
}

const char *session_get_workspace_name(session_store_t *store,
                                        const char *chat_id)
{
    if (!store || !chat_id) {
        return "";
    }
    session_entry_t *e = find_entry(store, chat_id);
    return e ? e->workspace_name : "";
}

const char *session_get_workspace_path(session_store_t *store,
                                        const char *chat_id)
{
    if (!store || !chat_id) {
        return "";
    }
    session_entry_t *e = find_entry(store, chat_id);
    return e ? e->workspace_path : "";
}

const char *session_get_workspace_provider(session_store_t *store,
                                            const char *chat_id)
{
    if (!store || !chat_id) {
        return "";
    }
    session_entry_t *e = find_entry(store, chat_id);
    return e ? e->provider : "";
}

/* ── Active workspace tracking ────────────────────────────────────────── */

const char *session_get_active_workspace(session_store_t *store,
                                          const char *chat_id)
{
    if (!store || !chat_id) {
        return "";
    }
    for (int i = 0; i < store->active_count; i++) {
        if (strcmp(store->active[i].chat_id, chat_id) == 0) {
            return store->active[i].workspace_name;
        }
    }
    return "";
}

void session_set_active_workspace(session_store_t *store,
                                   const char *chat_id,
                                   const char *workspace_name)
{
    if (!store || !chat_id) {
        return;
    }
    /* Update existing slot */
    for (int i = 0; i < store->active_count; i++) {
        if (strcmp(store->active[i].chat_id, chat_id) == 0) {
            snprintf(store->active[i].workspace_name,
                     sizeof(store->active[i].workspace_name),
                     "%s", workspace_name ? workspace_name : "");
            store->dirty = 1;
            return;
        }
    }
    /* New slot */
    if (store->active_count < MAX_ACTIVE) {
        snprintf(store->active[store->active_count].chat_id,
                 RELAY_MAX_USER_ID, "%s", chat_id);
        snprintf(store->active[store->active_count].workspace_name,
                 sizeof(store->active[0].workspace_name),
                 "%s", workspace_name ? workspace_name : "");
        store->active_count++;
        store->dirty = 1;
    }
}

const session_entry_t *session_get_for_workspace(session_store_t *store,
                                                  const char *chat_id,
                                                  const char *workspace_name)
{
    if (!store || !chat_id || !workspace_name) {
        return NULL;
    }
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].chat_id, chat_id) == 0 &&
            strcmp(store->entries[i].workspace_name, workspace_name) == 0) {
            return &store->entries[i];
        }
    }
    return NULL;
}

int session_list_for_chat(session_store_t *store, const char *chat_id,
                          const session_entry_t **out, int max)
{
    if (!store || !chat_id || !out || max <= 0) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < store->count && count < max; i++) {
        if (strcmp(store->entries[i].chat_id, chat_id) == 0) {
            out[count++] = &store->entries[i];
        }
    }
    return count;
}

void session_close_workspace(session_store_t *store, const char *chat_id,
                              const char *workspace_name)
{
    if (!store || !chat_id || !workspace_name) {
        return;
    }
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->entries[i].chat_id, chat_id) == 0 &&
            strcmp(store->entries[i].workspace_name, workspace_name) == 0) {
            /* Shift remaining entries down */
            for (int j = i; j < store->count - 1; j++) {
                store->entries[j] = store->entries[j + 1];
            }
            store->count--;
            store->dirty = 1;
            return;
        }
    }
}
const char *session_get_if_workspace_matches(session_store_t *store,
                                              const char *key,
                                              const char *workspace_name)
{
    if (!store || !key || !workspace_name) {
        return NULL;
    }
    session_entry_t *e = find_entry(store, key);
    if (!e || e->session_id[0] == '\0') {
        return NULL;
    }
    /* Only return the session_id if the stored workspace matches the requested one.
     * If it differs, the caller must start a fresh Claude session in the new workspace. */
    if (workspace_name[0] == '\0' || strcmp(e->workspace_name, workspace_name) == 0) {
        return e->session_id;
    }
    return NULL;
}
