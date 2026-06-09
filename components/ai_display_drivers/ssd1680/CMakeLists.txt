idf_component_register(
    SRCS "ssd1680.c" "ssd1680_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES ai_board driver ai_hal
)
