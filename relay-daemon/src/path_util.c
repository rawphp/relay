#include "path_util.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
