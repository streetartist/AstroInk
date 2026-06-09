// AstroInk Display HAL — active-driver registry + convenience wrappers.

#include "ai_display.h"
#include "esp_log.h"

static const char *TAG = "ai_display";

static const ai_display_drv_t *s_active;

esp_err_t ai_display_register(const ai_display_drv_t *drv)
{
    if (!drv || !drv->init || !drv->flush_full) {
        ESP_LOGE(TAG, "register: driver missing required ops");
        return ESP_ERR_INVALID_ARG;
    }
    s_active = drv;
    ESP_LOGI(TAG, "registered '%s' (%ux%u, %ubpp, partial=%d)",
             drv->name ? drv->name : "?", drv->width, drv->height,
             drv->bpp, drv->supports_partial);
    return ESP_OK;
}

const ai_display_drv_t *ai_display_active(void) { return s_active; }

esp_err_t ai_display_init(void)
{
    if (!s_active) {
        ESP_LOGE(TAG, "init: no driver registered");
        return ESP_ERR_INVALID_STATE;
    }
    return s_active->init();
}

void ai_display_flush_full(const uint8_t *fb)
{
    if (s_active && s_active->flush_full) s_active->flush_full(fb);
}

void ai_display_flush_partial(int x, int y, int w, int h, const uint8_t *fb)
{
    if (!s_active) return;
    // Fall back to a full flush if the panel can't do partial refresh.
    if (s_active->supports_partial && s_active->flush_partial)
        s_active->flush_partial(x, y, w, h, fb);
    else if (s_active->flush_full)
        s_active->flush_full(fb);
}

void ai_display_sleep(void)
{
    if (s_active && s_active->sleep) s_active->sleep();
}

void ai_display_wakeup(void)
{
    if (s_active && s_active->wakeup) s_active->wakeup();
}

void ai_display_size(int *w, int *h)
{
    if (w) *w = s_active ? s_active->width  : 0;
    if (h) *h = s_active ? s_active->height : 0;
}
