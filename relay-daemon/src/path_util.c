#include "path_util.h"
#include "relay.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void path_util_encode_claude_dir(const char *path, char *out, size_t max)
{
    if (!out || max == 0) return;
    if (!path || path[0] == '\0') {
        out[0] = '\0';
        return;
    }

    /* Strip trailing slashes from a working copy */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    /* Replace '/' with '-' */
    size_t j = 0;
    for (size_t i = 0; i < len && j < max - 1; i++) {
        out[j++] = (path[i] == '/') ? '-' : path[i];
    }
    out[j] = '\0';
}

void path_util_decode_claude_dir(const char *encoded, char *out, size_t max)
{
    if (!out || max == 0) return;
    if (!encoded || encoded[0] == '\0') {
        out[0] = '\0';
        return;
    }

    /* Replace '-' with '/' */
    size_t len = strlen(encoded);
    size_t j = 0;
    for (size_t i = 0; i < len && j < max - 1; i++) {
        out[j++] = (encoded[i] == '-') ? '/' : encoded[i];
    }
    out[j] = '\0';
}

int path_util_install_dir(const char *config_path, char *out, size_t max)
{
    if (!config_path || !out || max == 0) {
        return RELAY_ERR;
    }

    static const char suffix[] = "/config/relay.conf";
    size_t cp_len = strlen(config_path);
    size_t sf_len = strlen(suffix);

    if (cp_len < sf_len ||
        strcmp(config_path + cp_len - sf_len, suffix) != 0) {
        return RELAY_ERR;
    }

    size_t home_len = cp_len - sf_len;
    if (home_len == 0) {
        snprintf(out, max, "/");
    } else {
        snprintf(out, max, "%.*s", (int)home_len, config_path);
    }
    return RELAY_OK;
}

void resolve_default_config_path(const char *argv0, char *out, size_t max)
{
    static const char *fallback = "config/relay.conf";

    if (!argv0 || !out || max == 0) {
        if (out && max > 0) {
            snprintf(out, max, "%s", fallback);
        }
        return;
    }

    /* Resolve argv0 to an absolute path */
    char resolved[4096];
    if (!realpath(argv0, resolved)) {
        snprintf(out, max, "%s", fallback);
        return;
    }

    /* dirname() may modify its argument — work on a copy */
    char resolved_copy[4096];
    snprintf(resolved_copy, sizeof(resolved_copy), "%s", resolved);
    char *dir = dirname(resolved_copy);

    /* Construct <binary_dir>/../config/relay.conf */
    char candidate[4096];
    snprintf(candidate, sizeof(candidate), "%s/../config/relay.conf", dir);

    /* Normalise with realpath — resolves the .. */
    char normalised[4096];
    if (!realpath(candidate, normalised)) {
        snprintf(out, max, "%s", fallback);
        return;
    }

    /* Use binary-relative path only if it actually exists */
    if (access(normalised, F_OK) == 0) {
        snprintf(out, max, "%s", normalised);
    } else {
        snprintf(out, max, "%s", fallback);
    }
}
