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
#include "ai_system_api.h"
#include "ai_runtime_js.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "astroink";

#define W SSD1680_WIDTH
#define H SSD1680_HEIGHT

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

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

// Animate a box across a strip near the bottom using partial refresh only.
static void partial_refresh_demo(void)
{
    const int strip_h = imin(32, imax(20, H / 3));
    const int strip_y = H - strip_h - 6;
    const int box_w   = imin(30, imax(16, W / 7));
    const int box_h   = strip_h - 8;
    int frame = 0;

    while (1) {
        int x = (frame * 8) % (W - box_w);

        // Redraw the whole strip: white background + black box at new x.
        ssd1680_fill_rect(0, strip_y, W, strip_h, false);
        ssd1680_fill_rect(x, strip_y + 4, box_w, box_h, true);

        ai_display_flush_partial(0, strip_y, W, strip_h, NULL);
        ESP_LOGI(TAG, "partial frame %d (x=%d)", frame, x);

        frame++;

        // Every 20 partial updates, do a full refresh to clear ghosting.
        if (frame % 20 == 0) {
            draw_full_test_pattern();
            ESP_LOGI(TAG, "ghost-clear full refresh");
        }
        vTaskDelay(pdMS_TO_TICKS(800));
    }
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

// P0c: run a JS snippet through mquickjs, exercising the ai.* System API.
static void js_hello(void)
{
    static const char *SRC =
        "ai.log('Hello from JS on AstroInk!');\n"
        "ai.log('screen:', ai.screenW() + 'x' + ai.screenH());\n"
        "var n = ai.kvGet('js_runs');\n"
        "n = (n ? parseInt(n) : 0) + 1;\n"
        "ai.kvSet('js_runs', '' + n);\n"
        "ai.log('js run #' + n + ' at ' + ai.millis() + 'ms');\n"
        "ai.writeFile('/system/hello.txt', 'written by JS, run ' + n);\n"
        "ai.log('readback:', ai.readFile('/system/hello.txt'));\n";

    ai_js_vm *vm = ai_js_create(0);
    if (!vm) { ESP_LOGE(TAG, "JS VM create failed"); return; }
    if (ai_js_eval(vm, SRC, strlen(SRC), "hello.js") != 0)
        ESP_LOGW(TAG, "JS hello script failed");
    ai_js_destroy(vm);
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
    storage_selftest();

    // Register the panel with the Display HAL, then drive it through ai_display.
    ESP_ERROR_CHECK(ai_display_register(ssd1680_get_driver()));
    ESP_ERROR_CHECK(ai_display_init());

    // P0c: JavaScript end-to-end via mquickjs + ai.* bindings.
    js_hello();

    draw_full_test_pattern();
    vTaskDelay(pdMS_TO_TICKS(1500));
    partial_refresh_demo();
}
