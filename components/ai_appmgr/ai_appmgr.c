#include "ai_appmgr.h"
#include "ai_fs.h"
#include "ai_runtime.h"
#include "ai_vfs.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ai_appmgr";

typedef struct {
    const ai_runtime_t *runtime;
    void *vm;
    ai_app_info_t info;
    bool exit_requested;
    int exit_code;
} app_state_t;

static app_state_t s_app;
static ai_app_info_t s_catalog[AI_APP_MAX];
static int s_catalog_count;

static void copy_field(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static esp_err_t join_path(char *dst, size_t dst_size, const char *a, const char *b)
{
    if (!dst || dst_size == 0 || !a || !b) return ESP_ERR_INVALID_ARG;
    int n = snprintf(dst, dst_size, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= dst_size) {
        if (dst_size > 0) dst[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static void destroy_current(void)
{
    if (!s_app.vm || !s_app.runtime) return;
    ESP_LOGI(TAG, "destroy app '%s'", s_app.info.id);
    s_app.runtime->destroy(s_app.vm);
    memset(&s_app, 0, sizeof(s_app));
}

static int read_text_file(const char *path, char **out, long max_size)
{
    *out = NULL;
    long size = ai_fs_size(path);
    if (size < 0 || size > max_size) return -1;

    int fd = ai_fs_open(path, "r");
    if (fd < 0) return -1;

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        ai_fs_close(fd);
        return -1;
    }

    int total = 0;
    while (total < (int)size) {
        int n = ai_fs_read(fd, buf + total, (int)size - total);
        if (n <= 0) break;
        total += n;
    }
    ai_fs_close(fd);
    buf[total] = '\0';
    *out = buf;
    return total;
}

static bool json_string_to_field(cJSON *root, const char *name, char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0')
        return false;
    copy_field(dst, dst_size, item->valuestring);
    return true;
}

static esp_err_t parse_manifest(const char *json, ai_app_info_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    bool ok = json_string_to_field(root, "id", out->id, sizeof(out->id)) &&
              json_string_to_field(root, "lang", out->lang, sizeof(out->lang)) &&
              json_string_to_field(root, "entry", out->entry, sizeof(out->entry));

    cJSON *heap_kb = cJSON_GetObjectItemCaseSensitive(root, "heap_kb");
    int kb = cJSON_IsNumber(heap_kb) ? heap_kb->valueint : 64;
    if (kb <= 0) kb = 64;
    if (kb > 512) kb = 512;
    out->heap_bytes = (size_t)kb * 1024;

    cJSON_Delete(root);
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static int scan_root(const char *root)
{
    ai_dirent_t entries[AI_APP_MAX];
    int n = ai_fs_listdir(root, entries, AI_APP_MAX);
    if (n < 0) {
        ESP_LOGW(TAG, "scan skipped: %s not readable", root);
        return 0;
    }

    int added = 0;
    for (int i = 0; i < n && s_catalog_count < AI_APP_MAX; i++) {
        if (!entries[i].is_dir) continue;

        char dir[AI_APP_DIR_MAX];
        char manifest_path[AI_APP_ENTRY_MAX];
        if (join_path(dir, sizeof(dir), root, entries[i].name) != ESP_OK ||
            join_path(manifest_path, sizeof(manifest_path), dir, "manifest.json") != ESP_OK) {
            ESP_LOGW(TAG, "app path too long under %s: %s", root, entries[i].name);
            continue;
        }

        char *json = NULL;
        if (read_text_file(manifest_path, &json, 4096) < 0) continue;

        ai_app_info_t info;
        esp_err_t err = parse_manifest(json, &info);
        free(json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "invalid manifest: %s", manifest_path);
            continue;
        }

        char entry_rel[AI_APP_ENTRY_MAX];
        copy_field(entry_rel, sizeof(entry_rel), info.entry);
        copy_field(info.dir, sizeof(info.dir), dir);
        if (join_path(info.entry, sizeof(info.entry), info.dir, entry_rel) != ESP_OK) {
            ESP_LOGW(TAG, "entry path too long for app '%s'", info.id);
            continue;
        }

        s_catalog[s_catalog_count++] = info;
        added++;
        ESP_LOGI(TAG, "found app '%s' (%s) entry=%s", info.id, info.lang, info.entry);
    }
    return added;
}

esp_err_t ai_appmgr_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalog_count = 0;
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}

int ai_app_scan(void)
{
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalog_count = 0;

    scan_root(AI_VFS_SYSTEM "/apps");
    scan_root(AI_VFS_SD "/apps");
    ESP_LOGI(TAG, "scan complete: %d app(s)", s_catalog_count);
    return s_catalog_count;
}

int ai_app_count(void)
{
    return s_catalog_count;
}

const ai_app_info_t *ai_app_get(int idx)
{
    if (idx < 0 || idx >= s_catalog_count) return NULL;
    return &s_catalog[idx];
}

esp_err_t ai_app_launch_file(const char *id, const char *lang,
                             const char *entry, size_t heap_bytes)
{
    if (!id || !lang || !entry) return ESP_ERR_INVALID_ARG;

    const ai_runtime_t *runtime = ai_runtime_find(lang);
    if (!runtime) {
        ESP_LOGE(TAG, "runtime '%s' not found", lang);
        return ESP_ERR_NOT_FOUND;
    }

    destroy_current();

    void *vm = runtime->create(heap_bytes);
    if (!vm) {
        ESP_LOGE(TAG, "create VM failed for '%s'", id);
        return ESP_ERR_NO_MEM;
    }

    copy_field(s_app.info.id, sizeof(s_app.info.id), id);
    copy_field(s_app.info.lang, sizeof(s_app.info.lang), lang);
    copy_field(s_app.info.entry, sizeof(s_app.info.entry), entry);
    s_app.info.heap_bytes = heap_bytes;
    s_app.runtime = runtime;
    s_app.vm = vm;

    ESP_LOGI(TAG, "launch '%s' (%s) entry=%s", s_app.info.id, s_app.info.lang, s_app.info.entry);
    if (runtime->run_file(vm, entry) != 0) {
        ESP_LOGE(TAG, "app '%s' failed during entry run", id);
        destroy_current();
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ai_app_launch(const char *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < s_catalog_count; i++) {
        const ai_app_info_t *app = &s_catalog[i];
        if (strcmp(app->id, id) == 0)
            return ai_app_launch_file(app->id, app->lang, app->entry, app->heap_bytes);
    }
    ESP_LOGE(TAG, "app '%s' not found", id);
    return ESP_ERR_NOT_FOUND;
}

void ai_app_exit(int code)
{
    if (!s_app.vm) return;
    s_app.exit_requested = true;
    s_app.exit_code = code;
}

const ai_app_info_t *ai_app_current(void)
{
    return s_app.vm ? &s_app.info : NULL;
}

void ai_appmgr_pump(void)
{
    if (!s_app.vm || !s_app.runtime) return;

    if (s_app.exit_requested) {
        ESP_LOGI(TAG, "app '%s' exit code=%d", s_app.info.id, s_app.exit_code);
        destroy_current();
        return;
    }

    if (s_app.runtime->pump)
        s_app.runtime->pump(s_app.vm);
}

void ai_appmgr_dispatch(const ai_event_t *ev)
{
    if (!ev || !s_app.vm || !s_app.runtime || !s_app.runtime->dispatch) return;
    if (s_app.runtime->dispatch(s_app.vm, ev) != 0) {
        ESP_LOGW(TAG, "app '%s' dispatch failed for event type=%u", s_app.info.id, ev->type);
    }
}
