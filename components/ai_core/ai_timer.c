#include "ai_timer.h"
#include "ai_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "ai_timer";

typedef struct {
    bool    used;
    bool    repeat;
    uint8_t gen;
    int64_t interval_ms;
    int64_t due_ms;
} timer_slot_t;

static timer_slot_t s_timers[AI_TIMER_MAX];

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int make_id(int idx, uint8_t gen)
{
    return ((int)gen << 8) | idx;
}

static bool decode_id(int id, int *idx, uint8_t *gen)
{
    int i = id & 0xFF;
    if (id <= 0 || i < 0 || i >= AI_TIMER_MAX) return false;
    if (idx) *idx = i;
    if (gen) *gen = (uint8_t)((id >> 8) & 0xFF);
    return true;
}

esp_err_t ai_timer_init(void)
{
    return ai_event_init();
}

int ai_timer_create(int interval_ms, bool repeat)
{
    if (interval_ms <= 0) return -1;
    if (ai_event_init() != ESP_OK) return -1;

    for (int i = 0; i < AI_TIMER_MAX; i++) {
        timer_slot_t *t = &s_timers[i];
        if (t->used) continue;

        t->used = true;
        t->repeat = repeat;
        t->interval_ms = interval_ms;
        t->due_ms = now_ms() + interval_ms;
        if (++t->gen == 0) t->gen = 1;
        return make_id(i, t->gen);
    }

    ESP_LOGW(TAG, "no free timer slots");
    return -1;
}

void ai_timer_cancel(int id)
{
    int idx;
    uint8_t gen;
    if (!decode_id(id, &idx, &gen)) return;

    timer_slot_t *t = &s_timers[idx];
    if (!t->used || t->gen != gen) return;
    t->used = false;
}

void ai_timer_cancel_all(void)
{
    for (int i = 0; i < AI_TIMER_MAX; i++) {
        if (s_timers[i].used) {
            s_timers[i].used = false;
            if (++s_timers[i].gen == 0) s_timers[i].gen = 1;
        }
    }
}

void ai_timer_tick(void)
{
    int64_t now = now_ms();

    for (int i = 0; i < AI_TIMER_MAX; i++) {
        timer_slot_t *t = &s_timers[i];
        if (!t->used || now < t->due_ms) continue;

        int id = make_id(i, t->gen);
        ai_event_t ev = {
            .type = AI_EV_TIMER,
            .a = id,
            .b = t->repeat ? 1 : 0,
        };
        esp_err_t err = ai_event_post(&ev);
        if (err != ESP_OK)
            ESP_LOGW(TAG, "timer event post failed: %s", esp_err_to_name(err));

        if (t->repeat) {
            do {
                t->due_ms += t->interval_ms;
            } while (now >= t->due_ms);
        } else {
            t->used = false;
        }
    }
}
