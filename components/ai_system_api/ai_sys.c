// AstroInk System API — misc system services.

#include "ai_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

int64_t ai_sys_millis(void)
{
    return esp_timer_get_time() / 1000;
}

void ai_sys_sleep(int ms)
{
    if (ms < 0) ms = 0;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void ai_sys_battery(int *percent, bool *charging)
{
    // TODO: read the battery ADC divider (schematic sheet P1, net ADC->IO1)
    // and the TP4056 charge status, then map to %/charging. Needs calibration.
    if (percent)  *percent = -1;
    if (charging) *charging = false;
}
