// AstroInk System API — file system (POSIX-backed wrapper over the VFS).

#include "ai_fs.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static int mode_to_flags(const char *mode)
{
    if (!mode) return -1;
    // Compare the leading verb; ignore a trailing 'b' (binary, no-op here).
    if (!strncmp(mode, "r+", 2)) return O_RDWR;
    if (!strncmp(mode, "w+", 2)) return O_RDWR  | O_CREAT | O_TRUNC;
    if (!strncmp(mode, "a+", 2)) return O_RDWR  | O_CREAT | O_APPEND;
    if (mode[0] == 'r')          return O_RDONLY;
    if (mode[0] == 'w')          return O_WRONLY | O_CREAT | O_TRUNC;
    if (mode[0] == 'a')          return O_WRONLY | O_CREAT | O_APPEND;
    return -1;
}

int ai_fs_open(const char *path, const char *mode)
{
    int flags = mode_to_flags(mode);
    if (!path || flags < 0) return -1;
    return open(path, flags, 0644);
}

int ai_fs_read(int fd, void *buf, int len)
{
    if (fd < 0 || !buf || len < 0) return -1;
    return (int)read(fd, buf, (size_t)len);
}

int ai_fs_write(int fd, const void *buf, int len)
{
    if (fd < 0 || !buf || len < 0) return -1;
    return (int)write(fd, buf, (size_t)len);
}

void ai_fs_close(int fd)
{
    if (fd >= 0) close(fd);
}

bool ai_fs_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

int ai_fs_listdir(const char *path, ai_dirent_t *out, int max)
{
    if (!path || !out || max <= 0) return -1;

    DIR *dir = opendir(path);
    if (!dir) return -1;

    char full[AI_FS_NAME_MAX * 2];
    int n = 0;
    struct dirent *de;
    while (n < max && (de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;

        strncpy(out[n].name, de->d_name, AI_FS_NAME_MAX - 1);
        out[n].name[AI_FS_NAME_MAX - 1] = '\0';

        // Prefer d_type; fall back to stat when the FS doesn't report it.
        if (de->d_type == DT_DIR) {
            out[n].is_dir = true;
        } else if (de->d_type == DT_UNKNOWN) {
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            struct stat st;
            out[n].is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        } else {
            out[n].is_dir = false;
        }
        n++;
    }
    closedir(dir);
    return n;
}
