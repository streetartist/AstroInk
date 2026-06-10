idf_component_register(
    SRCS "ai_vfs.c"
    INCLUDE_DIRS "include"
    REQUIRES ai_board joltwallet__littlefs fatfs sdmmc esp_driver_sdmmc
)
