#pragma once
// AstroInk Display HAL.
//
// A pluggable e-paper abstraction: each panel/controller provides one
// `ai_display_drv_t` (a function-pointer table). The OS registers the active
// driver once at boot and from then on talks only to this interface, so the
// rest of the system (UI, System API) is panel-agnostic and multi-screen ready.
//
// Framebuffer convention (all drivers): 1bpp, row-major, MSB = leftmost pixel,
// `row_bytes = (width + 7) / 8`, total `row_bytes * height` bytes. Bit polarity
// (1=white vs 1=black) is the driver's concern; callers just hand over a buffer
// matching the panel's native bit order.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;          // e.g. "ssd1680"
    uint16_t    width;         // pixels (X)
    uint16_t    height;        // pixels (Y)
    uint8_t     bpp;           // bits per pixel (1 for B/W)
    bool        supports_partial;

    // Bring the panel up (GPIO/SPI/controller init). Returns ESP_OK on success.
    esp_err_t (*init)(void);

    // Push a full-frame buffer to the panel with a full refresh (slow, no ghost).
    // `fb` may be NULL: drivers that keep an internal framebuffer then just
    // refresh their current contents (handy for low-level drawing at bring-up).
    void (*flush_full)(const uint8_t *fb);

    // Push the (x,y,w,h) dirty region with a fast partial refresh. `fb` is the
    // whole-frame buffer (or NULL, same meaning as flush_full); the driver reads
    // the region out of it. X is auto byte-aligned by the driver.
    void (*flush_partial)(int x, int y, int w, int h, const uint8_t *fb);

    void (*sleep)(void);       // enter deep sleep (panel keeps last image)
    void (*wakeup)(void);      // re-init controller after sleep
} ai_display_drv_t;

// Bytes in a full 1bpp framebuffer for this driver.
static inline size_t ai_display_fb_size(const ai_display_drv_t *drv)
{
    if (!drv) return 0;
    return (size_t)((drv->width + 7) / 8) * drv->height;
}

// ---- Active-driver registry ----
// Register the panel driver the OS should use, then call ai_display_init().
esp_err_t                ai_display_register(const ai_display_drv_t *drv);
const ai_display_drv_t  *ai_display_active(void);

// Convenience wrappers over the active driver (no-op / error if none registered).
esp_err_t ai_display_init(void);
void      ai_display_flush_full(const uint8_t *fb);
void      ai_display_flush_partial(int x, int y, int w, int h, const uint8_t *fb);
void      ai_display_sleep(void);
void      ai_display_wakeup(void);
void      ai_display_size(int *w, int *h);

#ifdef __cplusplus
}
#endif
