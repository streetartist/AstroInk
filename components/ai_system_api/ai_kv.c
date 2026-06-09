// AstroInk System API — persistent key/value store (NVS-backed).

#include "ai_kv.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "ai_kv";
#define KV_NAMESPACE "ai_kv"

static bool s_ready;

esp_err_t ai_kv_init(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

void ai_kv_set(const char *key, const char *value)
{
    if (!s_ready || !key || !value) return;

    nvs_handle_t h;
    if (nvs_open(KV_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, key, value) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

int ai_kv_get(const char *key, char *out, int max)
{
    if (!s_ready || !key || !out || max <= 0) return -1;

    nvs_handle_t h;
    if (nvs_open(KV_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return -1;

    size_t len = (size_t)max;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);

    if (err != ESP_OK) return -1;
    return (int)(len > 0 ? len - 1 : 0);  // len includes the NUL terminator
}

void ai_kv_erase(const char *key)
{
    if (!s_ready || !key) return;

    nvs_handle_t h;
    if (nvs_open(KV_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_key(h, key) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}
