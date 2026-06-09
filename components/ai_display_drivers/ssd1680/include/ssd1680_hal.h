#pragma once
// SSD1680 as an AstroInk Display HAL driver.
// Register the returned table with ai_display_register() at boot.

#include "ai_display.h"

#ifdef __cplusplus
extern "C" {
#endif

const ai_display_drv_t *ssd1680_get_driver(void);

#ifdef __cplusplus
}
#endif
