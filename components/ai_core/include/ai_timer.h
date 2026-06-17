#pragma once
// AstroInk software timers.
//
// Timers never call user callbacks directly. Expiry posts AI_EV_TIMER events
// and the main OS loop decides how to dispatch them.

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_TIMER_MAX 16

esp_err_t ai_timer_init(void);
int       ai_timer_create(int interval_ms, bool repeat);
void      ai_timer_cancel(int id);
void      ai_timer_cancel_all(void);
void      ai_timer_tick(void);

#ifdef __cplusplus
}
#endif
