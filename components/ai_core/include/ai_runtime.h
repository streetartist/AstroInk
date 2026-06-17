#pragma once
// AstroInk language runtime abstraction.
//
// App Manager talks to this interface instead of knowing about JS/Lua/Python
// concrete VM types. Runtime implementations register themselves at boot.

#include <stddef.h>
#include "ai_event.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_runtime {
    const char *lang;
    void *(*create)(size_t heap_limit);
    int   (*run_file)(void *vm, const char *path);
    int   (*dispatch)(void *vm, const ai_event_t *ev);
    void  (*pump)(void *vm);
    void  (*request_stop)(void *vm);
    void  (*destroy)(void *vm);
} ai_runtime_t;

esp_err_t ai_runtime_register(const ai_runtime_t *runtime);
const ai_runtime_t *ai_runtime_find(const char *lang);

#ifdef __cplusplus
}
#endif
