#include "ai_runtime.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ai_runtime";

#define AI_RUNTIME_MAX 8

static const ai_runtime_t *s_runtimes[AI_RUNTIME_MAX];

esp_err_t ai_runtime_register(const ai_runtime_t *runtime)
{
    if (!runtime || !runtime->lang || !runtime->create ||
        !runtime->run_file || !runtime->destroy) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < AI_RUNTIME_MAX; i++) {
        if (s_runtimes[i] && strcmp(s_runtimes[i]->lang, runtime->lang) == 0) {
            s_runtimes[i] = runtime;
            ESP_LOGI(TAG, "replaced runtime '%s'", runtime->lang);
            return ESP_OK;
        }
    }

    for (int i = 0; i < AI_RUNTIME_MAX; i++) {
        if (!s_runtimes[i]) {
            s_runtimes[i] = runtime;
            ESP_LOGI(TAG, "registered runtime '%s'", runtime->lang);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "registry full, cannot register '%s'", runtime->lang);
    return ESP_ERR_NO_MEM;
}

const ai_runtime_t *ai_runtime_find(const char *lang)
{
    if (!lang) return NULL;
    for (int i = 0; i < AI_RUNTIME_MAX; i++) {
        if (s_runtimes[i] && strcmp(s_runtimes[i]->lang, lang) == 0)
            return s_runtimes[i];
    }
    return NULL;
}
