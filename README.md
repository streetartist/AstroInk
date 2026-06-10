# AstroInk

AstroInk is an ESP32-S3 e-paper firmware project that is growing into a lightweight app OS for small black-and-white e-paper devices. The current tree is the P0 bring-up firmware: it mounts storage, initializes the System API, runs a JavaScript snippet through mquickjs, and drives a Waveshare 2.13" V2 / SSD1680-compatible panel through a display HAL.

The long-term direction is documented in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): one C System API, thin language bindings, a single foreground app VM, LVGL-based UI, and pluggable display/input/storage/power HALs. The module-by-module implementation plan (functions, pitfalls, DoD per milestone) lives in [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md).

## Current Status

Implemented:

- ESP-IDF project skeleton for ESP32-S3-N16R8.
- Custom 16 MB partition table with a large LittleFS-backed `storage` partition mounted at `/system`.
- Optional microSD mount at `/sd` through SDMMC 4-bit + FATFS.
- Display HAL (`ai_display_drv_t`) and SSD1680-compatible e-paper driver.
- UI-independent System API:
  - `ai_fs_*` for VFS file access.
  - `ai_kv_*` for NVS-backed persistent key/value strings.
  - `ai_sys_*` for millis/sleep/battery placeholder.
- JavaScript runtime component using `mquickjs`.
- `ai.*` JavaScript namespace for log, time, KV, file, and screen-size access.
- P0 display bring-up demo with full refresh plus partial-refresh animation.

Not implemented yet:

- LVGL UI layer.
- App Manager / manifest-based app lifecycle.
- Lua and Python runtimes.
- Power management and battery ADC calibration.

## Hardware Target

The current board configuration is in [components/ai_board/board_astroink_v1.h](components/ai_board/board_astroink_v1.h).

- MCU: ESP32-S3-N16R8, 16 MB flash + 8 MB octal PSRAM.
- Display: Waveshare 2.13" V2 style black/white e-paper panel, SSD1680-compatible.
- Panel native framebuffer: `122x250` 1 bpp. The advertised visible area is `212x104`; landscape rotation is planned for the LVGL layer.
- microSD: 4-bit SDMMC.
- ESP-IDF: v5.5.2.

Default e-paper pins:

| Signal | GPIO |
| --- | ---: |
| SCLK | 18 |
| MOSI | 17 |
| CS | 8 |
| DC | 38 |
| RST | 39 |
| BUSY | 40 |

Default SD pins:

| Signal | GPIO |
| --- | ---: |
| CLK | 12 |
| CMD | 13 |
| D0 | 11 |
| D1 | 10 |
| D2 | 21 |
| D3 | 14 |
| CD | 2 |

If your hardware wiring differs, update `components/ai_board/board_astroink_v1.h` before flashing.

## Repository Layout

```text
AstroInk/
+-- CMakeLists.txt
+-- partitions.csv
+-- sdkconfig.defaults
+-- main/
|   +-- main.c
+-- components/
|   +-- ai_board/              # Board pins and hardware constants
|   +-- ai_hal/                # Display HAL registry and wrappers
|   +-- ai_display_drivers/
|   |   +-- ssd1680/            # Current e-paper driver
|   +-- ai_vfs/                # /system LittleFS and /sd FATFS mounts
|   +-- ai_system_api/         # File, KV, and system APIs
|   +-- ai_runtime_js/         # mquickjs integration and ai.* bindings
+-- mquickjs/                 # Embedded JavaScript engine source
+-- docs/
    +-- ARCHITECTURE.md
    +-- IMPLEMENTATION_PLAN.md
    +-- P0_BRINGUP.md
```

## Build And Flash

Use an ESP-IDF PowerShell environment. The project is configured for ESP-IDF v5.5.2.

```powershell
& D:\esp\v5.5.2\esp-idf\export.ps1
cd D:\Project\AstroInk
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the serial port for your board. Exit the monitor with `Ctrl+]`.

The first build may download the managed LittleFS dependency declared in [components/ai_vfs/idf_component.yml](components/ai_vfs/idf_component.yml).

## Expected Runtime Behavior

On boot, `main/app_main()` performs the current P0 self-test sequence:

1. Mount `/system` from the internal LittleFS partition.
2. Try to mount `/sd`; missing SD is non-fatal.
3. Initialize the System API and update a persistent boot counter in NVS.
4. Write and read back `/system/boot.txt`.
5. Register and initialize the SSD1680 display driver through the Display HAL.
6. Create a mquickjs VM and run an embedded JavaScript hello script.
7. Draw a full-screen e-paper test pattern.
8. Run a partial-refresh animation near the bottom of the screen.

Typical log lines include:

```text
I astroink: AstroInk P0a boot
I astroink: VFS: system=1 sd=0
I astroink: selftest: boot #...
I ai_js: Hello from JS on AstroInk!
I astroink: full test pattern drawn
I astroink: partial frame ...
```

See [docs/P0_BRINGUP.md](docs/P0_BRINGUP.md) for display bring-up details and troubleshooting.

## JavaScript API

The current JS runtime exposes an `ai` global namespace backed by the C System API:

```js
ai.log("Hello from JS on AstroInk!");
ai.log("screen:", ai.screenW() + "x" + ai.screenH());

var n = ai.kvGet("js_runs");
n = (n ? parseInt(n) : 0) + 1;
ai.kvSet("js_runs", "" + n);

ai.writeFile("/system/hello.txt", "written by JS, run " + n);
ai.log("readback:", ai.readFile("/system/hello.txt"));
```

Available bindings currently include:

- `ai.log(...)`
- `ai.millis()`
- `ai.sleep(ms)`
- `ai.kvGet(key)`
- `ai.kvSet(key, value)`
- `ai.readFile(path)`
- `ai.writeFile(path, data)`
- `ai.exists(path)`
- `ai.screenW()`
- `ai.screenH()`

The VM heap defaults to 64 KB and is allocated from PSRAM.

## Storage

`partitions.csv` defines a 16 MB flash layout:

- `nvs`: persistent key/value storage.
- `factory`: application image.
- `storage`: large internal data partition mounted as LittleFS at `/system`.

The SD card, when present, is mounted at `/sd`. Higher layers should use `ai_fs_*` or JS file helpers rather than calling LittleFS/FATFS directly.

## Regenerating JS Stdlib Headers

The JS component contains generated headers:

- `components/ai_runtime_js/generated/mquickjs_atom.h`
- `components/ai_runtime_js/generated/ai_stdlib.h`

They are generated from `components/ai_runtime_js/ai_js_stdlib.c` by:

```sh
components/ai_runtime_js/tools/gen_stdlib.sh
```

Regenerate them whenever the JS stdlib or `ai.*` surface changes.

## Development Roadmap

Near-term order:

1. Finish real-hardware P0 display verification.
2. Add LVGL v9 monochrome UI on top of the Display HAL.
3. Freeze the C System API including UI/input/timer pieces.
4. Add App Manager, manifest loading, lifecycle, and event bus.
5. Add Lua and Python runtimes as thin bindings over the same C API.
6. Add power management, launcher, SD app distribution, and multi-panel support.

The architecture notes intentionally treat JS as the first full path. Lua and Python should be added only after the System API is stable enough that each new language is mostly binding code.
