#include "ai_loop.h"
#include "ai_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ai_loop";

static ai_loop_event_handler_t s_event_handler;
static void *s_event_ctx;
static ai_loop_pump_t s_pump;
static void *s_pump_ctx;

static void default_event_handler(const ai_event_t *ev, void *ctx)
{
    (void)ctx;
    if (ev->type == AI_EV_TIMER)
        ESP_LOGI(TAG, "event: timer id=%ld repeat=%ld", (long)ev->a, (long)ev->b);
    else
        ESP_LOGI(TAG, "event: type=%u a=%ld b=%ld", ev->type, (long)ev->a, (long)ev->b);
}

void ai_loop_set_event_handler(ai_loop_event_handler_t handler, void *ctx)
{
    s_event_handler = handler;
    s_event_ctx = ctx;
}

void ai_loop_set_pump(ai_loop_pump_t pump, void *ctx)
{
    s_pump = pump;
    s_pump_ctx = ctx;
}

void ai_loop_once(int wait_ms)
{
    ai_timer_tick();

    ai_event_t ev;
    while (ai_event_poll(&ev, 0)) {
        ai_loop_event_handler_t handler = s_event_handler ? s_event_handler : default_event_handler;
        handler(&ev, s_event_ctx);
    }

    if (s_pump) s_pump(s_pump_ctx);

    if (wait_ms > 0)
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
}

void ai_loop_run(void)
{
    ESP_LOGI(TAG, "run");
    while (1) {
        ai_loop_once(5);
    }
}
