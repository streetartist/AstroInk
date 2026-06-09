#pragma once
// ============================================================
// AstroInk board v1
//   MCU   : ESP32-S3-N16R8 (16MB flash, 8MB octal PSRAM)
//   Panel : 2.13" 212x104 B/W e-paper, SSD1680-compatible, SPI, partial-refresh
// ============================================================

// ---------- Display geometry ----------
// Waveshare 2.13" V2 (epd2in13_V2). Marketed as 212x104, but the SSD1680
// controller framebuffer is natively 122 (source/X) x 250 (gate/Y); the
// visible 104x212 area sits inside it. We drive the native buffer here;
// landscape 212x104 rotation is layered on top later (P0b, via LVGL).
#define BOARD_EPD_WIDTH        122   // source lines (X), 16 bytes/row
#define BOARD_EPD_HEIGHT       250   // gate lines   (Y)

// 1 = framebuffer/panel bit "1" means WHITE (SSD1680 BW default).
// If your panel shows inverted black/white, set this to 0.
#define BOARD_EPD_BIT_WHITE    1

// ============================================================
//  GPIOs taken from the PortableAnki board schematic (SCH_Schematic1,
//  2026-06-09), e-paper section on sheet P3 "墨水屏驱动".
//  The bare SSD1680 panel's charge pump (GDR/RESE -> L1/Q4/diodes ->
//  PREVGH/PREVGL) is on-board and self-driven by the panel, so the MCU
//  only provides the 4-wire SPI + DC/RST/BUSY below.
//  DC/RST/BUSY verified against pin order (IO35-37 skipped: octal PSRAM).
//  TODO(verify): MOSI/CLK/CS read from the PDF render — confirm IO17/18/8
//  against the EDA netlist before trusting a blank screen as a wiring bug.
// ============================================================
#define BOARD_EPD_SPI_HOST     SPI2_HOST

#define BOARD_EPD_PIN_SCLK     18   // CLK  / SCK
#define BOARD_EPD_PIN_MOSI     17   // DIN  / MOSI / SDA
#define BOARD_EPD_PIN_CS       8    // CS
#define BOARD_EPD_PIN_DC       38   // DC   (data/command)
#define BOARD_EPD_PIN_RST      39   // RST  (reset)
#define BOARD_EPD_PIN_BUSY     40   // BUSY (active HIGH on SSD1680, input)

#define BOARD_EPD_SPI_CLOCK_HZ (10 * 1000 * 1000)  // 10 MHz (safe; raise after bring-up)

// ============================================================
//  microSD card — wired 4-bit SDMMC (CARD2/TF-01A on schematic sheet P2;
//  all four data lines routed + 10k pull-ups -> use SDMMC 4-bit, not SPI).
//  ESP32-S3 SDMMC slot pins are GPIO-matrix flexible.
// ============================================================
#define BOARD_SD_PIN_CLK       12   // SD_CLK
#define BOARD_SD_PIN_CMD       13   // SD_CMD
#define BOARD_SD_PIN_D0        11   // SD_D0
#define BOARD_SD_PIN_D1        10   // SD_D1
#define BOARD_SD_PIN_D2        21   // SD_D2
#define BOARD_SD_PIN_D3        14   // SD_D3
#define BOARD_SD_PIN_CD        2    // card detect (active low). Currently unused
                                    // by the mount (robustness); wire-verified.
#define BOARD_SD_BUS_WIDTH     4
