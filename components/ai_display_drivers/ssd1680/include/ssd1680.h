#pragma once
// SSD1680 B/W e-paper driver (SPI) for AstroInk.
// Geometry and pins come from the active board header (ai_board).

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "board.h"

#define SSD1680_WIDTH        BOARD_EPD_WIDTH
#define SSD1680_HEIGHT       BOARD_EPD_HEIGHT
#define SSD1680_ROW_BYTES    ((SSD1680_WIDTH + 7) / 8)
#define SSD1680_FB_SIZE      (SSD1680_ROW_BYTES * SSD1680_HEIGHT)

#ifdef __cplusplus
extern "C" {
#endif

// Initialize GPIO + SPI + panel. Clears the framebuffer to white.
esp_err_t ssd1680_init(void);

// Raw 1bpp framebuffer (row-major, MSB = leftmost pixel).
uint8_t *ssd1680_framebuffer(void);

// Framebuffer drawing (does NOT touch the panel; call a refresh after).
void ssd1680_clear(bool black);
void ssd1680_draw_pixel(int x, int y, bool black);
void ssd1680_fill_rect(int x, int y, int w, int h, bool black);

// Push the framebuffer to the panel.
//  - full   : slow (~2s), flicker, removes ghosting. Use sparingly.
//  - partial: fast (~0.3s), region must be byte-aligned in X (auto-aligned).
void ssd1680_refresh_full(void);
void ssd1680_refresh_partial(int x, int y, int w, int h);

// Enter deep sleep (panel retains image).
void ssd1680_sleep(void);

// Wake from sleep: re-run the panel/controller init (full mode). Reuses the
// already-initialized SPI bus, so it is NOT a full ssd1680_init().
void ssd1680_wake(void);

#ifdef __cplusplus
}
#endif
