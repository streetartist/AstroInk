# AstroInk 完整实施方案(M0 → M11)

> 版本 v1.0 · 2026-06-10
> 本文是 [ARCHITECTURE.md](ARCHITECTURE.md) 的落地细化:把"完成整个项目"拆成可顺序执行的模块,
> 每个模块给出:目标、新增文件、要实现的函数、实现逻辑、易错点、注意事项、验收标准(DoD)。
> 架构原则(一套 C System API、单前台 VM、PSRAM 优先)不在此重复,以 ARCHITECTURE.md 为准。

---

## 0. 当前基线(2026-06-10)

已完成并经代码审查修复:

| 模块 | 状态 |
|------|------|
| 工程骨架 / 分区表 / sdkconfig(esp32s3 N16R8) | ✅(target 错配已修;`ai_vfs` 依赖名已改 `joltwallet__littlefs`) |
| Display HAL(`ai_hal`)+ SSD1680 驱动 | ✅(partial 模式切换双闪已修;init 错误传播已修) |
| VFS(`/system` LittleFS + `/sd` FATFS) | ✅ |
| System API 非 UI 子集(`ai_fs/ai_kv/ai_sys`) | ✅(新增 `ai_fs_size`、`ai_kv_get_len`) |
| JS 运行时(mquickjs)+ `ai.*` 绑定 | ✅(按文件大小分配、参数校验、writeFile 截断顺序已修) |
| 真机验证 | ⏳ 等烧录(M0) |

**模块总顺序(强制)**:

```
M0 真机验证 → M1 LVGL/ai_ui → M2 输入 HAL → M3 事件总线+定时器+OS主循环
→ M4 System API 冻结(P1) → M5 VM 抽象 → M6 App Manager → M7 Launcher
→ M8 Lua(P2) → M9 PikaPython(P3) → M10 电源管理 → M11 平台化(P4)
```

依赖关系:M1–M3 可部分并行,但 **M4(冻结)必须等 M1–M3 在 JS 路径上跑通打磨后进行**;
M5/M6 依赖 M4;M8/M9 依赖 M5;M10/M11 随时可插但建议靠后。

---

## 1. 全局约定(先读,所有模块共用)

### 1.1 线程模型:单 OS 任务(关键决策)

**全系统只有一个 `ui_task`(内部 RAM 栈,16KB 起步)承载:LVGL、当前 App 的 VM、所有 `ai_ui_*` 调用、事件分发。**

```
ui_task 主循环:
  1. 事件总线 drain(输入/定时器/系统事件 → 经全局 dispatch 进 VM)
  2. vm->pump()                  ← VM 内部事务(JS 目前为空操作)
  3. lv_timer_handler()          ← LVGL 渲染,flush_cb 内决定全刷/局刷
  4. vTaskDelay(5ms)             ← 或按 lv_timer_handler 返回值休眠
```

推论(写进每个人的肌肉记忆):
- **任何其他任务/ISR 不得直接调用 lv_\*、VM、ai_ui_\***;只能向事件总线 post。
- 因为单任务,LVGL 与 VM 之间**免锁**;这是本设计拿到的最大红利,不要轻易破坏。
- 墨水屏全刷 ~2s 的 BUSY 等待会阻塞主循环 → 接受(单前台、墨水屏交互节奏慢);
  P4 如确有需要再评估把"面板刷新"挪到独立低优先级任务(届时需加帧缓冲锁)。

### 1.2 错误处理约定

- System API:查询类返回 `int`(`<0` 错误)或 `bool`;动作类返回 `esp_err_t`;指针类失败返回 `NULL`。
- 绑定层翻译为各语言惯例:JS 抛 `TypeError`(参数错)/ 返回 `null`(资源不存在)/ 返回 `-1`(IO 失败)。
- **禁止静默吞错**:底层失败至少 `ESP_LOGW`。

### 1.3 内存预算(8MB PSRAM / 512KB SRAM)

| 项 | 位置 | 预算 |
|----|------|------|
| LVGL draw buffer(I1 局部条带 ×2) | PSRAM | ~2×(250×40/8+8) ≈ 2.6KB |
| native shadow framebuffer(122×250) | 内部 RAM(DMA 需要) | 4KB |
| LVGL 内部堆(LV_MEM) | PSRAM | 128KB |
| JS VM 堆(默认/上限) | PSRAM | 64KB / 512KB(manifest 可调) |
| Lua state | PSRAM | 128KB 配额 |
| PikaPython | PSRAM | ≥256KB 配额 |
| ui_task 栈 | 内部 RAM | 16KB |
| 字体缓存(P4 中文) | PSRAM | ≤1MB |

注意:**SPI DMA 缓冲必须在内部 RAM**——SSD1680 驱动的 `s_fb` 保持现状(静态内部 RAM),
LVGL 渲染缓冲在 PSRAM,flush 时旋转拷贝进 `s_fb`,顺便完成格式转换,不额外多一次拷贝。

### 1.4 立刻要做的决策:分区表是否留 OTA

当前 `partitions.csv` 只有 `factory`。**若未来要 OTA,必须现在改**(以后改分区表会清掉用户的
`/system` 数据)。建议:P0 真机验证通过后立即改为 `factory + ota_0 + otadata`(各 ~3MB,
storage 压到 ~9MB),代价最低的时机就是现在。不要 OTA 则明确写死结论,本段关闭。

---

## 2. M0 — 真机验证(P0a 收尾)

**目标**:在实物上确认 SPI 接线、全刷、局刷、极性、可视区域。

按 [P0_BRINGUP.md](P0_BRINGUP.md) 执行,额外补三项检查:

1. **可视区域偏移**:板头注释里"标称 212×104、控制器原生 122×250"存在矛盾可能
   (Waveshare 2.13 V2 正常是全部 122×250 可视)。看测试图案 3px 黑边框是否四边完整:
   - 四边完整 → 全可视,后续按 250×122 横屏布局;
   - 有边被裁 → 实测裁切量,在 `board_astroink_v1.h` 增加
     `BOARD_EPD_VISIBLE_X0/Y0/W/H`,M1 的旋转映射要把偏移算进去。
2. **局刷只闪一次**:每 20 帧 ghost-clear 后,动画恢复时屏幕应只闪一次全刷
   (本次审查修复点,顺带回归)。
3. **SPI 提速试验**:10MHz 稳定后试 20MHz(SSD1680 上限 20MHz 写),记录结论到板头注释。

**DoD**:测试图案四边完整(或偏移已记录)、局刷动画无残影、`BOARD_EPD_BIT_WHITE` 极性确认。

---

## 3. M1 — LVGL v9 单色 UI(`ai_ui` 组件)

**目标**:LVGL 渲染 → 旋转/格式转换 → Display HAL,带墨水屏刷新策略。**这是全项目技术风险最高的模块。**

### 3.1 新增文件

```
components/ai_ui/
  CMakeLists.txt          # REQUIRES lvgl ai_hal ai_board; idf_component.yml: lvgl/lvgl "^9.2"
  idf_component.yml
  ai_ui_port.c            # display 创建、flush_cb、tick、旋转拷贝
  ai_ui_refresh.c         # 全刷/局刷策略状态机
  ai_ui_theme.c           # 单色主题(高对比、无动画)
  include/ai_ui_port.h
```

### 3.2 要实现的函数

```c
// ai_ui_port.c
esp_err_t ai_ui_init(void);              // 创建 lv_display、注册 flush_cb、tick、主题
void      ai_ui_task_handler(void);      // 主循环里调:lv_timer_handler 包装

// 内部
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void rotate_copy_i1(const uint8_t *src /*LVGL I1, 含 stride*/,
                           const lv_area_t *area, uint8_t *dst_native_fb);

// ai_ui_refresh.c — 刷新策略(纯逻辑,可 host 单测)
typedef enum { AI_REFRESH_AUTO, AI_REFRESH_FULL, AI_REFRESH_PARTIAL } ai_refresh_mode_t;
void ai_refresh_mark_dirty(int x, int y, int w, int h);   // flush_cb 调用,累计脏区
void ai_refresh_commit(void);            // flush_is_last 时调用:决策并触发 HAL 刷新
void ai_refresh_force_full(void);        // App 切换/唤醒时调用
void ai_refresh_set_mode(ai_refresh_mode_t m); // 暴露给 System API
```

### 3.3 实现逻辑

- **显示配置**:`lv_display_create(250, 122)`(逻辑横屏);
  `lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1)`;
  `LV_DISPLAY_RENDER_MODE_PARTIAL` + 两个条带 draw buffer(250×40)。
- **flush_cb**:把 LVGL 给的 area 像素**旋转 90° 写入驱动的 native framebuffer**
  (`ssd1680_framebuffer()`,122×250 竖屏),同时 `ai_refresh_mark_dirty(旋转后坐标)`;
  `lv_display_flush_is_last(disp)` 为真时调 `ai_refresh_commit()`;最后必须
  `lv_display_flush_ready(disp)`。
- **旋转公式**(逻辑横屏 W=250,H=122 → native 122×250,二选一真机定):
  - 顺时针 90°:`nx = ly; ny = 249 - lx`
  - 逆时针 90°:`nx = 121 - ly; ny = lx`
- **刷新策略状态机**(`ai_refresh_commit`):
  ```
  if 强制全刷标志 || partial_count >= 20 || 脏区面积 > 全屏 60%:
      ai_display_flush_full(NULL); partial_count = 0
  elif 距上次刷新 < AI_REFRESH_MIN_INTERVAL_MS(默认 250ms):
      合并脏区,挂起到下个 commit(不丢更新,只延迟)
  else:
      ai_display_flush_partial(脏区bbox, NULL); partial_count++
  清空脏区
  ```
- **tick**:`lv_tick_set_cb(ai_sys_millis 包装)`,不要再开 esp_timer。
- **主题**:`lv_theme` 自定义:纯黑白、边框 1px、无渐变无阴影、
  `lv_obj_set_style_anim_duration(.., 0, ..)` 全局禁动画。

### 3.4 易错点(重点背诵)

1. **I1 调色板头**:LVGL9 的 I1 draw buffer **前 8 字节是调色板**(2 色 ARGB8888),
   像素数据从 `px_map + 8` 开始。直接当像素用会整屏花掉。
2. **stride 对齐**:LVGL draw buffer 每行按 `LV_DRAW_BUF_STRIDE_ALIGN` 对齐,
   行字节数 ≠ `(w+7)/8`。必须用 `lv_draw_buf_width_to_stride()`(或 area 宽度算出 stride)
   逐行寻址,绝不能假设紧密排列。
3. **I1 不支持 LVGL 软件旋转**:`lv_display_set_rotation` 对 I1 无效/未实现,
   旋转必须自己在 flush_cb 做(这就是 3.3 的旋转拷贝存在的原因)。
4. **bit 极性两层映射**:LVGL I1 的 1 = 调色板索引 1;SSD1680 的 1 = 白(`BOARD_EPD_BIT_WHITE`)。
   写一个真机校验步骤:画黑底白字,错了在 rotate_copy 里取反,不要去改驱动。
5. **flush_ready 漏调**:任何提前 return 路径漏掉 `lv_display_flush_ready` → LVGL 永久卡住。
6. **不要让 LVGL 自由刷新**:墨水屏没有 33ms 帧率概念。控制权在 `ai_refresh_commit`,
   LVGL 只负责把脏区画进 shadow FB。
7. **`lv_timer_handler` 必须与 VM 同任务**(见 1.1),否则后患无穷。

### 3.5 注意事项

- LVGL 通过 idf_component.yml 引入后,菜单配置用 `CONFIG_LV_*`;把关键项写进
  `sdkconfig.defaults`(LV_MEM 128KB 指向 PSRAM、禁用不需要的 widget 省 flash)。
- main.c 的 P0 演示循环此阶段退役,改为:init 链 → `lv_label "Hello AstroInk"` → 主循环。
- ⚠️ 验证项(写代码前先查当前 LVGL 9.x 文档):I1 支持的最低版本、
  `lv_display_set_color_format` 对 I1 的渲染路径、palette 头是否仍是 8 字节。

**DoD**:真机上 LVGL label/button 正常显示(横屏 250×122);连续点按钮 20 次只在
第 20 次触发全刷;脏区局刷肉眼无残影累积;host 单测覆盖刷新策略逻辑。

---

## 4. M2 — 输入 HAL(`ai_input`)

**目标**:物理按键 → 统一事件。

### 4.1 文件与函数

```
components/ai_input/{ai_input.c, include/ai_input.h}

typedef enum { AI_KEY_UP, AI_KEY_DOWN, AI_KEY_OK, AI_KEY_BACK } ai_key_t;   // 按板定义
typedef enum { AI_KEY_EV_DOWN, AI_KEY_EV_UP, AI_KEY_EV_LONG, AI_KEY_EV_REPEAT } ai_key_event_t;

esp_err_t ai_input_init(void);   // 按 board 头的按键表配 GPIO,启动扫描
```

### 4.2 实现逻辑与易错点

- **用 10ms 周期轮询扫描**(esp_timer 或独立小任务),不用 GPIO 中断:
  去抖简单可靠(连续 2 次采样一致才算变化),长按(>800ms)/连发(每 200ms)在扫描里判。
- 扫描上下文**只允许** `ai_event_post()`,严禁碰 LVGL/VM(见 1.1)。
- 板头补 `BOARD_KEY_*` 引脚定义;注意 IO0(boot)做按键时上电状态的副作用。
- LVGL 侧:注册 `lv_indev`(KEYPAD 类型)把按键喂给 LVGL 焦点系统;
  同一事件**同时**走 Event Bus 给 App——两条消费路径并存,在 M4 决定 App 是否能拦截。

**DoD**:串口能看到按键 DOWN/UP/LONG 事件;LVGL 按钮能被按键焦点操作。

---

## 5. M3 — 事件总线 + 软件定时器 + OS 主循环(`ai_core`)

**目标**:打通"事件 → VM 回调"的唯一通道,建立 1.1 的主循环。

### 5.1 文件与函数

```
components/ai_core/{ai_event.c, ai_timer.c, ai_loop.c, include/...}

// 事件总线(FreeRTOS queue 包装,深度 32)
typedef struct { uint16_t type; int32_t a, b; } ai_event_t;
// type: AI_EV_KEY / AI_EV_TIMER / AI_EV_SYS_BATTERY / AI_EV_APP_KILL ...
esp_err_t ai_event_post(const ai_event_t *ev);            // 任意任务可调
esp_err_t ai_event_post_isr(const ai_event_t *ev);        // ISR 变体
bool      ai_event_poll(ai_event_t *out, int timeout_ms); // 仅 ui_task

// 软件定时器(数组实现,容量 16,App 退出全清)
int  ai_timer_create(int interval_ms, bool repeat);   // 返回 timer id(>0),失败 -1
void ai_timer_cancel(int id);
void ai_timer_tick(void);        // 主循环每轮调:到期 → post AI_EV_TIMER{a=id}

// 主循环
void ai_loop_run(void);          // app_main 最终落点,永不返回
```

### 5.2 实现逻辑与易错点

- **定时器绝不直接回调**:到期只 post 事件,由主循环统一派发进 VM。
  esp_timer 回调跑在它自己的任务/中断上下文,直接进 VM 是经典翻车点。
- timer id 用 **index + generation 递增**编码(`id = gen<<8 | idx`),
  防止"App A 的过期 id 杀掉复用同槽位的 App B 定时器"。
- 队列满:`ai_event_post` 返回错误并 `ESP_LOGW`(事件风暴可见化),不阻塞。
- `ai_sys_sleep` 保留但文档明确:它阻塞整个 OS 循环,App 应使用定时器。

**DoD**:host 单测覆盖定时器(到期/取消/generation 复用);真机上按键事件经总线进入主循环日志。

---

## 6. M4 — System API 冻结(P1,项目脊梁)

**目标**:UI/输入/定时器进入 `ai_system_api`,头文件冻结,JS 全量绑定。
**冻结后改动需走"破坏性变更评审"——往后每种语言、每个 App 都压在这套签名上。**

### 6.1 UI 句柄模型(防 GC/悬垂的关键设计)

脚本永远拿到的是 **`uint32_t` 句柄,不是指针**:

```c
typedef uint32_t ai_obj_t;            // 0 = 无效;低 8 位 index,高位 generation
// C 侧表:components/ai_system_api/ai_ui.c
#define AI_UI_MAX_OBJS 128
typedef struct { lv_obj_t *obj; uint16_t gen; bool used; } ui_slot_t;
```

- 每次 App 退出:`lv_obj_clean(lv_screen_active())` + 全表 `gen++` 清空 → 旧句柄全部自然失效。
- 每个 API 入口 `slot_resolve(handle)` 校验 index/gen,不合法返回错误,**绝不解引用悬垂指针**。

### 6.2 冻结函数清单(草案,实现时定稿)

```c
// ---- UI(全部仅限 ui_task 调用)----
ai_obj_t ai_ui_screen(void);                                  // 当前 App 根容器
ai_obj_t ai_ui_label(ai_obj_t parent, const char *text);
ai_obj_t ai_ui_button(ai_obj_t parent, const char *text);
ai_obj_t ai_ui_image(ai_obj_t parent, const char *src_path);  // /system|/sd 的 .bin
int  ai_ui_set_text(ai_obj_t, const char *text);
int  ai_ui_set_pos(ai_obj_t, int x, int y);
int  ai_ui_set_size(ai_obj_t, int w, int h);
int  ai_ui_align(ai_obj_t, int align /*AI_ALIGN_**/, int dx, int dy);
int  ai_ui_delete(ai_obj_t);
int  ai_ui_on_event(ai_obj_t, int ev_mask);    // 事件经 dispatch 进脚本,不存 C 回调
void ai_ui_screen_size(int *w, int *h);
void ai_ui_refresh(ai_refresh_mode_t);          // FULL=立即全刷; AUTO=恢复策略

// ---- 输入 / 定时器(已在 M2/M3,此处入册)----
int  ai_timer_create(int interval_ms, bool repeat);
void ai_timer_cancel(int id);

// ---- 应用 ----
void ai_app_exit(int code);                     // 由 M6 实现,先占签名

// ---- 既有(已实现,冻结时复核)----
ai_fs_open/read/write/close/exists/size/listdir
ai_kv_set/get/get_len/erase
ai_sys_millis/sleep/battery
```

### 6.3 脚本回调:全局 dispatch 模式(强制,所有语言统一)

**C 层不持有任何脚本值。** 事件进 VM 的唯一路径:

```
C:  ai_event_t{type,a,b}
 → 绑定层: global = JS_GetGlobalObject(ctx)
           fn = JS_GetPropertyStr(ctx, global, "__ai_dispatch")
           JS_PushArg(type); JS_PushArg(a); JS_PushArg(b); JS_Call(ctx, 3)
           JS_HasException? → 打日志 + 标记 App 异常计数
JS stdlib(ai_js_stdlib.c 内置):
   var __handlers = {};  // "timer:3" -> fn, "ui:17:click" -> fn, "key" -> fn
   function __ai_dispatch(type, a, b) { ...查表调用... }
   ai.setTimeout/ai.onKey/obj.onClick = 往 __handlers 注册 + 调对应 C 函数
```

为什么:mquickjs 的 `JSValue` 是堆句柄,**GC 压缩可能移动对象,C 侧长期持值 = 悬垂**。
每次按名查 `__ai_dispatch` 的开销可忽略(每事件一次)。Lua/Python 沿用同一约定,
System API 才不会被迫为"回调存储"开洞——这正是架构检验标准的落点。

### 6.4 KV 的 NVS 硬限制(易错点)

- **NVS key ≤ 15 字符、namespace ≤ 15 字符**(含义:`ai.kvSet("user_preferred_language",..)` 直接失败)。
- 方案:绑定层校验 key 长度,超限返回错误并日志;M6 起每 App 一个 namespace:
  `"a_" + fnv1a32(app_id) 的 8 位 hex`(共 10 字符,留余量),App 间天然隔离。
- 大值(>1KB)建议引导走文件;NVS 单条 string ≤ ~4000B 是协议上限。

### 6.5 DoD

头文件加 `// FROZEN v1.0` 标记;JS 绑定全量 + stdlib 重生成(`tools/gen_stdlib.sh`);
示例 App(时钟:label + 1s 定时器 + 按键退出)在真机跑 24h 无内存增长(`esp_get_free_heap_size` 周期打点)。

---

## 7. M5 — VM 抽象(`ai_runtime_t`)+ JS 运行时重构

**目标**:App Manager 不认识具体语言,只认统一接口。

```c
// components/ai_core/include/ai_runtime.h
typedef struct ai_runtime {
    const char *lang;                          // "js" / "lua" / "py"
    void *(*create)(size_t heap_limit);
    int   (*run_file)(void *vm, const char *path);
    int   (*dispatch)(void *vm, const ai_event_t *ev);  // 进脚本 __ai_dispatch
    void  (*pump)(void *vm);                   // VM 内部事务(JS 暂为空)
    void  (*request_stop)(void *vm);           // 置停止标志(可异步)
    void  (*destroy)(void *vm);
} ai_runtime_t;

const ai_runtime_t *ai_runtime_find(const char *lang);   // 注册表
```

### 实现要点与易错点

- `ai_runtime_js` 改造成此接口的第一个实现;`dispatch` 即 6.3 的调用序列。
- **卡死保护**:`create` 时 `JS_SetInterruptHandler(ctx, interrupt_cb)`;
  回调里只做两件事:检查 `stop_requested` 标志、检查本次 dispatch 起始时间是否超
  预算(默认 2000ms)。超时返回非 0 → mquickjs 中断执行。
  注意:interrupt 回调被高频调用,**禁止**在里面打日志/拿锁。
- `request_stop` 只置标志;真正销毁永远由主循环在 dispatch 返回后做
  (**绝不在脚本栈活跃时 destroy VM**)。
- JS 异常处理统一收口:dispatch/run_file 返回非 0 时取 `JS_GetException` 打完整信息,
  连续 3 次异常 → 上报 App Manager 杀 App(M6)。

**DoD**:故意写 `while(true){}` 的 App 在 2s 内被中断并回到稳定状态,系统不重启。

---

## 8. M6 — App Manager(`ai_appmgr`)

**目标**:manifest 扫描、生命周期、崩溃恢复。

### 8.1 文件与函数

```
components/ai_appmgr/{ai_appmgr.c, ai_manifest.c, include/...}

typedef struct {
    char id[32]; char name[32]; char version[16];
    char lang[8]; char entry[64]; char dir[96];
    uint32_t heap_kb;          // 默认 64,上限 512
    uint32_t perms;            // bit: AI_PERM_FS_SD / AI_PERM_FS_SHARED / ...
} ai_app_info_t;

int  ai_app_scan(void);                          // /system/apps + /sd/apps → 内部表
int  ai_app_count(void);
const ai_app_info_t *ai_app_get(int idx);
esp_err_t ai_app_launch(const char *id);         // 排队请求,主循环执行真正切换
void ai_app_exit(int code);                      // 当前 App 主动退出(同样是排队)
const ai_app_info_t *ai_app_current(void);

// ai_manifest.c(纯函数,host 可测)
int ai_manifest_parse(const char *json, size_t len, ai_app_info_t *out);
```

### 8.2 生命周期(状态机)

```
LAUNCH 请求(排队,绝不在 dispatch 栈里立即切换)
 → 主循环检测到待切换:
   1. 旧 VM: request_stop → dispatch 返回后 destroy
   2. ai_timer 全清、输入订阅清空、UI 句柄表 gen++ 失效、lv_obj_clean(screen)
   3. ai_refresh_force_full()
   4. runtime = ai_runtime_find(info->lang); vm = create(heap_kb*1024)
   5. run_file(dir/entry) — 入口脚本执行完即"挂机",靠事件驱动
   6. 状态 RUNNING
崩溃路径:连续异常/卡死中断/堆耗尽 → 同 1–3 → 自动 launch("launcher")
```

### 8.3 易错点与注意事项

- **manifest 解析用 IDF 自带 cJSON**(`json` 组件):`cJSON_Parse` 后所有取值判 NULL、
  给默认值;**必须 `cJSON_Delete`**;文件读入用 `ai_fs_size` 精确分配(≤4KB 上限)。
- 切换动作必须**排队到主循环顶部执行**:`ai_app_exit` 是脚本经绑定调进来的,
  此刻 VM 栈是活的,立即 destroy = use-after-free。这是本模块第一翻车点。
- 权限检查落点在 `ai_fs_open`:App 自身目录 RW;`/sd` 其余 R(W 需 `fs_sd` 权限);
  `/system` 只读且仅 `/system/fonts`、`/system/shared`(白名单)。
  实现为前缀比较,注意 **路径必须先规范化拒绝 `..`**(`strstr(path,"..")` 直接拒绝即可,VFS 无 symlink)。
- App id 字符集校验 `[a-z0-9_.]`,它会进 NVS namespace 推导(6.4)和日志。
- 看门狗:`esp_task_wdt_add(ui_task)`,周期 8s;主循环每轮 feed;
  全刷 BUSY 等待内部已 `vTaskDelay`,不会饿死 IDLE,但 **wdt 周期必须 > 最大全刷时长**。

**DoD**:`/sd/apps/clock` 拔卡重插后可重新扫描;clock 死循环 → 2s 中断 → 自动回 Launcher;
异常 App 反复崩不拖垮系统(连崩 3 次进入"禁用"标记)。

---

## 9. M7 — Launcher(内置 JS App,吃自家狗粮)

**目标**:用且仅用公开的 `ai.*` API 写出第一个真实 App;它暴露的所有不便=API 设计缺陷,
反馈回 M4(冻结前最后窗口)。

- 位置:`/system/apps/launcher/{manifest.json, main.js}`;
  构建期从 `apps/launcher/` 打包进 LittleFS 镜像(`littlefs_create_partition_image`,
  joltwallet 组件自带 CMake 函数——**比首启动时用代码写入文件可靠**)。
- 功能:`ai.fs.listdir("/sd/apps")` + manifest 名称列表 → label 列表 + 焦点高亮 →
  OK 键 `ai.app.launch(id)`;BACK 无操作;空 SD 显示提示。
- 字体:P1 阶段英文(内置 Montserrat 14);中文字体属 M11。
- **易错点**:Launcher 自己也是被 App Manager 管的普通 App——崩溃恢复目标也是它,
  务必保证它本身极简、无第三方依赖;`launcher` id 硬编码为恢复目标。

**DoD**:从 Launcher 启动 clock → clock 退出 → 自动回 Launcher,循环 50 次无泄漏。

---

## 10. M8 — Lua 运行时(P2)

**目标**:验证"新语言 = 一层绑定"。**若此阶段被迫改 System API,即架构验收失败,先回炉 M4。**

- 引入:直接 vendor Lua 5.4 源码到 `components/ai_runtime_lua/lua/`
  (Lua 无 OS 依赖,十几个 .c;比第三方 component 可控,版本锁死)。
- `lua_bind.c`:
  ```c
  static const luaL_Reg ai_funcs[] = { {"log",l_log}, {"millis",l_millis}, ... };
  // create: lua_newstate(psram_alloc_with_quota, &vm->quota) — 自定义分配器同时做
  //         PSRAM 优先 + 配额(超配额返回 NULL,Lua 自身抛 "not enough memory")
  // dispatch: lua_getglobal(L,"__ai_dispatch") + lua_pcall(L,3,0,errhandler)
  // 卡死保护: lua_sethook(L, hook, LUA_MASKCOUNT, 100000) — hook 里查 stop/超时
  ```
- **易错点**:
  1. 每个 lua_CFunction 严格栈平衡,返回值数量与 `return n` 一致;
  2. 一切进入 Lua 的调用必须 `lua_pcall` 包裹(裸 `lua_call` 遇错 longjmp 穿透 C 栈);
  3. `luaL_openlibs` 不要全开:禁 `os`/`io`(绕过 ai_fs 权限),只开 base/table/string/math;
  4. Lua 整数/浮点双类型,fd/handle 用 `lua_Integer` 收发。
- 同一个 clock App 用 Lua 重写为 `main.lua`,manifest `"lang":"lua"`。

**DoD**:clock.lua 与 clock.js 行为一致;System API 头文件零改动(检验标准达成)。

---

## 11. M9 — PikaPython(P3)

- 接入 PikaPython 为 component;绑定经其 Pre-compiler 从注释生成(`pika_bind`)。
- 配额 ≥256KB;`dispatch` 进 `__ai_dispatch` 同约定。
- **风险已知**:ESP-IDF 集成成熟度中等;**时间盒 2 周**,不行果断降级
  MicroPython embed port(架构文档既定备选),VM 抽象保证了替换面只有一个目录。
- **易错点**:PikaPython 的内存池初始化参数与 PSRAM 配合、其线程假设(确认全部 API
  可在 ui_task 单任务驱动,不能则在其 port 层垫薄适配)。

**DoD**:clock.py 跑通;三语言 README 矩阵更新。

---

## 12. M10 — 电源管理(`ai_power`)

**目标**:墨水屏设备的核心卖点——月级待机。

```c
esp_err_t ai_power_init(void);
void ai_power_kick(void);            // 任何用户活动调用,重置空闲计时
// 策略(主循环空闲时评估):
//   idle > 30s  : 面板 sleep(已有 ai_display_sleep)+ CPU light sleep(按键唤醒)
//   idle > 10min: deep sleep(RTC/按键唤醒;唤醒=重启,走正常 boot + force full)
void ai_sys_battery(int *percent, bool *charging);   // 落地 TODO:ADC 标定
```

- 电池:ADC oneshot + `esp_adc_cal` 曲线校准;分压系数按原理图;
  锂电压→百分比查表(4.2→100,3.3→0,非线性 10 段);TP4056 `CHRG` 读充电状态。
- **易错点**:
  1. **BUSY 期间禁止任何 sleep**(刷新中断电=花屏甚至伤屏)——`ai_power` 评估前查显示状态;
  2. 墨水屏**刷完即睡**:长时间维持驱动电压伤面板,空闲即 `ai_display_sleep()`
     (驱动已支持 MODE_NONE 自动恢复路径,本次审查已修);
  3. light sleep 后 `esp_timer` 与 LVGL tick 出现跳变——唤醒后调 `lv_tick` 校正或直接全刷重置;
  4. USB-CDC 日志在 light sleep 下会断流,调试期提供 `CONFIG` 开关关掉睡眠;
  5. deep sleep 唤醒是**重启**:App 状态不保留,文档向 App 开发者明示(用 ai_kv 自存)。

**DoD**:空闲 30s 实测电流 < 2mA(light)/ < 200µA(deep,板上其他芯片允许范围内);按键唤醒后 UI 全刷恢复。

---

## 13. M11 — 平台化(P4 收尾)

按优先级:

1. **SD App 安装/卸载**:Launcher 长按出菜单;"安装"=校验 manifest+拷贝,"卸载"=递归删目录
   + 清该 App NVS namespace。易错点:递归删除的深度与 FAT 长文件名配置
   (`CONFIG_FATFS_LFN_HEAP`,默认 8.3 短名会毁中文/长目录名——**现在就开 LFN**)。
2. **中文字体**:`lv_font_conv` 离线生成 bin(常用 3500 字 + 16px)→ `/system/fonts/`
   → `lv_binfont_load("A:/system/fonts/...")`。需先实现 LVGL FS 驱动挂到 ai_fs
   (`lv_fs_drv_t`,字母 'A')。易错点:bin 字体常驻 PSRAM(~500KB),App 配额外记账。
3. **第二块屏验证多屏抽象**:选一块 SSD1681 1.54"(200×200),只新增
   `components/ai_display_drivers/ssd1681/` + 新 board 头;**全系统其余零改动**为验收标准。
   旋转/可视区差异逼出 Display HAL 的遗漏(如有,补 HAL,不准在 ai_ui 写 if(panel))。
4. **BLE(NimBLE)文件传输**(可选):手机推 App 到 /sd;安全模型从简(配对 PIN)。
5. **OTA**(若 1.4 决策为做):`esp_https_ota` + 双分区;版本号进 manifest `min_os` 校验链。

---

## 14. 测试与工程纪律

- **host 单测优先**:纯逻辑模块(刷新策略、manifest 解析、定时器、句柄表)写成无 IDF 依赖
  的 .c,`tests/host/` 下用 gcc + assert 跑(stdlib codegen 已有 host 工具链先例)。
  目标:这些模块 80% 覆盖。
- **真机 smoke 清单**:每模块 DoD 汇总成 `docs/SMOKE_CHECKLIST.md`,烧录后 10 分钟过一遍。
- **内存回归**:主循环每 60s `ESP_LOGI` 一次 `free_heap / min_free_heap / largest_block`
  (internal 与 PSRAM 分开);任何模块合并前跑 1h 看趋势。
- **git**:每模块一个 `feat/mX-*` 分支;提交信息按既定 conventional commits;
  HAL/System API 头文件改动必须在提交信息里写 `API-CHANGE:` 前缀(冻结后即评审触发器)。
- **文档同步**:每模块完成更新 ARCHITECTURE.md §9 进度 + README 状态;本文件对应章节打 ✅。

## 15. 易错点速查表(全文汇总)

| # | 易错点 | 出处 |
|---|--------|------|
| 1 | LVGL I1 buffer 前 8 字节是调色板,像素从 +8 开始 | M1 |
| 2 | LVGL stride 对齐 ≠ (w+7)/8,逐行按 stride 拷贝 | M1 |
| 3 | I1 无软件旋转,flush_cb 自己转 90° | M1 |
| 4 | 漏调 lv_display_flush_ready → 整个 UI 卡死 | M1 |
| 5 | 黑白极性 = LVGL 调色板 × 面板 BIT_WHITE 两层映射 | M1 |
| 6 | ISR/扫描任务只准 post 事件,禁碰 LVGL/VM | M2/M3 |
| 7 | 定时器回调不直接进 VM,一律走事件队列 | M3 |
| 8 | timer/UI 句柄要带 generation,防跨 App 串号 | M3/M4 |
| 9 | C 侧绝不长期持有 JSValue(GC 压缩会移动),回调走全局 __ai_dispatch | M4 |
| 10 | NVS key/namespace ≤15 字符;App KV 用哈希 namespace | M4 |
| 11 | 不在脚本栈活跃时 destroy VM;exit/launch 一律排队到主循环 | M5/M6 |
| 12 | interrupt/hook 回调高频执行,内部禁日志禁锁 | M5/M8 |
| 13 | cJSON 判 NULL + cJSON_Delete;路径含 ".." 直接拒 | M6 |
| 14 | lua_pcall 包裹一切,Lua 错误是 longjmp;禁开 os/io 库 | M8 |
| 15 | BUSY 刷新中禁 sleep;墨水屏空闲即面板深睡 | M10 |
| 16 | light sleep 后 LVGL tick 跳变需校正 | M10 |
| 17 | FATFS 现在就开 LFN,否则长文件名 App 目录全毁 | M11 |
| 18 | SPI DMA 缓冲必须内部 RAM;PSRAM 缓冲不能直接给 DMA | 全局 |
| 19 | 分区表要不要 OTA,P0 后立刻定,晚了清用户数据 | 全局 |
| 20 | wdt 周期必须大于最长全刷阻塞时间 | M6 |
