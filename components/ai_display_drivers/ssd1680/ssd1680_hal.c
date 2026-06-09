// SSD1680 -> AstroInk Display HAL adapter.
// Exposes the low-level ssd1680 driver as a pluggable ai_display_drv_t.

#include "ssd1680.h"
#include "ai_display.h"
#include <string.h>

// The HAL hands us a whole-frame buffer; the low-level driver keeps its own
// framebuffer and refreshes from it. Copy in, then refresh.
static esp_err_t hal_init(void)
{
    return ssd1680_init();
}

static void hal_flush_full(const uint8_t *fb)
{
    if (fb) memcpy(ssd1680_framebuffer(), fb, SSD1680_FB_SIZE);
    ssd1680_refresh_full();
}

static void hal_flush_partial(int x, int y, int w, int h, const uint8_t *fb)
{
    if (fb) memcpy(ssd1680_framebuffer(), fb, SSD1680_FB_SIZE);
    ssd1680_refresh_partial(x, y, w, h);
}

static void hal_sleep(void)  { ssd1680_sleep(); }
static void hal_wakeup(void) { ssd1680_wake(); }

static const ai_display_drv_t s_ssd1680_drv = {
    .name             = "ssd1680",
    .width            = SSD1680_WIDTH,
    .height           = SSD1680_HEIGHT,
    .bpp              = 1,
    .supports_partial = true,
    .init             = hal_init,
    .flush_full       = hal_flush_full,
    .flush_partial    = hal_flush_partial,
    .sleep            = hal_sleep,
    .wakeup           = hal_wakeup,
};

const ai_display_drv_t *ssd1680_get_driver(void)
{
    return &s_ssd1680_drv;
}
