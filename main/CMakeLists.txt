idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ai_board ssd1680 ai_hal ai_vfs ai_system_api ai_runtime_js ai_core ai_appmgr
)
