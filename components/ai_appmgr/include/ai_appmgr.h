#pragma once
// AstroInk minimal App Manager.
//
// This first slice supports launching one foreground app from a script file via
// the generic runtime registry. Manifest scanning and lifecycle queues come
// next, but callers already avoid depending on a concrete VM implementation.

#include <stddef.h>
#include "ai_event.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_APP_ID_MAX    32
#define AI_APP_LANG_MAX  8
#define AI_APP_ENTRY_MAX 160
#define AI_APP_DIR_MAX   128
#define AI_APP_MAX       8

typedef struct {
    char   id[AI_APP_ID_MAX];
    char   lang[AI_APP_LANG_MAX];
    char   dir[AI_APP_DIR_MAX];
    char   entry[AI_APP_ENTRY_MAX];
    size_t heap_bytes;
} ai_app_info_t;

esp_err_t ai_appmgr_init(void);
int       ai_app_scan(void);
int       ai_app_count(void);
const ai_app_info_t *ai_app_get(int idx);
esp_err_t ai_app_launch_file(const char *id, const char *lang,
                             const char *entry, size_t heap_bytes);
esp_err_t ai_app_launch(const char *id);
void      ai_app_exit(int code);
const ai_app_info_t *ai_app_current(void);

void ai_appmgr_pump(void);
void ai_appmgr_dispatch(const ai_event_t *ev);

#ifdef __cplusplus
}
#endif
