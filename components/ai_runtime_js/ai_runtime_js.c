// AstroInk JavaScript runtime (mquickjs) lifecycle.

#include "ai_runtime_js.h"
#include <stdio.h>
#include <stdlib.h>

#include "mquickjs.h"
#include "ai_fs.h"
#include "ai_event.h"
#include "ai_runtime.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "ai_js";

// Defined by the generated ROM tables included in mqjs_bind.c.
extern const JSSTDLibraryDef js_stdlib;

struct ai_js_vm {
    JSContext *ctx;
    void      *mem;
    bool       stop_requested;
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

    long size = ai_fs_size(path);
    if (size < 0) {
        ESP_LOGE(TAG, "cannot stat %s", path);
        return -1;
    }
    if (size > AI_JS_READ_FILE_MAX) {
        ESP_LOGE(TAG, "%s too large (%ld > %d)", path, size, AI_JS_READ_FILE_MAX);
        return -1;
    }

    int fd = ai_fs_open(path, "r");
    if (fd < 0) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return -1;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { ai_fs_close(fd); return -1; }

    int total = 0, n;
    while (total < (int)size &&
           (n = ai_fs_read(fd, buf + total, (int)size - total)) > 0) {
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

static int ai_js_dispatch(ai_js_vm *vm, const ai_event_t *ev)
{
    if (!vm || !vm->ctx || !ev) return -1;

    JSContext *ctx = vm->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "__ai_dispatch");
    if (!JS_IsFunction(ctx, fn)) return 0;

    JS_PushArg(ctx, JS_NewInt32(ctx, ev->type));
    JS_PushArg(ctx, JS_NewInt32(ctx, ev->a));
    JS_PushArg(ctx, JS_NewInt32(ctx, ev->b));
    JS_PushArg(ctx, fn);
    JS_PushArg(ctx, global);

    JSValue ret = JS_Call(ctx, 3);
    return eval_check(vm, ret);
}

static void *runtime_create(size_t heap_limit)
{
    return ai_js_create(heap_limit);
}

static int runtime_run_file(void *vm, const char *path)
{
    return ai_js_run_file((ai_js_vm *)vm, path);
}

static int runtime_dispatch(void *vm, const ai_event_t *ev)
{
    return ai_js_dispatch((ai_js_vm *)vm, ev);
}

static void runtime_pump(void *vm)
{
    (void)vm;
}

static void runtime_request_stop(void *vm)
{
    if (vm) ((ai_js_vm *)vm)->stop_requested = true;
}

static void runtime_destroy(void *vm)
{
    ai_js_destroy((ai_js_vm *)vm);
}

const ai_runtime_t *ai_runtime_js(void)
{
    static const ai_runtime_t rt = {
        .lang = "js",
        .create = runtime_create,
        .run_file = runtime_run_file,
        .dispatch = runtime_dispatch,
        .pump = runtime_pump,
        .request_stop = runtime_request_stop,
        .destroy = runtime_destroy,
    };
    return &rt;
}
