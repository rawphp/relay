#ifndef RELAY_PATH_UTIL_H
#define RELAY_PATH_UTIL_H

#include <stddef.h>

/* resolve_default_config_path — find config relative to the binary location.
 *
 * When no -c flag is supplied, the relay binary defaults to the CWD-relative
 * path "config/relay.conf".  This breaks when the user runs the binary from
 * any directory other than RELAY_HOME.
 *
 * This helper resolves argv0 to an absolute path via realpath(), then looks
 * for <binary_dir>/../config/relay.conf.  If that path exists it is written
 * into out[].  Otherwise out[] receives the literal fallback "config/relay.conf"
 * so existing behaviour is preserved.
 *
 * Parameters:
 *   argv0  — argv[0] from main(); may be relative or absolute
 *   out    — destination buffer
 *   max    — size of out buffer
 */
void resolve_default_config_path(const char *argv0, char *out, size_t max);

#endif /* RELAY_PATH_UTIL_H */
