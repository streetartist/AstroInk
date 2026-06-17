#pragma once
// AstroInk core event queue.
//
// This is the only cross-task path into the future UI/VM loop. Producers may
// post from tasks or ISRs; consumers must poll from the main OS loop.

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_EVENT_QUEUE_DEPTH 32

typedef enum {
    AI_EV_NONE  = 0,
    AI_EV_TIMER = 1,
    AI_EV_KEY   = 2,
    AI_EV_SYS   = 3,
} ai_event_type_t;

typedef struct {
    uint16_t type;
    int32_t  a;
    int32_t  b;
} ai_event_t;

esp_err_t ai_event_init(void);
esp_err_t ai_event_post(const ai_event_t *ev);
esp_err_t ai_event_post_isr(const ai_event_t *ev, bool *woken);
bool      ai_event_poll(ai_event_t *out, int timeout_ms);

#ifdef __cplusplus
}
#endif
