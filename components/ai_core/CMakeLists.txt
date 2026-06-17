idf_component_register(
    SRCS "ai_event.c" "ai_timer.c" "ai_loop.c" "ai_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)
