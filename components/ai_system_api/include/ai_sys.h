#pragma once
// AstroInk System API — misc system services (architecture §4.4).

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Milliseconds since boot (monotonic).
int64_t ai_sys_millis(void);

// Block the current task for `ms` milliseconds.
void    ai_sys_sleep(int ms);

// Battery state. On success sets *percent (0-100) and *charging.
// NOT YET IMPLEMENTED on this board: sets percent=-1, charging=false until the
// ADC divider (sheet P1) is calibrated. Safe to call.
void    ai_sys_battery(int *percent, bool *charging);

#ifdef __cplusplus
}
#endif
