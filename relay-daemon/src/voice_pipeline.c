#include "voice_pipeline.h"
#include <stdio.h>
#include <string.h>

int voice_pipeline_build_args(const char *home,
                               const char *chat_id,
                               const char *audio_path,
                               const char **argv, size_t argv_max,
                               char *bin_buf, size_t bin_buf_size,
                               char *cfg_buf, size_t cfg_buf_size)
{
    if (!home || !chat_id || !audio_path) {
        return RELAY_ERR_INVALID;
    }
    if (argv_max < 7) {
        return RELAY_ERR_INVALID;
    }

    snprintf(bin_buf, bin_buf_size,
             "%s/relay/lib/voice/voice_daemon_bridge.py", home);
    snprintf(cfg_buf, cfg_buf_size,
             "%s/relay/config/voice.json", home);

    argv[0] = bin_buf;
    argv[1] = "--config";
    argv[2] = cfg_buf;
    argv[3] = "process-audio-telegram";
    argv[4] = chat_id;
    argv[5] = audio_path;
    argv[6] = NULL;

    return RELAY_OK;
}

int voice_pipeline_run(const char *home,
                        const char *chat_id,
                        const char *audio_path,
                        const relay_proc_t *proc,
                        char *output, size_t output_max)
{
    const char *argv[VOICE_PIPELINE_MAX_ARGS];
    char bin_buf[RELAY_MAX_PATH];
    char cfg_buf[RELAY_MAX_PATH];

    int rc = voice_pipeline_build_args(home, chat_id, audio_path,
                                        argv, VOICE_PIPELINE_MAX_ARGS,
                                        bin_buf, sizeof(bin_buf),
                                        cfg_buf, sizeof(cfg_buf));
    if (rc != RELAY_OK) {
        return rc;
    }

    return proc->spawn(argv[0], argv, NULL, output, output_max, 60);
}
