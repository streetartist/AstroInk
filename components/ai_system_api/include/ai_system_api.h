#pragma once
// AstroInk System API — umbrella header (architecture §4.4).
//
// The single C interface every language binding (JS/Lua/Python) targets.
// UI (ai_ui_*) is added once LVGL lands; the storage/system/kv subset below
// is the part that is panel/UI-independent.

#include "ai_fs.h"
#include "ai_kv.h"
#include "ai_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the UI-independent System API subsystems (currently KV/NVS).
// VFS is mounted separately via ai_vfs_init().
esp_err_t ai_system_api_init(void);

#ifdef __cplusplus
}
#endif
