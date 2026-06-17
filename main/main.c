// AstroInk P0a — display bring-up, driven through the Display HAL.
// Validates: SPI wiring, full refresh (geometry + B/W polarity), partial
// refresh (the highest-risk item, pulled forward), AND the ai_display HAL
// (register -> init -> flush) that the UI/System API will sit on.
//
// Drawing uses the ssd1680 driver's low-level helpers straight into its
// internal framebuffer; flushes go through the HAL with fb=NULL.

#include "ssd1680.h"
#include "ssd1680_hal.h"
#include "ai_display.h"
#include "ai_vfs.h"
#include "ai_appmgr.h"
#include "ai_system_api.h"
#include "ai_runtime_js.h"
#include "ai_event.h"
#include "ai_loop.h"
#include "ai_runtime.h"
#include "ai_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "astroink";

#define W SSD1680_WIDTH
#define H SSD1680_HEIGHT

typedef struct {
    int strip_h;
    int strip_y;
    int box_w;
    int box_h;
    int frame;
    int64_t next_ms;
} demo_state_t;

static demo_state_t s_demo;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void partial_refresh_pump(void *ctx);

static void loop_event_handler(const ai_event_t *ev, void *ctx)
{
    (void)ctx;
    if (ev->type == AI_EV_TIMER)
        ESP_LOGI(TAG, "event: timer id=%ld repeat=%ld", (long)ev->a, (long)ev->b);
    else
        ESP_LOGI(TAG, "event: type=%u a=%ld b=%ld", ev->type, (long)ev->a, (long)ev->b);
    ai_appmgr_dispatch(ev);
}

static void loop_pump(void *ctx)
{
    (void)ctx;
    ai_appmgr_pump();
    partial_refresh_pump(&s_demo);
}

// Draw a static test pattern to verify orientation, full coverage and polarity.
static void draw_full_test_pattern(void)
{
    ssd1680_clear(false); // all white

    // 1) Solid black border (3px) — proves all four edges are reachable.
    const int border = imin(3, imin(W, H));
    ssd1680_fill_rect(0, 0, W, border, true);
    ssd1680_fill_rect(0, H - border, W, border, true);
    ssd1680_fill_rect(0, 0, border, H, true);
    ssd1680_fill_rect(W - border, 0, border, H, true);

    // 2) Top solid bar with a white notch (polarity check: bar black, notch white).
    const int margin = imax(8, W / 18);
    const int bar_y = imax(border + 6, H / 10);
    const int bar_h = imax(12, H / 5);
    ssd1680_fill_rect(margin, bar_y, W - margin * 2, bar_h, true);
    ssd1680_fill_rect(margin * 2, bar_y + bar_h / 4,
                      imax(18, W / 7), imax(6, bar_h / 2), false);

    // 3) Diagonal line corner-to-corner (orientation / no missing rows).
    for (int i = 0; i < imin(W, H); i++)
        ssd1680_draw_pixel(i, i, true);

    // 4) Horizontal stripe band (row addressing sanity).
    const int stripe_y0 = imax(bar_y + bar_h + 8, H / 2);
    const int stripe_y1 = imin(H - 12, stripe_y0 + imax(12, H / 5));
    for (int y = stripe_y0; y < stripe_y1; y += 4)
        ssd1680_fill_rect(margin, y, W - margin * 2, 2, true);

    // 5) Nested rectangles (center).
    const int rect_w = imin(W - margin * 2, (W * 2) / 5);
    const int rect_h = imin(H / 3, 32);
    const int rect_x = (W - rect_w) / 2;
    const int rect_y = imin(H - rect_h - border - 4, stripe_y1 + 8);
    // Alternate black/white per layer so the nesting is actually visible
    // (all-black would just paint one solid block).
    for (int k = 0; k < 4; k++) {
        const int inset = k * 5;
        if (rect_w - inset * 2 <= 0 || rect_h - inset * 2 <= 0) break;
        ssd1680_fill_rect(rect_x + inset, rect_y + inset,
                          rect_w - inset * 2, rect_h - inset * 2, (k % 2) == 0);
    }

    ai_display_flush_full(NULL); // flush driver's internal framebuffer
    ESP_LOGI(TAG, "full test pattern drawn");
}

static void partial_refresh_pump(void *ctx)
{
    demo_state_t *st = (demo_state_t *)ctx;
    int64_t now = ai_sys_millis();
    if (now < st->next_ms) return;

    int x = (st->frame * 8) % (W - st->box_w);

    // Redraw the whole strip: white background + black box at new x.
    ssd1680_fill_rect(0, st->strip_y, W, st->strip_h, false);
    ssd1680_fill_rect(x, st->strip_y + 4, st->box_w, st->box_h, true);

    ai_display_flush_partial(0, st->strip_y, W, st->strip_h, NULL);
    ESP_LOGI(TAG, "partial frame %d (x=%d)", st->frame, x);

    st->frame++;

    // Every 20 partial updates, do a full refresh to clear ghosting.
    if (st->frame % 20 == 0) {
        draw_full_test_pattern();
        ESP_LOGI(TAG, "ghost-clear full refresh");
    }
    st->next_ms = ai_sys_millis() + 800;
}

// Animate a box across a strip near the bottom using partial refresh only.
static void partial_refresh_demo(void)
{
    s_demo.strip_h = imin(32, imax(20, H / 3));
    s_demo.strip_y = H - s_demo.strip_h - 6;
    s_demo.box_w = imin(30, imax(16, W / 7));
    s_demo.box_h = s_demo.strip_h - 8;
    s_demo.frame = 0;
    s_demo.next_ms = ai_sys_millis() + 1500;

    ai_loop_set_event_handler(loop_event_handler, NULL);
    ai_loop_set_pump(loop_pump, NULL);
    ai_loop_run();
}

// Exercise the System API end-to-end: KV boot counter + a /system file round-trip.
static void storage_selftest(void)
{
    // KV: persistent boot counter.
    char buf[32] = {0};
    long boots = 0;
    if (ai_kv_get("boots", buf, sizeof(buf)) > 0) boots = strtol(buf, NULL, 10);
    boots++;
    snprintf(buf, sizeof(buf), "%ld", boots);
    ai_kv_set("boots", buf);
    ESP_LOGI(TAG, "selftest: boot #%ld (KV)", boots);

    // FS: write then read back a file on /system.
    const char *path = AI_VFS_SYSTEM "/boot.txt";
    int fd = ai_fs_open(path, "w");
    if (fd >= 0) {
        char line[48];
        int n = snprintf(line, sizeof(line), "AstroInk boot %ld\n", boots);
        ai_fs_write(fd, line, n);
        ai_fs_close(fd);

        char rd[48] = {0};
        fd = ai_fs_open(path, "r");
        if (fd >= 0) {
            int got = ai_fs_read(fd, rd, sizeof(rd) - 1);
            ai_fs_close(fd);
            if (got > 0) { rd[got] = '\0'; ESP_LOGI(TAG, "selftest: read back '%.*s'", got - 1, rd); }
        }
    } else {
        ESP_LOGW(TAG, "selftest: could not open %s", path);
    }
}

static esp_err_t ensure_dir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return ESP_OK;
    ESP_LOGE(TAG, "mkdir failed: %s errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t write_text_file(const char *path, const char *text)
{
    int fd = ai_fs_open(path, "w");
    if (fd < 0) {
        ESP_LOGE(TAG, "could not write %s", path);
        return ESP_FAIL;
    }

    int len = (int)strlen(text);
    int written = ai_fs_write(fd, text, len);
    ai_fs_close(fd);
    if (written != len) {
        ESP_LOGE(TAG, "short write %s (%d/%d)", path, written, len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t write_app_smoke_files(void)
{
    static const char *manifest =
        "{\n"
        "  \"id\": \"smoke\",\n"
        "  \"name\": \"Smoke\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"lang\": \"js\",\n"
        "  \"entry\": \"main.js\",\n"
        "  \"heap_kb\": 64\n"
        "}\n";
    static const char *script =
        "ai.log('App smoke JS on AstroInk');\n"
        "ai.log('screen:', ai.screenW() + 'x' + ai.screenH());\n"
        "var n = ai.kvGet('app_runs');\n"
        "n = (n ? parseInt(n) : 0) + 1;\n"
        "ai.kvSet('app_runs', '' + n);\n"
        "ai.log('app run #' + n + ' at ' + ai.millis() + 'ms');\n"
        "var events = 0;\n"
        "function __ai_dispatch(type, a, b) {\n"
        "  if (type == 1) {\n"
        "    events++;\n"
        "    ai.log('app event timer', a, b, 'count=' + events);\n"
        "    if (events == 2) ai.appExit(0);\n"
        "  }\n"
        "}\n";

    ESP_ERROR_CHECK(ensure_dir(AI_VFS_SYSTEM "/apps"));
    ESP_ERROR_CHECK(ensure_dir(AI_VFS_SYSTEM "/apps/smoke"));
    ESP_ERROR_CHECK(write_text_file(AI_VFS_SYSTEM "/apps/smoke/manifest.json", manifest));
    ESP_ERROR_CHECK(write_text_file(AI_VFS_SYSTEM "/apps/smoke/main.js", script));
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "AstroInk P0a boot");

    // Storage: /system (LittleFS) is required; /sd (SD card) is best-effort.
    if (ai_vfs_init() != ESP_OK)
        ESP_LOGW(TAG, "VFS: /system not mounted");
    ESP_LOGI(TAG, "VFS: system=%d sd=%d",
             ai_vfs_system_mounted(), ai_vfs_sd_mounted());

    // System API (KV/NVS) + a storage round-trip self-test.
    ESP_ERROR_CHECK(ai_system_api_init());
    ESP_ERROR_CHECK(ai_event_init());
    ESP_ERROR_CHECK(ai_timer_init());
    ESP_ERROR_CHECK(ai_runtime_register(ai_runtime_js()));
    ESP_LOGI(TAG, "runtime lookup: js=%s", ai_runtime_find("js") ? "ok" : "missing");
    ESP_ERROR_CHECK(ai_appmgr_init());
    storage_selftest();

    // Register the panel with the Display HAL, then drive it through ai_display.
    ESP_ERROR_CHECK(ai_display_register(ssd1680_get_driver()));
    ESP_ERROR_CHECK(ai_display_init());

    ESP_ERROR_CHECK(write_app_smoke_files());
    ESP_LOGI(TAG, "app scan: %d found", ai_app_scan());
    ESP_ERROR_CHECK(ai_app_launch("smoke"));

    int heartbeat_timer = ai_timer_create(5000, true);
    ESP_LOGI(TAG, "core smoke: heartbeat timer id=%d", heartbeat_timer);

    draw_full_test_pattern();
    vTaskDelay(pdMS_TO_TICKS(1500));
    partial_refresh_demo();
}
