/*
 * AstroInk JS standard-library definition (code-gen INPUT, host-compiled only).
 *
 * This file is NOT compiled into the firmware. It is fed to the mquickjs build
 * tool (mquickjs_build.c) on the host to emit two C-source headers:
 *   mquickjs_atom.h  (atom #defines; mquickjs.c needs it to compile)
 *   ai_stdlib.h      (the `js_stdlib` ROM tables; mqjs_bind.c includes it)
 *
 * Function names below are stringified by the JS_CFUNC_DEF macros, so the real
 * C implementations live in the firmware (mqjs_bind.c), not here.
 *
 * Regenerate with components/ai_runtime_js/tools/gen_stdlib.sh.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

/* The `ai` global namespace exposed to AstroInk apps. */
static const JSPropDef js_ai[] = {
    JS_CFUNC_DEF("log",        1, js_ai_log),
    JS_CFUNC_DEF("millis",     0, js_ai_millis),
    JS_CFUNC_DEF("sleep",      1, js_ai_sleep),
    JS_CFUNC_DEF("kvGet",      1, js_ai_kv_get),
    JS_CFUNC_DEF("kvSet",      2, js_ai_kv_set),
    JS_CFUNC_DEF("readFile",   1, js_ai_read_file),
    JS_CFUNC_DEF("writeFile",  2, js_ai_write_file),
    JS_CFUNC_DEF("exists",     1, js_ai_exists),
    JS_CFUNC_DEF("screenW",    0, js_ai_screen_w),
    JS_CFUNC_DEF("screenH",    0, js_ai_screen_h),
    JS_CFUNC_DEF("appExit",    1, js_ai_app_exit),
    JS_PROP_END,
};
static const JSClassDef js_ai_obj = JS_OBJECT_DEF("ai", js_ai);

/* Pull in the full standard library; the CONFIG_ASTROINK hook in mqjs_stdlib.c
   wires `js_ai_obj` into the global object and drops REPL-only globals. */
#define CONFIG_ASTROINK
#include "mqjs_stdlib.c"
