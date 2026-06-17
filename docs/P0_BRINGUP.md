# P0a — 显示 Bring-up 指南

目标：在真实的 2.13" 212×104 黑白墨水屏上验证 **SPI 驱动 + 全刷 + 局刷**。
（局刷是全项目最高风险项，故在 P0a 就提前验证。）

## 0. 先确认引脚 ⚠️

当前代码以 `components/ai_board/board_astroink_v1.h` 为准。默认引脚来自 PortableAnki 板级配置：

```c
#define BOARD_EPD_PIN_SCLK     18   // CLK / SCK
#define BOARD_EPD_PIN_MOSI     17   // DIN / MOSI / SDA
#define BOARD_EPD_PIN_CS       8    // CS
#define BOARD_EPD_PIN_DC       38   // DC
#define BOARD_EPD_PIN_RST      39   // RST
#define BOARD_EPD_PIN_BUSY     40   // BUSY, active HIGH on SSD1680
```

如果你的实物接线不同，先改 `components/ai_board/board_astroink_v1.h` 再烧录。引脚错通常只会点不亮或 `BUSY timeout`，不会损坏屏幕。

> 注意：`BOARD_EPD_PIN_SCLK/MOSI/CS = IO18/IO17/IO8` 仍标注为需 EDA netlist 复核；`DC/RST/BUSY = IO38/IO39/IO40` 已按当前板级头使用。

## 1. 构建 & 烧录

PowerShell：

```powershell
# 1) 加载 ESP-IDF 环境
& D:\esp\v5.5.2\esp-idf\export.ps1

# 2) 进入项目
cd D:\Project\AstroInk

# 3) 设目标芯片（首次）
idf.py set-target esp32s3

# 4) 构建 + 烧录 + 看日志（把 COMx 换成你的串口）
idf.py -p COM5 flash monitor
```

退出 monitor：`Ctrl+]`。

## 2. 预期现象

启动后串口打印：
```
I astroink: AstroInk P0a boot
I astroink: VFS: system=1 sd=0
I astroink: selftest: boot #...
I ai_appmgr: found app 'smoke' (js) entry=/system/apps/smoke/main.js
I ai_appmgr: scan complete: 1 app(s)
I ai_appmgr: launch 'smoke' (js) entry=/system/apps/smoke/main.js
App smoke JS on AstroInk
I ssd1680: init ok (122x250, 4000 bytes fb)
I astroink: full test pattern drawn
I astroink: event: timer id=256 repeat=1
app event timer 256 1 count=1
I astroink: event: timer id=256 repeat=1
app event timer 256 1 count=2
I ai_appmgr: app 'smoke' exit code=0
I ai_appmgr: destroy app 'smoke'
```

> 说明：这块 Waveshare 2.13" V2 屏标称 212×104，但控制器原生 framebuffer 是
> **122×250（竖屏）**，可视区 104×212 嵌在其中。P0a 先按原生竖屏点亮验证；
> 横屏 212×104 的旋转留到 P0b 接 LVGL 时做。所以测试图案是**竖着**的。

屏幕**全刷**出测试图案：3px 黑边框、顶部黑条带白缺口、对角线、横条纹带、中心嵌套矩形（黑白交替的同心方块）。

约 1.5s 后进入**局刷**循环：底部一个黑方块每 0.8s 向右移动一格（只刷底部那条带，应明显比全刷快、无整屏闪烁）。每 20 帧做一次全刷消残影。串口同步打印 `partial frame N`。

局刷使用官方 epd2in13_V2 的自定义 LUT（命令 `0x32`），全刷 `0x22=0xC7`、局刷 `0x22=0x0C`。

## 3. 排查

| 现象 | 可能原因 |
|------|----------|
| 全黑/全白无图案 | 引脚错；CS/DC/RST 接反；SPI host 选错 |
| 图案黑白反相 | 把 `BOARD_EPD_BIT_WHITE` 改成 `0` |
| 图案上下/左右镜像 | 数据进入方向（`0x11`）或 `0x01` 扫描方向需调整 |
| `BUSY timeout` 警告 | BUSY 引脚错，或面板未上电 |
| 局刷不更新/重影严重 | 当前已写入官方 V2 partial LUT（`0x32`）；优先检查 BUSY、电源、面板版本是否真为 V2/兼容款 |

> **局刷说明**：当前 `ssd1680.c` 按 Waveshare `epd2in13_V2` 序列实现，使用 MCU 下发的 full/partial LUT（命令 `0x32`）。代码不再使用通用 OTP 差分局刷。

## 4. 通过后

P0a 通过后，下一步是 **P0b-UI**：叠加 LVGL v9 单色，把驱动接到 LVGL flush callback。

当前代码已经接入 mquickjs、`ai.*` System API、runtime registry 和最小 App Manager 冒烟测试；启动时会写入 `/system/apps/smoke/{manifest.json,main.js}`，扫描 manifest 后加载 `/system/apps/smoke/main.js`。后续要把它扩展为完整生命周期、SD App 扫描和 Launcher。
