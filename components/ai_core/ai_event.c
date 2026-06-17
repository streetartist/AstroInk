#include "ai_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "ai_event";

static QueueHandle_t s_queue;

esp_err_t ai_event_init(void)
{
    if (s_queue) return ESP_OK;
    s_queue = xQueueCreate(AI_EVENT_QUEUE_DEPTH, sizeof(ai_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "queue create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ai_event_post(const ai_event_t *ev)
{
    if (!ev) return ESP_ERR_INVALID_ARG;
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropping event type=%u", ev->type);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ai_event_post_isr(const ai_event_t *ev, bool *woken)
{
    if (!ev) return ESP_ERR_INVALID_ARG;
    if (!s_queue) return ESP_ERR_INVALID_STATE;

    BaseType_t hp_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_queue, ev, &hp_task_woken) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    if (woken) *woken = hp_task_woken == pdTRUE;
    return ESP_OK;
}

bool ai_event_poll(ai_event_t *out, int timeout_ms)
{
    if (!out || !s_queue) return false;
    if (timeout_ms < 0) timeout_ms = 0;
    return xQueueReceive(s_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
