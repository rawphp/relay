#include "group_chat_context.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_CONTEXT_AGE_SECONDS 86400  /* 24 hours */

/* Scan {workspace}/data/sessions/ for .txt files, read the most recently
 * modified one that is within 24 hours old.  Returns 1 on success. */
static int load_from_sessions_dir(const char *workspace, char *buf, size_t buf_len)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/data/sessions", workspace);

    DIR *dp = opendir(dir);
    if (!dp) return 0;

    char best[512] = {0};
    time_t best_mtime = 0;
    time_t now = time(NULL);
    struct dirent *de;

    while ((de = readdir(dp)) != NULL) {
        size_t nlen = strlen(de->d_name);
        if (nlen < 5 || strcmp(de->d_name + nlen - 4, ".txt") != 0) continue;

        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(fpath, &st) != 0 || st.st_size == 0) continue;
        if ((now - st.st_mtime) > MAX_CONTEXT_AGE_SECONDS) continue;

        if (st.st_mtime > best_mtime) {
            best_mtime = st.st_mtime;
            snprintf(best, sizeof(best), "%s", fpath);
        }
    }
    closedir(dp);

    if (!best[0]) return 0;

    FILE *fp = fopen(best, "r");
    if (!fp) return 0;

    size_t n = fread(buf, 1, buf_len - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return (n > 0) ? 1 : 0;
}

int group_chat_context_load(const char *workspace, char *buf, size_t buf_len)
{
    if (!workspace || !buf || buf_len == 0) return 0;

    /* Prefer the most recently modified session context file. */
    if (load_from_sessions_dir(workspace, buf, buf_len)) return 1;

    /* Fall back to the global recent_group_chat.txt. */
    char path[512];
    snprintf(path, sizeof(path), "%s/data/recent_group_chat.txt", workspace);

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (st.st_size == 0) return 0;

    time_t age = time(NULL) - st.st_mtime;
    if (age > MAX_CONTEXT_AGE_SECONDS) return 0;

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    size_t n = fread(buf, 1, buf_len - 1, fp);
    fclose(fp);

    buf[n] = '\0';
    return (n > 0) ? 1 : 0;
}
