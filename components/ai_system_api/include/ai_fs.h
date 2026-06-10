#pragma once
// AstroInk System API — file system (architecture §4.4).
//
// Thin, language-agnostic wrapper over the VFS mount points (/system, /sd).
// Apps and language bindings call only ai_fs_*; they never see LittleFS/FATFS.
// Paths are normal VFS paths, e.g. "/system/cfg.json", "/sd/apps/clock/main.js".

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_FS_NAME_MAX 256

typedef struct {
    char name[AI_FS_NAME_MAX];
    bool is_dir;
} ai_dirent_t;

// Open a file. `mode` is stdio-style: "r" "w" "a" "r+" "w+" "a+".
// Returns a non-negative descriptor, or -1 on error.
int  ai_fs_open(const char *path, const char *mode);

// Read/write up to `len` bytes. Return bytes transferred, or -1 on error.
int  ai_fs_read(int fd, void *buf, int len);
int  ai_fs_write(int fd, const void *buf, int len);

void ai_fs_close(int fd);

bool ai_fs_exists(const char *path);

// Size of a regular file in bytes, or -1 if missing / not a regular file.
long ai_fs_size(const char *path);

// Fill `out[0..max-1]` with directory entries. Returns the count written
// (>=0) or -1 on error.
int  ai_fs_listdir(const char *path, ai_dirent_t *out, int max);

#ifdef __cplusplus
}
#endif
