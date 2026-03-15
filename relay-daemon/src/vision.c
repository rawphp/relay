#include "vision.h"

#include <cJSON/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vision {
    relay_http_t *http;
    char model[RELAY_MAX_VALUE];       /* e.g. "moondream" or "llava" */
    char ollama_url[RELAY_MAX_URL];    /* e.g. "http://localhost:11434" */
    int enabled;                      /* 1 if vision_model is configured */
};

/* ── Base64 encoder ─────────────────────────────────────────────────── */

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode in_len bytes from in into out (NUL-terminated).
 * out_max must be at least ceil(in_len/3)*4 + 1 bytes.
 * Returns number of base64 characters written (excluding NUL). */
static size_t b64_encode(const unsigned char *in, size_t in_len,
                         char *out, size_t out_max)
{
    size_t out_pos = 0;

    for (size_t i = 0; i < in_len; i += 3) {
        if (out_pos + 4 >= out_max) {
            break;
        }

        unsigned int grp = (unsigned int)in[i] << 16;
        if (i + 1 < in_len) {
            grp |= (unsigned int)in[i + 1] << 8;
        }
        if (i + 2 < in_len) {
            grp |= (unsigned int)in[i + 2];
        }

        out[out_pos++] = b64_alphabet[(grp >> 18) & 0x3F];
        out[out_pos++] = b64_alphabet[(grp >> 12) & 0x3F];
        out[out_pos++] = (i + 1 < in_len)
                         ? b64_alphabet[(grp >> 6) & 0x3F] : '=';
        out[out_pos++] = (i + 2 < in_len)
                         ? b64_alphabet[grp & 0x3F] : '=';
    }

    out[out_pos] = '\0';
    return out_pos;
}

/* ── Public API ─────────────────────────────────────────────────────── */

vision_t *vision_create(relay_http_t *http, const config_t *cfg)
{
    if (!http || !cfg) {
        return NULL;
    }

    vision_t *v = calloc(1, sizeof(vision_t));
    if (!v) {
        return NULL;
    }

    v->http = http;

    snprintf(v->model, sizeof(v->model), "%s",
             config_get(cfg, "vision_model", "moondream"));
    snprintf(v->ollama_url, sizeof(v->ollama_url), "%s",
             config_get(cfg, "vision_ollama_url", "http://localhost:11434"));

    /* Only enabled when vision_model is explicitly set in config */
    const char *model_cfg = config_get(cfg, "vision_model", NULL);
    v->enabled = (model_cfg != NULL && model_cfg[0] != '\0') ? 1 : 0;

    return v;
}

int vision_parse_response(const char *json, char *out, size_t max)
{
    if (!json || !out) {
        return RELAY_ERR_PARSE;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return RELAY_ERR_PARSE;
    }

    /* Ollama returns {"error":"..."} when the model isn't found etc. */
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (err && cJSON_IsString(err)) {
        cJSON_Delete(root);
        return RELAY_ERR;
    }

    cJSON *resp = cJSON_GetObjectItem(root, "response");
    if (!cJSON_IsString(resp) || resp->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return RELAY_ERR_PARSE;
    }

    snprintf(out, max, "%s", resp->valuestring);
    cJSON_Delete(root);
    return RELAY_OK;
}

int vision_describe(vision_t *v, const char *image_path,
                    char *out, size_t max)
{
    if (!v || !image_path || !out || max == 0) {
        return RELAY_ERR;
    }

    /* ── 1. Read image file ──────────────────────────────────────── */
    FILE *fp = fopen(image_path, "rb");
    if (!fp) {
        return RELAY_ERR_IO;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 20L * 1024 * 1024) { /* 20 MB cap */
        fclose(fp);
        return RELAY_ERR;
    }

    unsigned char *img_data = malloc((size_t)file_size);
    if (!img_data) {
        fclose(fp);
        return RELAY_ERR_NOMEM;
    }

    size_t read_bytes = fread(img_data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_bytes != (size_t)file_size) {
        free(img_data);
        return RELAY_ERR_IO;
    }

    /* ── 2. Base64 encode ────────────────────────────────────────── */
    /* Output buffer: ceil(n/3)*4 + 1 */
    size_t b64_max = (((size_t)file_size + 2) / 3) * 4 + 1;
    char *b64 = malloc(b64_max);
    if (!b64) {
        free(img_data);
        return RELAY_ERR_NOMEM;
    }

    b64_encode(img_data, (size_t)file_size, b64, b64_max);
    free(img_data);

    /* ── 3. Build JSON payload ────────────────────────────────────── */
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        free(b64);
        return RELAY_ERR_NOMEM;
    }

    cJSON_AddStringToObject(payload, "model", v->model);
    cJSON_AddStringToObject(payload, "prompt",
        "Describe this image in detail. Include: what you see, "
        "any visible text, objects, people, colours, setting, and context. "
        "Be concise but thorough.");

    cJSON *images_arr = cJSON_CreateArray();
    if (!images_arr) {
        cJSON_Delete(payload);
        free(b64);
        return RELAY_ERR_NOMEM;
    }
    cJSON_AddItemToArray(images_arr, cJSON_CreateString(b64));
    cJSON_AddItemToObject(payload, "images", images_arr);
    cJSON_AddItemToObject(payload, "stream", cJSON_CreateFalse());

    free(b64);

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!body) {
        return RELAY_ERR_NOMEM;
    }

    /* ── 4. POST to Ollama API ───────────────────────────────────── */
    char url[RELAY_MAX_URL];
    snprintf(url, sizeof(url), "%s/api/generate", v->ollama_url);

    char response_buf[RELAY_MAX_RESPONSE];
    int rc = v->http->post(url, body, response_buf, sizeof(response_buf));
    free(body);

    if (rc != RELAY_OK) {
        return rc;
    }

    /* ── 5. Parse and return description ─────────────────────────── */
    return vision_parse_response(response_buf, out, max);
}

int vision_is_enabled(vision_t *v)
{
    return v ? v->enabled : 0;
}

void vision_free(vision_t *v)
{
    free(v);
}
