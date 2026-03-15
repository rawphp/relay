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

/* Encode an absolute path for Claude Code's ~/.claude/projects/ directory naming.
 * Replaces '/' with '-', e.g. "/Users/tom/project" → "-Users-tom-project".
 * Trailing slashes are stripped before encoding. */
void path_util_encode_claude_dir(const char *path, char *out, size_t max);

/* Decode a Claude Code project directory name back to an absolute path.
 * Replaces '-' with '/', e.g. "-Users-tom-project" → "/Users/tom/project". */
void path_util_decode_claude_dir(const char *encoded, char *out, size_t max);

/* Resolve the relay install directory from the config file path.
 * Strips the trailing "/config/relay.conf" to get the install root.
 * e.g., "/home/kai/config/relay.conf" → "/home/kai"
 * Returns RELAY_OK on success, RELAY_ERR if config_path is NULL or
 * does not end with "/config/relay.conf". */
int path_util_install_dir(const char *config_path, char *out, size_t max);

#endif /* RELAY_PATH_UTIL_H */
