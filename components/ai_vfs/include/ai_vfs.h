#pragma once
// AstroInk VFS — unified storage mount layer.
//
// Two backing stores, one path namespace (architecture §4.3):
//   /system  -> internal flash, LittleFS  (OS config, built-in apps, fonts)
//   /sd      -> microSD card, FATFS        (user apps, documents, assets)
//
// Higher layers (the ai_fs_* System API) only ever use these paths and never
// touch the underlying filesystem. SD is optional: a missing/unreadable card
// is non-fatal — /system still mounts and the OS boots.

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_VFS_SYSTEM   "/system"
#define AI_VFS_SD       "/sd"

// Mount /system (LittleFS) and attempt /sd (SD card). Returns ESP_OK if at
// least /system mounted; SD failure is logged but not fatal.
esp_err_t ai_vfs_init(void);

esp_err_t ai_vfs_mount_system(void);  // LittleFS on the "storage" partition
esp_err_t ai_vfs_mount_sd(void);      // SDMMC 4-bit + FATFS
void      ai_vfs_unmount_sd(void);

bool      ai_vfs_system_mounted(void);
bool      ai_vfs_sd_mounted(void);

#ifdef __cplusplus
}
#endif
