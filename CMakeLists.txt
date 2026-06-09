# AstroInk — ESP32-S3 e-ink OS
# ESP-IDF v5.5.2
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components/ai_display_drivers/ssd1680)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(astroink)
