#ifndef RELAY_VISION_H
#define RELAY_VISION_H

#include "relay.h"
#include "config.h"

/* ── Vision ─────────────────────────────────────────────────────────── */
/* Local image-to-text using Ollama. Converts a photo to a rich text    */
/* description before passing it to the main LLM provider.              */

typedef struct vision vision_t;

/* Create vision engine. http and cfg must be non-NULL.
 * Vision is considered enabled only if vision_model is set in config.
 * Returns NULL on allocation failure. */
vision_t *vision_create(relay_http_t *http, const config_t *cfg);

/* Describe the image at image_path by calling the Ollama API.
 * Reads the file, base64-encodes it, POSTs to Ollama, and returns the
 * plain-text description in out (NUL-terminated).
 * Returns RELAY_OK on success, RELAY_ERR_IO if file can't be read,
 * RELAY_ERR on HTTP failure, RELAY_ERR_PARSE if response is malformed. */
int vision_describe(vision_t *v, const char *image_path,
                    char *out, size_t max);

/* Parse an Ollama /api/generate JSON response.
 * Extracts the "response" field into out.
 * Returns RELAY_OK on success, RELAY_ERR if an error field is present,
 * RELAY_ERR_PARSE on invalid JSON or missing response field. */
int vision_parse_response(const char *json, char *out, size_t max);

/* Returns 1 if vision is enabled (vision_model configured), 0 if not. */
int vision_is_enabled(vision_t *v);

/* Free vision engine. */
void vision_free(vision_t *v);

#endif /* RELAY_VISION_H */
