#include "telegram_offset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OFFSET_FILE  "data/state/telegram-offset.txt"
#define OFFSET_TMP   "data/state/telegram-offset.txt.tmp"

static void offset_path(const char *workspace, const char *suffix,
                        char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", workspace, suffix);
}

int telegram_offset_save(const char *workspace, long long offset)
{
    if (!workspace) {
        return -1;
    }

    char tmp_path[512];
    char final_path[512];
    offset_path(workspace, OFFSET_TMP,  tmp_path,   sizeof(tmp_path));
    offset_path(workspace, OFFSET_FILE, final_path, sizeof(final_path));

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        return -1;
    }

    fprintf(f, "%lld\n", offset);
    fclose(f);

    if (rename(tmp_path, final_path) != 0) {
        remove(tmp_path);
        return -1;
    }

    return 0;
}

long long telegram_offset_load(const char *workspace)
{
    if (!workspace) {
        return 0;
    }

    char path[512];
    offset_path(workspace, OFFSET_FILE, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    long long offset = 0;
    fscanf(f, "%lld", &offset);
    fclose(f);

    return (offset > 0) ? offset : 0;
}
