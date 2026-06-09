idf_component_register(
    SRCS "ai_fs.c" "ai_kv.c" "ai_sys.c" "ai_system_api.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_timer
)
