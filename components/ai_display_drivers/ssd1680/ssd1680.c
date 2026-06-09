// SSD1680 B/W e-paper driver for AstroInk.
// Ported from Waveshare's official epd2in13_V2 sequence (the panel this board
// uses): MCU-loaded LUTs via 0x32, full refresh 0xC7, partial refresh 0x0C.
// The V2 panel does NOT work with the generic OTP differential update.

#include "ssd1680.h"
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "ssd1680";

// A white framebuffer byte, in panel-native bit order.
#define WHITE_BYTE   (BOARD_EPD_BIT_WHITE ? 0xFF : 0x00)
#define BIT_IS_WHITE (BOARD_EPD_BIT_WHITE ? true : false)

// Refresh-control (0x22) opcodes for this panel.
#define UPD_FULL     0xC7   // full update sequence
#define UPD_PARTIAL  0x0C   // partial (custom-LUT) update sequence
#define UPD_LOAD_LUT 0xC0   // load temperature + LUT only (no display)

// Official V2 waveform LUTs (70 bytes loaded via 0x32, then 6 tail params).
static const uint8_t lut_full_update[] = {
    0x80,0x60,0x40,0x00,0x00,0x00,0x00,   // LUT0 BB
    0x10,0x60,0x20,0x00,0x00,0x00,0x00,   // LUT1 BW
    0x80,0x60,0x40,0x00,0x00,0x00,0x00,   // LUT2 WB
    0x10,0x60,0x20,0x00,0x00,0x00,0x00,   // LUT3 WW
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT4 VCOM
    0x03,0x03,0x00,0x00,0x02,             // TP0
    0x09,0x09,0x00,0x00,0x02,             // TP1
    0x03,0x03,0x00,0x00,0x02,             // TP2
    0x00,0x00,0x00,0x00,0x00,             // TP3
    0x00,0x00,0x00,0x00,0x00,             // TP4
    0x00,0x00,0x00,0x00,0x00,             // TP5
    0x00,0x00,0x00,0x00,0x00,             // TP6
    0x15,0x41,0xA8,0x32,0x30,0x0A,        // tail: gate/source volts, dummy, gate time
};

static const uint8_t lut_partial_update[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT0 BB (no drive)
    0x80,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT1 BW
    0x40,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT2 WB
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT3 WW (no drive)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,   // LUT4 VCOM
    0x0A,0x00,0x00,0x00,0x00,             // TP0
    0x00,0x00,0x00,0x00,0x00,             // TP1
    0x00,0x00,0x00,0x00,0x00,             // TP2
    0x00,0x00,0x00,0x00,0x00,             // TP3
    0x00,0x00,0x00,0x00,0x00,             // TP4
    0x00,0x00,0x00,0x00,0x00,             // TP5
    0x00,0x00,0x00,0x00,0x00,             // TP6
    0x15,0x41,0xA8,0x32,0x30,0x0A,
};

typedef enum { MODE_NONE, MODE_FULL, MODE_PARTIAL } epd_mode_t;

static spi_device_handle_t s_spi;
static uint8_t s_fb[SSD1680_FB_SIZE];
static epd_mode_t s_mode = MODE_NONE;

// ---------------- low level ----------------

static void epd_delay(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void epd_wait_busy(void)
{
    // BUSY is active HIGH on SSD1680 (HIGH = busy).
    int guard = 0;
    while (gpio_get_level(BOARD_EPD_PIN_BUSY)) {
        epd_delay(2);
        if (++guard > 5000) { // ~10s timeout
            ESP_LOGW(TAG, "BUSY timeout");
            return;
        }
    }
}

static void epd_cmd(uint8_t cmd)
{
    gpio_set_level(BOARD_EPD_PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void epd_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(BOARD_EPD_PIN_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void epd_data1(uint8_t b) { epd_data(&b, 1); }

static void epd_reset(void)
{
    gpio_set_level(BOARD_EPD_PIN_RST, 1); epd_delay(200);
    gpio_set_level(BOARD_EPD_PIN_RST, 0); epd_delay(10);
    gpio_set_level(BOARD_EPD_PIN_RST, 1); epd_delay(200);
}

// X is byte-addressed (8 px/byte); Y is line-addressed. We drive both axes
// incrementing (data entry 0x03) to keep windowed writes simple.
static void epd_set_window(int x0, int y0, int x1, int y1)
{
    epd_cmd(0x44); // RAM X start/end (byte address)
    epd_data1(x0 / 8);
    epd_data1(x1 / 8);
    epd_cmd(0x45); // RAM Y start/end
    epd_data1(y0 & 0xFF); epd_data1((y0 >> 8) & 0xFF);
    epd_data1(y1 & 0xFF); epd_data1((y1 >> 8) & 0xFF);
}

static void epd_set_cursor(int x, int y)
{
    epd_cmd(0x4E); epd_data1(x / 8);
    epd_cmd(0x4F); epd_data1(y & 0xFF); epd_data1((y >> 8) & 0xFF);
}

static void epd_load_lut(const uint8_t *lut)
{
    epd_cmd(0x32);
    epd_data(lut, 70);
}

// Full-refresh mode: hardware reset + full panel config + full LUT.
static void epd_init_full(void)
{
    epd_reset();
    epd_wait_busy();
    epd_cmd(0x12);                 // SW reset
    epd_wait_busy();

    epd_cmd(0x74); epd_data1(0x54); // set analog block control
    epd_cmd(0x7E); epd_data1(0x3B); // set digital block control

    epd_cmd(0x01);                 // Driver output control: (HEIGHT-1) gates
    epd_data1((SSD1680_HEIGHT - 1) & 0xFF);
    epd_data1(((SSD1680_HEIGHT - 1) >> 8) & 0xFF);
    epd_data1(0x00);

    epd_cmd(0x11); epd_data1(0x03); // data entry: X+, Y+

    epd_set_window(0, 0, SSD1680_WIDTH - 1, SSD1680_HEIGHT - 1);

    epd_cmd(0x3C); epd_data1(0x03); // border waveform (full)
    epd_cmd(0x2C); epd_data1(0x55); // VCOM voltage

    epd_cmd(0x03); epd_data1(lut_full_update[70]);                      // gate driving voltage
    epd_cmd(0x04); epd_data1(lut_full_update[71]);                      // source driving voltage
                   epd_data1(lut_full_update[72]); epd_data1(lut_full_update[73]);
    epd_cmd(0x3A); epd_data1(lut_full_update[74]);                      // dummy line
    epd_cmd(0x3B); epd_data1(lut_full_update[75]);                      // gate time

    epd_load_lut(lut_full_update);

    epd_set_cursor(0, 0);
    epd_wait_busy();
}

// Partial-refresh mode: assumes a full init already configured the panel;
// only swaps in the partial LUT + VCOM + display option, then loads it.
static void epd_init_partial(void)
{
    epd_cmd(0x2C); epd_data1(0x26); // VCOM for partial
    epd_wait_busy();

    epd_load_lut(lut_partial_update);

    epd_cmd(0x37);                  // display option (per official V2)
    epd_data1(0x00); epd_data1(0x00); epd_data1(0x00); epd_data1(0x00);
    epd_data1(0x40); epd_data1(0x00); epd_data1(0x00);

    epd_cmd(0x22); epd_data1(UPD_LOAD_LUT);
    epd_cmd(0x20);
    epd_wait_busy();

    epd_cmd(0x3C); epd_data1(0x01); // border waveform (partial)
}

// Push the whole framebuffer to a RAM bank (0x24 = new, 0x26 = base).
static void epd_write_full(uint8_t ram_cmd)
{
    epd_set_window(0, 0, SSD1680_WIDTH - 1, SSD1680_HEIGHT - 1);
    epd_set_cursor(0, 0);
    epd_cmd(ram_cmd);
    epd_data(s_fb, SSD1680_FB_SIZE);
}

// ---------------- public API ----------------

esp_err_t ssd1680_init(void)
{
    gpio_config_t out = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_EPD_PIN_DC) |
                        (1ULL << BOARD_EPD_PIN_RST),
    };
    gpio_config(&out);

    gpio_config_t in = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_EPD_PIN_BUSY),
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&in);

    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_EPD_PIN_SCLK,
        .mosi_io_num = BOARD_EPD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SSD1680_FB_SIZE + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = BOARD_EPD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = BOARD_EPD_PIN_CS,
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(BOARD_EPD_SPI_HOST, &dev, &s_spi));

    ssd1680_clear(false); // white
    epd_init_full();
    s_mode = MODE_FULL;
    ESP_LOGI(TAG, "init ok (%dx%d, %d bytes fb)",
             SSD1680_WIDTH, SSD1680_HEIGHT, SSD1680_FB_SIZE);
    return ESP_OK;
}

uint8_t *ssd1680_framebuffer(void) { return s_fb; }

void ssd1680_clear(bool black)
{
    memset(s_fb, black ? (uint8_t)~WHITE_BYTE : WHITE_BYTE, SSD1680_FB_SIZE);
}

void ssd1680_draw_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= SSD1680_WIDTH || y < 0 || y >= SSD1680_HEIGHT) return;
    uint8_t mask = 0x80 >> (x & 7);
    int idx = y * SSD1680_ROW_BYTES + (x >> 3);
    bool want_white = (black ? !BIT_IS_WHITE : BIT_IS_WHITE);
    if (want_white) s_fb[idx] |= mask;
    else            s_fb[idx] &= ~mask;
}

void ssd1680_fill_rect(int x, int y, int w, int h, bool black)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ssd1680_draw_pixel(x + i, y + j, black);
}

void ssd1680_refresh_full(void)
{
    // Re-init full mode every time: this reloads the full LUT and doubles as
    // the ghosting clear (matches the official Init(FULL)+Display flow).
    epd_init_full();
    s_mode = MODE_FULL;

    epd_write_full(0x24);                  // new image
    epd_write_full(0x26);                  // seed base image (for later partials)

    epd_cmd(0x22); epd_data1(UPD_FULL);
    epd_cmd(0x20);
    epd_wait_busy();
}

// Region is auto byte-aligned in X. Uses the V2 custom partial LUT (0x0C).
// The partial LUT is differential, so we keep base RAM (0x26) in sync to
// minimise animation ghosting between updates.
void ssd1680_refresh_partial(int x, int y, int w, int h)
{
    if (s_mode != MODE_PARTIAL) {
        epd_init_partial();
        // Seed the differential baseline with the current framebuffer.
        epd_write_full(0x24);
        epd_write_full(0x26);
        epd_cmd(0x22); epd_data1(UPD_FULL);
        epd_cmd(0x20);
        epd_wait_busy();
        s_mode = MODE_PARTIAL;
    }

    int x0 = x & ~7;
    int x1 = ((x + w + 7) & ~7) - 1;
    int y0 = y;
    int y1 = y + h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= SSD1680_WIDTH)  x1 = SSD1680_WIDTH - 1;
    if (y1 >= SSD1680_HEIGHT) y1 = SSD1680_HEIGHT - 1;
    if (x1 < x0 || y1 < y0) return;

    int xb0 = x0 / 8;
    int rowbytes = (x1 / 8) - xb0 + 1;

    // Write new region to BW RAM (0x24).
    epd_set_window(x0, y0, x1, y1);
    epd_set_cursor(x0, y0);
    epd_cmd(0x24);
    for (int row = y0; row <= y1; row++)
        epd_data(&s_fb[row * SSD1680_ROW_BYTES + xb0], rowbytes);

    epd_cmd(0x22); epd_data1(UPD_PARTIAL);
    epd_cmd(0x20);
    epd_wait_busy();

    // Keep the base RAM (0x26) in sync so the next diff is correct.
    epd_set_window(x0, y0, x1, y1);
    epd_set_cursor(x0, y0);
    epd_cmd(0x26);
    for (int row = y0; row <= y1; row++)
        epd_data(&s_fb[row * SSD1680_ROW_BYTES + xb0], rowbytes);
}

void ssd1680_sleep(void)
{
    epd_cmd(0x10);
    epd_data1(0x01);
    epd_delay(100);
    s_mode = MODE_NONE;
}

void ssd1680_wake(void)
{
    // SPI bus/GPIO are still configured from ssd1680_init(); just bring the
    // controller back up (hardware reset + full LUT) so it can refresh again.
    epd_init_full();
    s_mode = MODE_FULL;
}
