#ifndef RELAY_VOICE_PIPELINE_H
#define RELAY_VOICE_PIPELINE_H

#include "relay.h"
#include <stddef.h>

/* Maximum number of args for voice pipeline (bin + flags + NULL terminator) */
#define VOICE_PIPELINE_MAX_ARGS 16

/* Build the argument array for voice_daemon_bridge.py.
 * home: value of $HOME (must not be NULL).
 * chat_id: Telegram chat ID (must not be NULL).
 * audio_path: path to downloaded audio file (must not be NULL).
 * argv: caller-supplied array of at least VOICE_PIPELINE_MAX_ARGS pointers.
 * bin_buf: caller-supplied buffer for the script path.
 * bin_buf_size: size of bin_buf.
 * cfg_buf: caller-supplied buffer for the config path.
 * cfg_buf_size: size of cfg_buf.
 * Returns RELAY_OK on success, RELAY_ERR_INVALID if any required arg is NULL. */
int voice_pipeline_build_args(const char *home,
                               const char *chat_id,
                               const char *audio_path,
                               const char **argv, size_t argv_max,
                               char *bin_buf, size_t bin_buf_size,
                               char *cfg_buf, size_t cfg_buf_size);

/* Run the voice pipeline via proc->spawn and return the JSON output.
 * home: value of $HOME (NULL-checked internally).
 * chat_id: Telegram chat ID.
 * audio_path: path to downloaded audio file.
 * proc: process spawner (dependency injection).
 * output: buffer for JSON output from the pipeline.
 * output_max: size of output buffer.
 * Returns RELAY_OK on success, error code on failure. */
int voice_pipeline_run(const char *home,
                        const char *chat_id,
                        const char *audio_path,
                        const relay_proc_t *proc,
                        char *output, size_t output_max);

#endif /* RELAY_VOICE_PIPELINE_H */
