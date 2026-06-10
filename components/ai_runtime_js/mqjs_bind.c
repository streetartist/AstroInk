/*
 * AstroInk <-> mquickjs binding layer.
 *
 * Defines every app-level C function the generated stdlib references
 * (print / Date / performance + the ai.* namespace), then pulls in the
 * generated ROM tables (ai_stdlib.h) which expose `js_stdlib`.
 *
 * The ai.* functions bridge JS to the AstroInk System API (ai_fs/ai_kv/ai_sys)
 * and Display HAL (ai_display).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mquickjs.h"
#include "esp_timer.h"

#include "ai_fs.h"
#include "ai_kv.h"
#include "ai_sys.h"
#include "ai_display.h"

// Hard cap for reads into the JS heap; real size comes from ai_fs_size().
#define AI_READ_FILE_MAX (64 * 1024)

// ---------------- console / print ----------------

static void print_values(JSContext *ctx, int argc, JSValue *argv)
{
    for (int i = 0; i < argc; i++) {
        if (i != 0) putchar(' ');
        if (JS_IsString(ctx, argv[i])) {
            JSCStringBuf buf;
            size_t len;
            const char *s = JS_ToCStringLen(ctx, &len, argv[i], &buf);
            if (s) fwrite(s, 1, len, stdout);
        } else {
            JS_PrintValueF(ctx, argv[i], JS_DUMP_LONG);
        }
    }
    putchar('\n');
}

static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    print_values(ctx, argc, argv);
    return JS_UNDEFINED;
}

// ---------------- time (Date / performance) ----------------

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewInt64(ctx, now_ms());
}

static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    // No wall-clock RTC yet; monotonic ms since boot. TODO: real RTC in P4.
    return JS_NewInt64(ctx, now_ms());
}

static JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    double val;
    argc &= ~FRAME_CF_CTOR;
    if (argc == 0) {
        val = (double)now_ms();
    } else if (argc == 1 && JS_IsNumber(ctx, argv[0])) {
        if (JS_ToNumber(ctx, &val, argv[0]))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "unsupported Date() parameter");
    }
    return JS_NewDate(ctx, val);
}

// ---------------- ai.* helpers ----------------

// Copy a JS string argument into a freshly malloc'd C string. Caller frees.
static char *dup_arg_cstr(JSContext *ctx, JSValue v)
{
    if (!JS_IsString(ctx, v)) return NULL;
    JSCStringBuf buf;
    size_t len;
    const char *s = JS_ToCStringLen(ctx, &len, v, &buf);
    if (!s) return NULL;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

// ---------------- ai.* JS bindings ----------------

static JSValue js_ai_log(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    print_values(ctx, argc, argv);
    return JS_UNDEFINED;
}

static JSValue js_ai_millis(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return JS_NewInt64(ctx, ai_sys_millis());
}

static JSValue js_ai_sleep(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int ms = 0;
    if (argc >= 1 && JS_ToInt32(ctx, &ms, argv[0]))
        return JS_EXCEPTION;
    ai_sys_sleep(ms);
    return JS_UNDEFINED;
}

static JSValue js_ai_kv_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char *key = dup_arg_cstr(ctx, argv[0]);
    if (!key) return JS_ThrowTypeError(ctx, "kvGet(key): string expected");

    int need = ai_kv_get_len(key);   // value length + NUL, straight from NVS
    if (need <= 0) { free(key); return JS_NULL; }

    char *val = malloc((size_t)need);
    if (!val) { free(key); return JS_ThrowOutOfMemory(ctx); }

    int n = ai_kv_get(key, val, need);
    free(key);
    if (n < 0) { free(val); return JS_NULL; }

    JSValue ret = JS_NewStringLen(ctx, val, (size_t)n);
    free(val);
    return ret;
}

static JSValue js_ai_kv_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char *key = dup_arg_cstr(ctx, argv[0]);
    char *val = dup_arg_cstr(ctx, argv[1]);
    if (key && val) ai_kv_set(key, val);
    free(key);
    free(val);
    return JS_UNDEFINED;
}

static JSValue js_ai_read_file(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char *path = dup_arg_cstr(ctx, argv[0]);
    if (!path) return JS_ThrowTypeError(ctx, "readFile(path): string expected");

    long size = ai_fs_size(path);
    if (size < 0) { free(path); return JS_NULL; }
    if (size > AI_READ_FILE_MAX) size = AI_READ_FILE_MAX;

    int fd = ai_fs_open(path, "r");
    free(path);
    if (fd < 0) return JS_NULL;

    char *buf = malloc((size_t)size + 1);
    if (!buf) { ai_fs_close(fd); return JS_ThrowOutOfMemory(ctx); }

    int total = 0, n;
    while (total < (int)size &&
           (n = ai_fs_read(fd, buf + total, (int)size - total)) > 0) {
        total += n;
    }
    ai_fs_close(fd);

    JSValue ret = JS_NewStringLen(ctx, buf, (size_t)total);
    free(buf);
    return ret;
}

static JSValue js_ai_write_file(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char *path = dup_arg_cstr(ctx, argv[0]);
    if (!path) return JS_ThrowTypeError(ctx, "writeFile(path, data): path expected");

    // Validate data BEFORE opening with "w": opening truncates, and a bad
    // data arg must not destroy the existing file.
    char *data = dup_arg_cstr(ctx, argv[1]);
    if (!data) {
        free(path);
        return JS_ThrowTypeError(ctx, "writeFile(path, data): data string expected");
    }

    int fd = ai_fs_open(path, "w");
    free(path);
    if (fd < 0) { free(data); return JS_NewInt32(ctx, -1); }

    int written = ai_fs_write(fd, data, (int)strlen(data));
    free(data);
    ai_fs_close(fd);
    return JS_NewInt32(ctx, written);
}

static JSValue js_ai_exists(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    char *path = dup_arg_cstr(ctx, argv[0]);
    if (!path) return JS_ThrowTypeError(ctx, "exists(path): string expected");
    bool e = ai_fs_exists(path);
    free(path);
    return JS_NewBool(e);
}

static JSValue js_ai_screen_w(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int w = 0, h = 0;
    ai_display_size(&w, &h);
    return JS_NewInt32(ctx, w);
}

static JSValue js_ai_screen_h(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    int w = 0, h = 0;
    ai_display_size(&w, &h);
    return JS_NewInt32(ctx, h);
}

// Pulls in `js_stdlib` (ROM tables) referencing every function above.
#include "ai_stdlib.h"
