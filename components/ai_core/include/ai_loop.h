#pragma once
// AstroInk main OS loop.
//
// The loop owns timer ticking, event draining, and one cooperative pump hook
// for the current foreground subsystem/app. Future LVGL and VM dispatch will
// attach here instead of creating independent loops.

#include "ai_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ai_loop_event_handler_t)(const ai_event_t *ev, void *ctx);
typedef void (*ai_loop_pump_t)(void *ctx);

void ai_loop_set_event_handler(ai_loop_event_handler_t handler, void *ctx);
void ai_loop_set_pump(ai_loop_pump_t pump, void *ctx);

void ai_loop_once(int wait_ms);
void ai_loop_run(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
