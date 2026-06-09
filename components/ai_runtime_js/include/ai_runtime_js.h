#pragma once
// AstroInk JavaScript runtime (mquickjs) lifecycle wrapper.
//
// One VM at a time (architecture §2.3): create with a PSRAM heap budget, eval
// source / run a file, then destroy. The `ai.*` global namespace bridges to the
// AstroInk System API (see mqjs_bind.c).

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_js_vm ai_js_vm;

// Default VM heap if 0 is passed to ai_js_create (bytes, allocated in PSRAM).
#define AI_JS_DEFAULT_HEAP (64 * 1024)

// Create a VM with a `heap_bytes` memory arena (0 -> AI_JS_DEFAULT_HEAP).
// Returns NULL on allocation failure.
ai_js_vm *ai_js_create(size_t heap_bytes);

// Evaluate a source buffer. `name` is used in error messages. Returns 0 on
// success, -1 on a JS exception (which is printed to the console).
int  ai_js_eval(ai_js_vm *vm, const char *src, size_t len, const char *name);

// Read and evaluate a script file (via the VFS, e.g. "/sd/apps/x/main.js").
int  ai_js_run_file(ai_js_vm *vm, const char *path);

void ai_js_destroy(ai_js_vm *vm);

#ifdef __cplusplus
}
#endif
