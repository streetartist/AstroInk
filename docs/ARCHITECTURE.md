# AstroInk —— ESP32-S3 墨水屏 OS 架构设计方案

> 版本 v0.1 · 2026-06-06
> 一个面向 ESP32-S3 + 电子墨水屏的轻量级应用操作系统，支持用 **JavaScript / Lua / Python** 编写 App，统一 UI（LVGL），支持多分辨率墨水屏，具备文件系统与应用（"进程"）管理。
> 逐模块的落地实施方案(函数清单/易错点/验收标准)见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)。

---

## 1. 项目目标

| 能力 | 目标 |
|------|------|
| 多语言 App | JS（mquickjs）、Lua（Lua 5.4）、Python（PikaPython） |
| UI | LVGL v9 单色（1bpp），墨水屏友好 |
| 多屏 | 通过 Display HAL 抽象支持多种分辨率/驱动 IC |
| 存储 | 系统区 LittleFS + 用户/App 区 SD 卡 FATFS，统一 VFS |
| 应用管理 | 单前台 App 模型 + 后台服务 + 内存配额 + 看门狗 |
| 分发 | App 以文件夹 + manifest 形式安装到 SD |

---

## 2. 现实约束（设计前提）

ESP32-S3 = 双核 Xtensa LX7 @240MHz + 512KB 内部 SRAM + 外接 PSRAM/Flash，运行 **FreeRTOS / ESP-IDF**。由此确立四条硬约束：

1. **无 MMU、无真进程隔离**。"进程管理"= FreeRTOS task + 应用生命周期 + 内存配额 + watchdog；隔离靠 VM 沙箱，不是硬件。
2. **内存是第一瓶颈**。内部 SRAM 仅 512KB，framebuffer + VM 堆放不下 → **硬性要求：目标板带 8MB octal PSRAM**，framebuffer 与 VM 堆全部分配在 PSRAM。
3. **同一时刻只运行一个 App VM**。墨水屏天然单前台全屏，内存账本 = `OS Shell + LVGL + 单个 VM`。多语言因此可行——按当前 App 语言**懒实例化**对应 VM，退出即销毁。
4. **墨水屏刷新慢且特殊**。全刷 ~1–4s、局刷 ~0.3s、1bit（部分屏 4/16 级灰阶）。UI 必须围绕"少动画、批量重绘、全刷/局刷分明"设计。

---

## 3. 总体架构

**核心设计原则：一套 C System API，多语言薄绑定。**
绝不为每种语言重写 UI/FS/系统逻辑；每个 VM 只写一层把统一 C API 暴露给该语言的 shim。

```
┌──────────────────────────────────────────────────────────┐
│  Apps        app.js   │   app.lua   │   app.py            │
├──────────────────────────────────────────────────────────┤
│  Language    mqjs_bind │  lua_bind   │  pika_bind          │  ← 薄 shim
│  Bindings    (.c)      │  (.c)       │  (.c)               │
├──────────────────────────────────────────────────────────┤
│  ★ AstroInk System API（统一 C 接口，唯一真相源）★         │
│    ai_ui_*  ai_fs_*  ai_app_*  ai_input_*  ai_sys_*  ...   │
├──────────────────────────────────────────────────────────┤
│  Services    App Manager │ UI(LVGL) │ VFS │ Event Bus(IPC) │
├──────────────────────────────────────────────────────────┤
│  HAL         Display │ Input │ Storage │ Power │ RTC        │
├──────────────────────────────────────────────────────────┤
│  ESP-IDF / FreeRTOS                                         │
└──────────────────────────────────────────────────────────┘
```

> **检验标准**：每新增一种语言，工作量应仅为"一层 binding"。若不止，说明 System API 抽象有漏洞。

---

## 4. 子系统设计

### 4.1 HAL（硬件抽象层）

每个 HAL 模块定义一个 C 接口结构体（函数指针表），具体硬件实现挂载其上，实现可插拔。

**Display HAL**（多屏关键）：
```c
typedef struct {
    uint16_t width, height;
    uint8_t  bpp;              // 1 / 2 / 4
    bool     supports_partial; // 是否支持局刷
    void (*init)(void);
    void (*flush_full)(const uint8_t *buf);            // 全刷
    void (*flush_partial)(int x,int y,int w,int h,const uint8_t*); // 局刷
    void (*sleep)(void);
    void (*wakeup)(void);
} ai_display_drv_t;
```
- 新增一块屏 = 新增一个 `ai_display_drv_t` 实现（SSD1680 / SSD1681 / IL3820 / GDEY… ）。
- 驱动起步参考 **tuanpmt/esp_epaper**（原生 LVGL9 + 1bit BW + 差分局刷）。
- 板级配置（屏型号、引脚、SPI）放 `boards/<board>.h`，编译期选板。

**Input HAL**：按键 / 触摸（部分屏带 L58 等触控）/ 旋钮，统一为事件投递到 Event Bus。
**Storage HAL**：内部 Flash 分区 + SD（SDMMC 或 SPI）。
**Power HAL**：light/deep sleep、电量读取、唤醒源。

### 4.2 UI 子系统（LVGL v9）

- `color depth = 1`，LVGL 自动转单色；framebuffer 在 PSRAM。
- LVGL flush callback → Display HAL；OS 决定全刷/局刷策略（脏区累计、N 次局刷后强制一次全刷消残影）。
- 提供 OS 级单色主题（高对比、无渐变、墨水屏专用控件样式）。
- **多分辨率**：App 用 LVGL flex/grid 做分辨率无关布局；System API 暴露 `ai_ui_screen_size()` 供 App 适配。
- ⚠️ **风险点：LVGL9 局刷为公认难点，Phase 0 必须先做原型验证。**

### 4.3 文件系统 / VFS

| 区域 | 介质 | 文件系统 | 用途 |
|------|------|----------|------|
| 系统区 | 内部 Flash | **LittleFS** | OS 配置、内置 App、字体 |
| 用户区 | SD 卡 | **FATFS** | 用户 App、文档、资源 |

- 上层 **VFS** 统一路径：`/system/...`、`/sd/...`。
- App 只通过 `ai_fs_*` 访问，看不到底层差异。LittleFS 掉电安全、磨损均衡（不用已弃用的 SPIFFS）。

### 4.4 System API（地基，需最先定稿）

统一 C 接口，所有语言绑定都指向它。命名前缀 `ai_`。草案：

```c
// ---- UI / 绘制 ----
ai_obj_t* ai_ui_label(ai_obj_t* parent, const char* text);
ai_obj_t* ai_ui_button(ai_obj_t* parent, const char* text);
void      ai_ui_set_pos(ai_obj_t*, int x, int y);
void      ai_ui_on_event(ai_obj_t*, ai_event_cb cb, void* user);
void      ai_ui_screen_size(int* w, int* h);
void      ai_ui_refresh(ai_refresh_mode_t mode); // FULL / PARTIAL / AUTO

// ---- 文件 ----
int     ai_fs_open(const char* path, const char* mode);
int     ai_fs_read(int fd, void* buf, int len);
int     ai_fs_write(int fd, const void* buf, int len);
void    ai_fs_close(int fd);
bool    ai_fs_exists(const char* path);
int     ai_fs_listdir(const char* path, ai_dirent_t* out, int max);

// ---- 应用 / 系统 ----
void    ai_app_exit(int code);
void    ai_app_set_title(const char* title);
int64_t ai_sys_millis(void);
void    ai_sys_sleep(int ms);
void    ai_sys_battery(int* percent, bool* charging);

// ---- 定时 / 事件 ----
ai_timer_t* ai_timer_create(int interval_ms, bool repeat, ai_cb cb, void*);
void        ai_input_on(ai_input_type_t, ai_cb cb, void*);

// ---- 持久化 KV（App 私有配置）----
void ai_kv_set(const char* k, const char* v);
int  ai_kv_get(const char* k, char* out, int max);
```
> 这套接口是项目脊梁，Phase 1 必须冻结。后续每种语言只是把它"翻译"过去。

### 4.5 多语言运行时

| 语言 | 引擎 | 接入方式 | 备注 |
|------|------|----------|------|
| JS | **mquickjs**（已在 `mquickjs/`） | ESP-IDF component，`mqjs_bind.c` 注册 native 函数 | ES5 子集，10KB RAM 级，**主语言** |
| Lua | **georgik/esp-idf-component-lua**（Lua 5.4） | 官方 component，`lua_bind.c` 注册 C 闭包 | 每 state 数十 KB |
| Python | **PikaPython**（pikasTech） | 作为库嵌入，Pre-compiler 绑定 C 函数生成 `pika_bind` | Python3 子集；与多 VM 共存哲学一致 |

**VM 抽象接口**（App Manager 通过它统一驱动各 VM）：
```c
typedef struct {
    const char* lang;                 // "js" / "lua" / "py"
    void* (*create)(size_t heap_limit);
    int   (*run_file)(void* vm, const char* path);
    void  (*pump)(void* vm);          // 事件循环 tick（处理 timer/input 回调）
    void  (*destroy)(void* vm);
} ai_runtime_t;
```
- App 启动：App Manager 据 manifest 选 runtime → `create(配额)` → `run_file(入口)` → 循环 `pump()` → 退出 `destroy()`。
- 内存配额：每个 VM 带独立 heap limit，超限即杀，保护系统。

### 4.6 应用 / "进程"管理

- **单前台模型**：App Manager 持有当前前台 App，在专属 FreeRTOS task 跑其 VM。
- **后台服务**：时钟、电量、网络等作为常驻 task。
- **生命周期**：`install → launch → foreground → suspend → exit`。墨水屏切 App 通常整屏重绘。
- **保护**：每 App 内存配额 + task watchdog；崩溃只杀该 App，回到 Launcher。
- **IPC / Event Bus**：FreeRTOS queue + 发布订阅，承载输入事件、系统广播（电量低、休眠）、App 间消息。

### 4.7 App 打包格式

App = SD 上一个文件夹：
```
/sd/apps/clock/
  manifest.json
  main.js                 # 或 main.lua / main.py
  assets/
    icon.bin
```
`manifest.json`：
```json
{
  "id": "com.astroink.clock",
  "name": "Clock",
  "version": "1.0.0",
  "lang": "js",
  "entry": "main.js",
  "icon": "assets/icon.bin",
  "orientation": "auto",
  "min_os": "0.1.0",
  "permissions": ["fs", "timer"]
}
```
Launcher 扫描 `/sd/apps/*/manifest.json` → 生成图标列表 → 选中后据 `lang` 实例化对应 VM。

### 4.8 电源管理

- 空闲 → light sleep；长时间 → deep sleep（墨水屏断电保图）。
- 唤醒源：按键 / RTC 定时 / 触摸。
- 唤醒后据策略全刷消残影。

---

## 5. 工程目录结构（建议）

```
AstroInk/
├─ CMakeLists.txt
├─ sdkconfig.defaults
├─ boards/                 # 板级/屏配置（编译期选板）
│   └─ generic_ssd1680.h
├─ components/
│   ├─ ai_hal/             # Display/Input/Storage/Power HAL
│   ├─ ai_display_drivers/ # 各墨水屏驱动实现
│   ├─ ai_system_api/      # ★ 统一 System API（地基）
│   ├─ ai_ui/              # LVGL 集成 + 单色主题 + 刷新策略
│   ├─ ai_vfs/             # LittleFS + FATFS + 统一 VFS
│   ├─ ai_appmgr/          # App Manager / 生命周期 / 事件总线
│   ├─ ai_runtime_js/      # mquickjs + mqjs_bind
│   ├─ ai_runtime_lua/     # lua + lua_bind
│   └─ ai_runtime_py/      # pikapython + pika_bind
├─ mquickjs/               # 已存在：JS 引擎源码
├─ main/                   # 启动、Launcher Shell
├─ apps/                   # 内置示例 App（js/lua/py 各一）
└─ docs/
    └─ ARCHITECTURE.md     # 本文件
```

---

## 6. 开发路线图（强制顺序）

> **铁律：不要一上来铺三语言。** 先用 JS 打通端到端，固化 System API，再以"加一层 binding"的方式增量接入 Lua、Python。

| Phase | 目标 | 完成标志（DoD） |
|-------|------|------|
| **P0 闭环** | HAL(显示) → LVGL 单色出画面 → LittleFS → App Manager → mquickjs 跑 `main.js` 画 "Hello" | 一块真实墨水屏上由 JS 脚本画出文字 |
| **P0.5 风险验证** | LVGL9 墨水屏**局刷**原型 | 局刷可用且无明显残影策略 |
| **P1 固化 API** | System API（ui/fs/input/timer/kv）定稿冻结 | 头文件冻结 + JS 全量绑定 |
| **P2 Lua** | 接入 Lua 5.4，复用同一 C API | 同一示例 App 用 Lua 重写跑通 |
| **P3 Python** | 接入 PikaPython | 同一示例 App 用 Python 跑通 |
| **P4 平台化** | 局刷优化、电源管理、多屏适配、SD 分发、Launcher 完善 | 可安装/卸载 SD 上的第三方 App |

---

## 7. 关键风险与对策

| 风险 | 等级 | 对策 |
|------|------|------|
| 内存不足（framebuffer+VM） | 高 | 强制 PSRAM；单 VM 懒实例化；每 VM 配额 |
| LVGL9 墨水屏局刷难 | 高 | P0.5 提前做原型，参考 esp_epaper |
| PikaPython ESP32 成熟度 | 中 | 放最后（P3）；不行则降级 MicroPython embed |
| System API 抽象漏（被迫每语言改逻辑） | 中 | P1 冻结前用 JS 充分打磨 |
| App 崩溃拖垮系统 | 中 | VM 沙箱 + 配额 + watchdog + 只杀单 App |
| 多屏适配碎片化 | 低 | Display HAL 函数指针表 + 板级配置编译期选 |

---

## 8. 锁定硬件（v1）

| 项 | 规格 |
|----|------|
| MCU | ESP32-S3-**N16R8**（16MB QUAD flash + 8MB OCTAL PSRAM）|
| 墨水屏 | 2.13" **212×104**，B/W 双色，**SSD1680 兼容**，SPI，支持局刷，无触摸 |
| 无线 | 需 WiFi + BLE（BLE 用 NimBLE，P4 启用）|
| ESP-IDF | **v5.5.2**（`D:\esp\v5.5.2\esp-idf`）|

引脚配置见 `components/ai_board/board_astroink_v1.h`（**待用户确认实际接线**）。

## 9. 当前进度

- ✅ 架构方案（本文件）
- ✅ P0a 骨架：工程/构建配置（16MB+octal PSRAM）、板级头、SSD1680 驱动、bring-up 测试程序
  - 屏确认为 **Waveshare 2.13" V2**，驱动按官方 `epd2in13_V2` 序列重写（原生 122×250、自定义 LUT、刷新 0xC7/0x0C）
  - 引脚据 PortableAnki 原理图填入（DC=IO38/RST=IO39/BUSY=IO40，SPI=IO17/18/8 待 EDA 复核）
- ✅ P0b-HAL：**Display HAL**（`ai_hal` 组件，`ai_display_drv_t` 函数指针表 + 活动驱动注册表）+ SSD1680 适配器；main 已改为经 HAL 驱动屏
- ✅ **VFS 存储层**（`ai_vfs` 组件）：LittleFS→`/system`（"storage" 分区，≈12MB）、SD(SDMMC 4-bit)→`/sd`；SD 缺卡非致命。SD 引脚据原理图：CLK=IO12/CMD=IO13/D0=IO11/D1=IO10/D2=IO21/D3=IO14/CD=IO2。依赖 `joltwallet/littlefs`（managed）
- ✅ **System API（非 UI 子集）**（`ai_system_api` 组件）：`ai_fs_*`（POSIX 包装 VFS）、`ai_kv_*`（NVS 持久 KV）、`ai_sys_*`（millis/sleep；battery 待 ADC 标定）。main 启动有存储+API 冒烟自测（KV 启动计数 + /system 文件读写）
- ✅ **P0c JS 运行时**（`ai_runtime_js` 组件）：接入 mquickjs，`ai.*` 全局命名空间桥接 System API（log/millis/sleep/kvGet/kvSet/readFile/writeFile/exists/screenW/screenH）。VM 内存在 PSRAM。stdlib 经 host 工具从 `ai_js_stdlib.c` 生成 `generated/{mquickjs_atom.h,ai_stdlib.h}`（`-m32`，已提交；`tools/gen_stdlib.sh` 重生成）。**已用 host gcc 端到端验证** codegen + 引擎 + ai 命名空间 + JS_Eval 跑通
- ✅ **M3/M5/M6 雏形**：`ai_core` 事件队列、软件定时器、统一 `ai_loop_run()` 主循环、`ai_runtime_t` registry、最小 `ai_appmgr` manifest 扫描和文件启动路径已接入；main 启动写入并通过 App Manager 加载 `/system/apps/smoke/main.js`
- ⏳ P0a 真机显示验证：等屏幕实物（见 `docs/P0_BRINGUP.md`）。冻结 System API（含 UI 部分）属 P1
- ⬜ 后续：完整 App 生命周期、SD App 热扫描、LVGL UI、Lua/Python
- ⬜ P0b-UI LVGL v9 单色（接 HAL flush）　⬜ P0c mquickjs JS Hello　⬜ P1 冻结 System API　⬜ P2 Lua　⬜ P3 PikaPython　⬜ P4 平台化

> 参考：
> [tuanpmt/esp_epaper](https://github.com/tuanpmt/esp_epaper) ·
> [PikaPython](https://github.com/pikasTech/PikaPython) ·
> [georgik/esp-idf-component-lua](https://github.com/georgik/esp-idf-component-lua) ·
> [MicroPython ports/embed（备选）](https://github.com/micropython/micropython/blob/master/ports/embed/README.md)
