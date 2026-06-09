// AstroInk JavaScript runtime (mquickjs) lifecycle.

#include "ai_runtime_js.h"
#include <stdio.h>
#include <stdlib.h>

#include "mquickjs.h"
#include "ai_fs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ai_js";

// Defined by the generated ROM tables included in mqjs_bind.c.
extern const JSSTDLibraryDef js_stdlib;

struct ai_js_vm {
    JSContext *ctx;
    void      *mem;
};

#define AI_JS_READ_FILE_MAX (128 * 1024)

static void js_log_write(void *opaque, const void *buf, size_t len)
{
    fwrite(buf, 1, len, stdout);
}

ai_js_vm *ai_js_create(size_t heap_bytes)
{
    if (heap_bytes == 0) heap_bytes = AI_JS_DEFAULT_HEAP;

    ai_js_vm *vm = calloc(1, sizeof(*vm));
    if (!vm) return NULL;

    // VM arena in PSRAM (architecture mandate); fall back to internal RAM.
    vm->mem = heap_caps_malloc(heap_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!vm->mem) {
        ESP_LOGW(TAG, "PSRAM alloc failed, using internal RAM (%u bytes)", (unsigned)heap_bytes);
        vm->mem = malloc(heap_bytes);
    }
    if (!vm->mem) { free(vm); return NULL; }

    vm->ctx = JS_NewContext(vm->mem, heap_bytes, &js_stdlib);
    if (!vm->ctx) {
        ESP_LOGE(TAG, "JS_NewContext failed");
        free(vm->mem);
        free(vm);
        return NULL;
    }
    JS_SetLogFunc(vm->ctx, js_log_write);
    ESP_LOGI(TAG, "VM created (%u byte heap)", (unsigned)heap_bytes);
    return vm;
}

static int eval_check(ai_js_vm *vm, JSValue v)
{
    if (JS_IsException(v)) {
        JSValue e = JS_GetException(vm->ctx);
        printf("JS exception: ");
        JS_PrintValueF(vm->ctx, e, JS_DUMP_LONG);
        printf("\n");
        return -1;
    }
    return 0;
}

int ai_js_eval(ai_js_vm *vm, const char *src, size_t len, const char *name)
{
    if (!vm || !src) return -1;
    JSValue v = JS_Eval(vm->ctx, src, len, name ? name : "<eval>", JS_EVAL_STRIP_COL);
    return eval_check(vm, v);
}

int ai_js_run_file(ai_js_vm *vm, const char *path)
{
    if (!vm || !path) return -1;

    int fd = ai_fs_open(path, "r");
    if (fd < 0) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return -1;
    }

    char *buf = malloc(AI_JS_READ_FILE_MAX + 1);
    if (!buf) { ai_fs_close(fd); return -1; }

    int total = 0, n;
    while (total < AI_JS_READ_FILE_MAX &&
           (n = ai_fs_read(fd, buf + total, AI_JS_READ_FILE_MAX - total)) > 0) {
        total += n;
    }
    ai_fs_close(fd);
    buf[total] = '\0';

    int rc = ai_js_eval(vm, buf, (size_t)total, path);
    free(buf);
    return rc;
}

void ai_js_destroy(ai_js_vm *vm)
{
    if (!vm) return;
    if (vm->ctx) JS_FreeContext(vm->ctx);  // runs finalizers
    free(vm->mem);
    free(vm);
}
