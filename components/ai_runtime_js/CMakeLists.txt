# AstroInk JavaScript runtime (mquickjs).
#
# The mquickjs engine sources live at the repo top-level (../../mquickjs) and
# are compiled into this component. Two generated headers in generated/
# (mquickjs_atom.h, ai_stdlib.h) are produced by tools/gen_stdlib.sh from
# ai_js_stdlib.c; regenerate them whenever the ai.* API surface changes.

set(MQJS "${CMAKE_CURRENT_LIST_DIR}/../../mquickjs")

idf_component_register(
    SRCS "ai_runtime_js.c"
         "mqjs_bind.c"
         "${MQJS}/mquickjs.c"
         "${MQJS}/dtoa.c"
         "${MQJS}/libm.c"
         "${MQJS}/cutils.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "generated" "${MQJS}"
    REQUIRES ai_system_api ai_hal ai_core ai_appmgr
    PRIV_REQUIRES esp_timer
)

# mquickjs is third-party C; keep its many style warnings from failing the build.
set_source_files_properties(
    "${MQJS}/mquickjs.c" "${MQJS}/dtoa.c" "${MQJS}/libm.c" "${MQJS}/cutils.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-w"
)
