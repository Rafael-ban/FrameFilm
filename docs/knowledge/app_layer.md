# FrameFilm App 层设计文档（草稿）

> 状态：**设计中**（本文档用于累积讨论结论，随迭代更新）
> 目标：为 3.7" 屏适配新特性引入 **app 层**，统一承载图片 / 模板 / 时钟 / 动图等应用能力。
> 关联固件：`firmware/frame_film_ark`（通行证版，**单机型**；app 框架主线）；`firmware/frame_film`（冰箱贴三机型，旧机型维护线，app 框架不再跟进新功能）。

---

## 1. 背景与目标

3.7"（720×480，JD7601，EPD_PANEL_ID 0x02）屏引入了新特性：8bpp 索引色 spectra（ColorQual / ColorFast）、MonoFast 差分局刷、film v2 多帧格式。这些能力让"屏幕"不再只是显示单张静态照片，而是可以承载多种应用：

| 应用 | 说明 |
|---|---|
| 图片显示 | 本地 TF 卡 / 蓝牙传输 / WiFi 传输 三种来源 |
| 模板显示 | 通用实时推送显示：蓝牙/WiFi 推送 film 落盘后装载缓存并整屏显示（天气/日历等内容共用） |
| 时钟 | 纯设备端页面显示功能 |
| 动图 | 与图片类似，但显示 film v2 多帧图（持续循环播放） |

现有固件 `app_main` → `film_sys_init` → `film_hal_init` + `film_service_init`，**没有独立的 app 层**。输入回调被硬绑定到 `service_film`（UP→prev / DOWN→next / LONG→clear），缺乏"应用调度 / 页面 / 菜单"这一层。本设计补齐该层。

---

## 2. 核心设计决策（已确认）

1. **（历史决策）不独立固件，保持单固件三机型**。
   - app 层加在 service 之上，不触碰 `film_hal` / `film_sys`，与机型宏隔离零耦合。
   - 独立固件会破坏 AGENTS.md 的跨端一致性（BLE 常量 / 颜色编码 / 协议文档），双份维护。
   - 同一份 app 代码运行在所有机型，靠能力位（而非编译宏）适配 3.7 屏新特性并优雅降级。
   - **现状（已修订）**：app 框架现已拆出**单机型固件 `frame_film_ark`（通行证版）**承载主线，`frame_film` 保留同构代码但不再跟进 app 框架新功能；跨端一致性仍按 AGENTS.md 要求在三端同步。

2. **app 切换是 3.7 屏专属能力**。
   - 非 3.7 屏固件保持现状（单一图片墙，无切换入口）。
   - 通过屏参数能力位 / 编译期开关判定，连接端（BLE 0x42 ScreenResolution）可据此判断是否支持切换。

3. **main 入口统一为 `film_app_init()`**。
   - 替代早期方案的 `app_manager_init()`。
   - 依赖方向：`app → service → hal → sys`（单向，不可逆）。

4. **动图做成独立 app**（其他版本无此功能）。
   - 独立文件夹存放，持续循环播放，切换时只切换动图文件（与图片切换语义不同）。

5. **首次开机默认进入图片（照片墙）**。
   - `app_manager` 启动时默认 app 为 `APP_ID_IMAGE`；上电流程见 §8.1（`FULL` 模式固定走 开机画面 → 主菜单；其余模式用 `service_param_app_current_get()` 恢复上次 app，非法/未注册则回落图片）。

---

## 3. 分层架构

```
┌─────────────────────────────────────────────┐
│  APP 层  (新)   components/film_app          │  ← 本次新增
│  film_app_init(调度) + 具体 app  + 渲染抽象        │
├─────────────────────────────────────────────┤
│  服务层  components/film_service              │
│  service_film / service_file / service_ble   │
│  service_wifi / service_ota / service_param  │
├─────────────────────────────────────────────┤
│  硬件层  components/film_hal                  │
│  hal_epd / hal_input / hal_sd               │
├─────────────────────────────────────────────┤
│  系统层  components/film_sys                  │
└─────────────────────────────────────────────┘
```

---

## 4. 组件文件布局（film_app）

顶层就四个目录：`inc/`（对外公共头）、`core/`（框架核心）、`pages/`（框架页面）、`apps/`（内容 app）；
头文件与源文件同目录。

```
components/film_app/
  inc/                      ── 对外公共头（其他组件只应包含这里）
    app_init.h              ── 统一 app 层入口 film_app_init()
    app_interface.h         ── 统一 app 接口（app_id_t / app_entry_t / app_event_t）
  core/                     ── 框架核心
    app_init.c              ── 注册各 app + 上电决策（BOOT PAGE / START APP）
    app_manager.{h,c}       ── 调度器：页面注册、切换状态机、事件路由、输入转发
    app_render.{h,c}        ── 渲染/显示抽象（能力位）
    app_shell.{h,c}         ── UI 页公共外壳（状态栏 + 居中时间 + 提示行 + 周期 tick）
    app_boot_cfg.{h,c}      ── 开机行为参数（持久化在 service_param 的 app7 槽位）
  pages/                    ── 框架自带页面（不是内容 app）
    app_boot.{h,c}          ── 开机画面（自检遥测 + 进度条）
    app_menu.{h,c}          ── 主菜单轮播
    app_settings.{h,c}      ── 系统设置（设备信息 / 参数 / app 参数）
    app_sleep.{h,c}         ── 休眠卡 + 入睡流程
  apps/                     ── 内容 app：一个 app 一个文件夹
    image/app_image.{h,c}
    template/app_template.{h,c}
    clock/app_clock.{h,c} + font_clock_hero.{h,c}   ── 时钟 + 60px 主读数数字字库
    animation/app_animation.{h,c}
    pass/app_pass.{h,c}     ── 通行证（UI 层占位页）
  CMakeLists.txt
```

> `CMakeLists.txt` 用**显式文件列表**（非 GLOB）：新增 `.c` 要登记进 `srcs`，新增目录要加进 `INCLUDE_DIRS`（组件内部一律按文件名包含，如 `#include "app_image.h"`）。
> 有意为之：**系统设置属于框架**（它是 UI 层页面、由调度器直接注册），不跟内容 app 放一起。

---

## 5. 入口与初始化

### 5.1 main 入口

```c
// main.c —— 统一入口只调 film_sys_init()
void app_main(void) {
    film_sys_init();   // 内部按序完成 hal / service / app 三层初始化
}

// sys_init.c（film_sys/src/sys_init.c#L73-L80）
void film_sys_init(void) {
    film_hal_init();       // 抽象层
    film_service_init();   // 服务层
    film_app_init();       // app 层初始化（结构完整，单一入口）
}
```

### 5.2 film_app_init() 职责

创建 `app_task` + 队列 → 注册 app（图片/模板恒注册；`FULL` 模式下追加 时钟/动图/主菜单/系统设置/通行证）→ 读取开机行为参数（`app_boot_cfg_init`）→ 按配置走开机流程（见 §8.3）→ 注册输入回调转发到 app_manager。

---

## 6. 统一 app 接口（app_interface.h）

```c
typedef enum {
    APP_ID_IMAGE = 0,
    APP_ID_TEMPLATE,
    APP_ID_CLOCK,
    APP_ID_ANIMATION,
    APP_ID_SETTINGS,       // 系统设置（UI 层）
    APP_ID_MENU,           // 主菜单（UI 层，调度器的"根"状态，不在轮播列表里）
    APP_ID_PASS,           // 通行证（UI 层占位页；id 追加在末尾，避免动到已落盘的 id）
    APP_ID_MAX,
} app_id_t;

typedef struct {
    app_id_t id;
    const char *name;
    const char *data_dir;
    uint32_t tick_ms;
    const uint16_t *events; // 关注的全局事件 ID 列表（sys_event_id_t，0 结尾）；NULL 表示不订阅
    void (*on_enter)(void);                 // 切到该 app
    void (*on_exit)(void);                  // 离开该 app
    void (*on_event)(const app_event_t *e); // 按键/BLE/网络/下载完成/总线事件
    void (*on_tick)(void);                  // 可选：周期性刷新（时钟/动图用）
} app_entry_t;
```

### 事件源（app_event_t）

```c
#define APP_EVENT_PAYLOAD_MAX   (16)    // 事件内联负载上限（与 sys_event 对齐）

typedef enum {
    APP_EVT_INPUT,       // hal_input 回调转发
    APP_EVT_TIMER,       // 定时器 tick
    APP_EVT_SWITCH,      // 内部：请求切换 app（app_manager 消费）
    APP_EVT_SYS,         // 全局事件总线事件（sys_event，按 app_entry.events 过滤）
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    input_press_type_t input;
    uint32_t cmd;               // APP_EVT_SYS=sys_event_id_t
    void *data;                 // 附加数据（APP_EVT_SWITCH 为 app_id_t 值）
    uint8_t payload[APP_EVENT_PAYLOAD_MAX]; // APP_EVT_SYS 事件负载（值语义）
    uint8_t len;
} app_event_t;
```

### 事件路由返回值

输入路由结果由 app_manager 内部枚举 `app_input_result_t` 表示（`APP_INPUT_CONSUMED` / `APP_INPUT_PASS`），仅用于调度器内部判定是否放行给当前 app。

---

## 7. AppManager 调度器（app_manager.h）

```c
void film_app_init(void);                     // 统一入口（app_manager_init 的既定名）
void app_manager_init(void);                  // 创建队列/任务/定时器 + 通配订阅事件总线
void app_manager_register(const app_entry_t *app);
void app_manager_switch(app_id_t id);
void app_manager_on_input(input_press_type_t key);
app_id_t app_manager_get_current(void);
```

`app_task` 循环：
```
取事件 → 若 app 未运行则 on_enter → 转发给当前 app.on_event
周期性 on_tick（条件触发：时钟每秒、动图每帧）
```

---

## 8. App 切换：两种交互模式（sys_cfg.h 切换）

按键切换交互由 `sys_cfg.h` 的 `SYS_APP_SWITCH_MODE` 三选一配置，运行期解析出生效模式
（`app_manager_get_switch_mode()`）：

| 配置 | 生效条件 | 按键行为 | 注册的 app |
|---|---|---|---|
| `SYS_APP_SWITCH_NONE`(0) | 恒生效 | 按键不参与切换，全部放行给当前 app（纯相框，BLE 远程切换仍有效） | 图片 + 模板 |
| `SYS_APP_SWITCH_SIMPLE`(1) | 恒生效 | 上/下回图片；确认键在图片 <-> 最近推送的 app 互切 | 图片 + 模板 |
| `SYS_APP_SWITCH_FULL`(2) | 需 UI 层可用（`SYS_UI_ENABLE` + MonoFast 面板 + 上/下导航与确认键），否则**自动降级为 SIMPLE** | 上电按 BOOT PAGE / START APP 配置走（默认 开机画面 → 主菜单）；菜单内 上/下 选择、确认键进入；其余 app 内长按确认键 = 手动休眠、双击确认键 = 回主菜单 | 全部 7 个（4 内容 + 通行证 + 设置 + 主菜单） |

> 注册集合按**配置模式**判定（非生效模式）：`FULL` 在跑不动主菜单的屏上只降级按键交互，
> 仍注册全部 app，保证连接端远程切换（BLE `0x4B`）行为不变。

### 8.1 主菜单态（FULL）

主菜单本身就是一个 **UI 层 app**（`APP_ID_MENU` / `g_app_menu_entry`），是调度器的"根"状态 ——
它不像图片 app 那样是"被切进去的内容"，而是所有内容 app 的入口：

```
BOOT PAGE=SHOW ──8s（进度条在页面内自走）──→ START APP（默认 [MENU]）
                                              │  上/下 → 换高亮项（下标下发 UI 页重绘）
                                              │  确认 → app_do_switch(m_menu_entries[m_menu_sel])
                                              │
  [IMAGE/TEMPLATE/CLOCK/ANIMATION/PASS/SETTINGS] ──长按确认键──→ 手动休眠（画休眠卡）
                                                 ──双击确认键──→ [MENU]
```

> `BOOT PAGE=SKIP` 时跳过开机画面，上电直接落到 `START APP`；两者均由设置页配置，见 §8.3。

**关键：任务/事件划分**
- 在 `[MENU]` 态：由 app_manager 接管**全部**按键（上/下换选中索引、确认键进入），页面只显示（`on_key = NULL`）。
- 在其余 app：**长按确认键 = 手动休眠**（全局语义，不看休眠开关）、**双击确认键 = 退回主菜单**；
  短按/上/下 照旧放行给 app 自己。

```c
// app_manager_process_input()：返回 app_input_result_t（APP_INPUT_CONSUMED / APP_INPUT_PASS）
static app_input_result_t app_manager_process_input(input_press_type_t key) {
    if (mode == NONE) return APP_INPUT_PASS;
    if (m_current_app == APP_ID_MENU) {             // 菜单态：接管全部按键
        UP/DOWN: m_menu_sel 循环 + ui_core_post(APP_UI_MSG_MENU_SEL);
        ENTER  : app_do_switch(m_menu_entries[m_menu_sel]);
        return APP_INPUT_CONSUMED;
    }
    if (key == INPUT_PRESS_LONG) {                  // 长按 = 手动休眠
        app_manager_sleep_from_app();
        return APP_INPUT_CONSUMED;
    }
    if (key == INPUT_PRESS_DOUBLE && app_menu_available()) {   // 双击 = 回主菜单
        app_menu_open();
        return APP_INPUT_CONSUMED;
    }
    return APP_INPUT_PASS;   // 否则放行给当前 app
}
```

`m_menu_entries[]`（app_manager.c）是菜单的**行为表**（下标 → app_id），
顺序必须与 `app_menu.c` 的 `MENU_ITEMS[]`（**视觉表**）严格一致。

### 8.2 简易切换态（SIMPLE）

不做主菜单，靠**声明式按键掩码**避让：每个 app 在 `app_entry_t.keys` 声明自己占用的按键
（`APP_KEY_UP/DOWN/SHORT/LONG`），调度器只在 app **未占用**的键上做切换，避免打断 app 自身功能。

- 上/下：当前不是图片 app → 直接回图片；是图片 app → 放行（图片已声明占用，继续翻页）。
- 确认键短按：当前是图片 app → 切到 `m_last_guest_app`（最近一次切入的非图片 app，含 BLE 远程推送目标）；
  当前不是图片 app → 回图片。未推送过任何内容时放行，不盲切。

> 按键占用现状：图片 = UP\|DOWN；动图 = SHORT\|UP\|DOWN；模板/时钟/设置 = 无（设置页用 UP\|DOWN\|SHORT，
> 长按 / 双击留给全局语义：手工休眠 / 回主菜单）。因此简易模式下只有「图片 + 模板」在场，模板不占用任何键，
> 上/下与确认键都能安全接管。

**主菜单 UI（已实现）：**
- 菜单是 **LVGL 页面**（横向轮播 ±2 共 5 张卡片 + 指示点 + ACTIVE 大字 + 层级面板），不是直绘封面。
- 图标来自 **SD 卡可替换资源**（`/sdcard/app/<app>/icon.bin`，FFUI 容器，80×80 1bpp）；
  缺失/尺寸不符/头非法 → 回退固件内置默认图（`ui_defaults.c`）。
- 选中项 = 实心黑板 + 反白图标 + 加粗描边；反色由固件对展开后的亮度缓冲取反（`~L8`）得到，**只需一份资源**。
- 进入菜单时会把选中索引对齐到"刚离开的 app"，避免菜单高亮与当前画面脱节。

### 8.3 开机行为参数（BOOT PAGE / START APP）

上电怎么走由两个 **app 层参数**决定（`app_boot_cfg.{h,c}`），与 `SYS_APP_SWITCH_MODE` 正交：
模式决定"有没有菜单/按键能不能切"，这两个参数决定"上电先画什么、落在哪个 app"。

| 参数 | 取值 | 说明 |
|---|---|---|
| `boot_page` | `APP_BOOT_PAGE_SHOW`(0) / `SKIP`(1) | 上电是否先画开机画面。开机页首帧顺带完成整屏清场，**SKIP 时不做清场**，直接进 START APP |
| `start_app` | `app_start_t` | 落点：`MENU`(0, 默认) / `LAST` / 图片 / 模板 / 时钟 / 动图 / 设置 / 通行证（**下标即存盘值**） |

**持久化**：借用 `service_param_app_load/save()` 的 app 状态 blob 通道，存在**保留槽位 `app7`**
（`SERVICE_PARAM_APP_ID_BOOT_CFG`，NVS namespace `FRAMEFILM_APP`，key `app7`），
6 字节头（magic `0xA55A` + size + version + rsvd）自带校验，app 层不写 NVS 代码。

> 为什么不加进 `ServiceParam_Def_t`：这两个参数只描述"上电时 app 层怎么走"，与 service 层
> （WiFi/BLE/休眠）无关；而且改 `ServiceParam_Def_t` 布局会触发**整体重置**，连带清掉 WiFi 配网与时区。
> 也正因此，`SERVICE_PARAM_APP_NUM` 从 6 提到 8，把 `app6 通行证` 与 `app7 开机行为` 一起纳入槽位范围。

**落点解析**（`app_boot_cfg_resolve_target()`，逐个排除不可用目标，**永远返回一个已注册的 app**）：

1. 模式裁剪：非 `FULL`（或 UI 层不可用）没有主菜单 → 回落 **图片**；
2. 指定 app 未注册 → 回落 **图片**；
3. `LAST` → 取 `service_param_app_current_get()`，越界/未注册 → 回落 **图片**。

**上电流程**（`app_init.c` 第 7 步）：

- `boot_page=SHOW` 且 `FULL` 且 UI 就绪 → `app_manager_boot_show(ops, target)`，开关机页占屏 8s 后 `app_manager_boot_end()` 切到 `target`；
- 否则（`SKIP`，或本来就没有开机页）→ 直接 `app_manager_switch(target)`。

**设置页**：`BOOT PAGE` / `START APP` 两行（`SET_KIND_CYCLE`），候选表顺序必须与 `app_start_t` 一致；
写入走 app 侧 `settings_on_event()` 校验后落盘，再把整页快照回投给页面。

### 8.4 状态栏（时间 / 电量）刷新

UI 页公共外壳 `app_shell`（`app_shell.{h,c}`）在状态栏**正中间**显示 `HH:MM`，与电量/WiFi/蓝牙图标同一行。

- **随页重绘**：`app_shell_apply()` 每次被调用时刷新时间与状态，页面自己画完就顺带带上；
- **空闲兜底**：页面 create 时 `app_shell_start_tick(&m_shell, cb)` 挂一个 **10s** 周期 tick，
  它刷时间 + 调页面自己的回调（默认 `app_shell_request_status()` 重新拉电量/连接态）；
  页面 destroy 时 `app_shell_release()` 删 timer 并清结构体。
- **去重**：写控件前先做变更检测（状态用 `memcmp`、时间用字符串比对）——电子纸每次写控件都是全帧
  （约 940ms 且会闪），去重后实际刷新 ≤1 次/分钟。
- 未校时（年份 < 2020）显示 `--:--`，不会画出一个荒唐的时间。

---

## 9. 能力判定（主菜单可交互能力）

用能力位运行时判定，而非散落编译宏：

```c
// app_render.c：主菜单是 UI 层页面 —— 要 UI 层编进来 + 面板能快刷 + 有上/下导航与确认键
int app_render_has_app_menu(void)
{
    if (SYS_UI_ENABLE == 0) return 0;
    return app_render_has_monofast() && SYS_INPUT_HAS_NAV_ENTER;
}
```

- `SYS_APP_SWITCH_MODE == FULL` 且 `app_render_has_app_menu() == 0` → 生效模式自动降级为 `SIMPLE`（启动日志给出 `cfg/effective/app_menu` 三个值），因此**不需要保证 `sys_cfg.h` 与 `hal_epd.h` 手工同步**。
- 该函数只回答「能不能跑主菜单」，与「能不能切换 app」解耦：简易模式在任何屏幕上都可切换。



---

## 10. 应用映射

| 应用 | 数据来源 | 渲染方式 | 依赖的 service/hal |
|---|---|---|---|
| 图片 img | 本地 TF / BLE / WiFi | 全屏 film（v1 或 v2） | `service_file` + `service_film` + `hal_epd_display_film` |
| 模板 template | 蓝牙直传 / WiFi 下载 film 落盘后装载缓存 | 全屏 film | `service_file`（load）+ `service_ble` / `service_wifi`（推送落盘）+ `app_render_display_full` |
| 时钟 clock | 设备端生成（WiFi/蓝牙校时 + 本地 ESP32 RTC） | 差分刷新 | `hal_epd_display_mono`（3.7 MonoFast） |
| 动图 animation | 独立文件夹 | 全屏 film v2 多帧（循环） | `service_file`（多目录）+ `service_film` + v2 帧循环 |

### 10.1 数据通道抽象（图片/模板/动图共用）

"数据从哪来"用 source 抽象，三种来源统一为一个 `app_frame_t`：

```c
typedef struct {
    uint32_t frame_count;              // 1=静态，>1=动图
    const uint8_t *frame_data;         // 当前帧主体 或 完整 film
    app_render_cb render;              // 由调用方决定怎么画
} app_frame_t;

int32_t app_source_local_load(uint32_t file_id, app_frame_t *out);  // service_file
int32_t app_source_ble_receive(...);                                 // service_ble 收帧
int32_t app_source_wifi_receive(uint8_t *buf, uint32_t len);         // service_wifi 下载后
```

---

## 11. 动图 app 的独立设计（重点）

### 11.1 独立文件夹

```c
#define ANIM_DIR    "/sdcard/animation"   // 独立文件夹，与 /sdcard/film 分开
```

### 11.2 与图片 app 的本质差异

| 维度 | 图片 app | 动图 app |
|---|---|---|
| 切换语义 | UP/DOWN 切"不同图片" | UP/DOWN 切"不同动图文件" |
| 播放模型 | 切到即停 | **持续循环**，靠 `on_tick` 推进 |
| 文件扫描 | `/sdcard/film` | `/sdcard/animation` |

### 11.3 播放推进

```c
// 动图 app 的 on_tick
frame_idx = (frame_idx + 1) % frame_count;
render_frame(frame_idx);          // 复用渲染抽象
```

### 11.4 文件服务多目录支持

动图目录单独入库。建议复用 `service_file` 的加载与 PSRAM 缓冲逻辑，把目录做成可配置参数（`/sdcard/film` 或 `/sdcard/animation`），而非复制一份文件服务。

### 11.5 上传通道（已确认）

动图**不引入独立的上传命令**：与普通图片完全同通道（BLE 文件传输 / WiFi 下载 / TF 卡直读），报文仍是标准 film 文件——只是体积更大、文件头 `FrameCount>1`。服务端在**接收/保存时按目录上下文自动落盘**：图片存 `/sdcard/film`，动图存 `/sdcard/animation`（保存前由连接端声明目标目录，或动图文件默认落 `/sdcard/animation`）。因此 `service_file` 的传输路径无需改动，仅保存目标目录可切换。至于"切到动图 app / 触发播放"这类**控制**指令，仍按 §13.1 走 `0x3E` 起的新命令（与文件上传本身无关）。

---

## 12. 渲染抽象（三档能力）

`hal_epd` 暴露能力位，app 层运行时判断：

```c
typedef enum {
    EPD_CAP_4BPP     = 1<<0,  // 所有屏都有（v1 4bpp）
    EPD_CAP_8BPP     = 1<<1,  // 3.7 屏 8bpp 索引色（ColorQual/ColorFast）
    EPD_CAP_MONOFAST = 1<<2,  // 3.7 屏 MonoFast 差分局刷
    EPD_CAP_PARTIAL  = 1<<3,  // 局部窗口刷新（709 双面板）
} epd_cap_t;
uint32_t hal_epd_get_capabilities(void);
```

三档渲染路径（自动降级）：
1. **全屏静态 film**（v1/v2）：`hal_epd_display_film` 所有机型通用。
2. **全屏 8bpp film**（3.7，Format 0x02/0x03）：`hal_epd_display_8bpp_mode(buf, mode)`，`mode` 由 `Format` 决定（0x02→1 ColorQual，0x03→0 ColorFast）。
3. **实时动态**（时钟）：有 `EPD_CAP_MONOFAST` 用 `hal_epd_display_mono` 差分局刷；没有则降级为定时整屏重刷。该路径仅在 3.7 屏用到（app 层切换菜单只在 3.7 启用）；**非 3.7 屏只支持图片 app**，不跑时钟/动图/模板。

---

## 13. service_film / service_file 改造（已定稿）

为支撑 app 层，两个服务做最小侵入改造。**已确认的取舍**：

- service_film **彻底去掉 next / prev / clear 业务**，退化为基础"渲染单帧/整张"的底座。
- 动图播放循环由 **app 层控制**，service_film 暴露"按帧索引渲染"；**启动播放后 service 自动跑完一帧的完整刷相**。
- 文件校验采用**宽松校验**（只按扩展名 + 最小大小过滤，不按固定 `FileSize`）。

### 13.1 service_film 接口改造

#### 保留
- `service_film_init(void)`
- `service_film_display(uint32_t file_id)` —— 加载并渲染单帧/整张静态图（BLE `0x07` [service_ble.c](file:///e:/project/FrameFilm/firmware/frame_film/components/film_service/src/service_ble.c) 与保存后自动加载 [service_file.c](file:///e:/project/FrameFilm/firmware/frame_film/components/film_service/src/service_file.c) 仍调用）
- 当前显示文件 id / 装载完成查询已从 service_film 移除，改由 service_file 提供：`service_file_get_current_id(void)`、`service_file_get_load_complete(void)`（BLE `0x08` 现调用 `service_file_get_current_id()`，见 [service_ble.c](file:///e:/project/FrameFilm/firmware/frame_film/components/film_service/src/service_ble.c#L482-L485)）

#### 删除（业务上移到 app 层）
- `service_film_next / prev / clear / set_play_mode`
- 内部事件 `MSG_FILM_NEXT / MSG_FILM_PREV / MSG_FILM_CLEAR / MSG_FILM_AUTO_PLAY`
- `film_task_handle` 里的硬绑输入回调（[service_film.c L124-127](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_film.c#L124-L127)）：改为 app_manager 统一注册输入并路由。
- `film_init_event` 里 `FILM_PLAY_MODE_WIFI` 的"等 WiFi → 下载"逻辑（[L187-213](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_film.c#L187-L213)）：开机决策上移 app_manager。

#### 新增（按帧索引渲染，支撑动图）
```c
// 按帧索引渲染：加载后按 format 分派，内部自动完成该帧的完整刷新（含刷相）
// idx = 0 表示单帧静态图；动图时 app 层循环调用
void service_film_render_frame(uint32_t file_id, uint32_t frame_idx);

// 供 app 层判断该文件是否为多帧动图
uint32_t service_film_get_frame_count(uint32_t file_id);   // 读取头 offset 0x0A 小端
```

**刷新方式由 film 文件头 `Format` 字段自主声明，service 层按 `Format` 映射到 `mode`**（无需上层硬编码）：

| `Format` | 刷新方式 | hal 接口 |
|---:|---|---|
| `0x00` | v1 4bpp | `hal_epd_display_film(buf)` |
| `0x01` | MonoFast | `hal_epd_display_mono(buf)` |
| `0x02` | ColorQual（3 相） | `hal_epd_display_8bpp_mode(buf, 1)` |
| `0x03` | ColorFast（2 相） | `hal_epd_display_8bpp_mode(buf, 0)` |

> **关键：固件** `epd_spectra_display_color(index8, mode)`（[hal_epd_370.c L505](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_hal/src/hal_epd_370.c#L505)）**已实现 `mode` 分派**（`0`=ColorFast 2 相，`1`=ColorQual 3 相）。公开接口 `hal_epd_display_8bpp_mode` 向 service 层暴露 `mode`：
> ```c
> // hal_epd.h 新增：带 mode 的 8bpp 显示（mode 0=ColorFast, 1=ColorQual）
> void hal_epd_display_8bpp_mode(const unsigned char *index8Data, uint8_t mode);
> // 实现即 epd_spectra_display_color(index8Data, mode)
> ```
> 原 `hal_epd_display_pic48`（把 mode 写死为 `1`/ColorQual）已由 `hal_epd_display_8bpp_mode(index8Data, 1)` 取代，不再保留。

动图播放由 app 层驱动：app 的 `on_tick` 里 `frame_idx = (frame_idx+1) % frame_count` 并调 `service_film_render_frame`，service 不管理播放节奏。

#### BLE 命令层的适配
`service_ble.c` 的 `0x07 FILE_DISPLAY` 仍走 `service_film_display`；新增"切 app / 播放动图"命令从 `0x3E` 起占用（`0x42` 已占）。见第 13.3 节跨端一致性。

### 13.2 service_file 接口改造

#### 新增：目录上下文（支撑动图独立文件夹）
```c
#define FILM_DIR    "/sdcard/film"        // 保留
#define ANIM_DIR    "/sdcard/animation"   // 新增（动图独立文件夹）

void   service_file_set_dir(const char *dir);   // 切换目录后清空列表并触发 refresh
const char *service_file_get_dir(void);
```
内部所有用 `FILM_DIR` 拼接路径处（[L228](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_file.c#L228)、[L384](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_file.c#L384)、[L419](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_file.c#L419)、[L550](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_file.c#L550)、[L781](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_service/src/service_file.c#L781)）改为读运行时目录。

#### 新增：宽松校验（替代固定大小过滤）
把 `file_list_refresh_event` / `MSG_FILE_SAVE_START` 里的 `st.st_size == FILE_EPD_IMGAGE_SIZE` 改为：
```c
if (ext && strcmp(ext, FILM_FILE_EXT) == 0 && st.st_size >= FILM_HEADER_SIZE)
```
不再按 v1 `W*H/2` 死算大小，v2（8bpp/mono/多帧）文件均能入列表。`FILM_PIXEL_DATA_SIZE` / `FILE_EPD_IMGAGE_SIZE` 宏废弃或仅保留作注释参考。

#### 保留（动图复用同一条加载/PSRAM 通道）
- `service_file_load / get_buffer / get_buffer_size / get_count / get_current_id / get_load_complete / get_filename_safe / get_size / delete / refresh_list`
- 单缓冲 + `current_file_id` 在 `set_dir` 时清空；动图与图片**不同时运行**，故单缓冲够用，无需多缓冲。

#### 保存接口路径
`save_start / save_data / save_stop` 存到 `service_file_get_dir()` 指向的目录，动图可存到 `/sdcard/animation`。

### 13.3 配套改动

1. **入口接线**：`film_app_init()` 放入 `film_sys_init()`（[sys_init.c#L73-L80](file:///e:/project/aitest/FrameFilm-main/firmware/frame_film/components/film_sys/src/sys_init.c#L73-L80)）内，在 `film_service_init()` 之后调用；`main.c` 的 `app_main()` 仍只调 `film_sys_init()`，保持单一完整入口。
2. **输入回调解绑**：`service_film.c` 中硬绑的 `INPUT_PRESS_*` 删除，改由 app_manager 注册并路由。
3. **时钟 app 时间源（已确认）**：时间源 = WiFi 校时 + 蓝牙校时 + **本地 ESP32 RTC** 兜底（断电保留时间）。加轻量时间抽象（SNTP + RTC）；`hal_epd_clock_demo` 是阻塞演示，生产 app 改为非阻塞 `on_tick` 局部更新。
4. **跨端一致性**：新增 BLE 命令（`0x3E` 起）同步 `ble-utils.js` / `frame.js` / `blecmd_protocol.md`；v2 颜色编码同步 `hal_epd.h` / `film-utils.js` / `convert.js`。

---

## 14. 待确认 / 后续补充点

- [x] service_film 是否彻底去掉 next/prev/clear —— **已确认：去掉**
- [x] 动图播放循环归属 —— **已确认：app 层控制循环，service 渲染帧，启动后自动完成该帧刷相**
- [x] v2 文件校验策略 —— **已确认：宽松校验（扩展名 + 最小大小）**
- [x] app 切换菜单 UI —— **已落地：主菜单本身就是一个 UI 层 app（`APP_ID_MENU` / `g_app_menu_entry`），页面是 LVGL 横向轮播（卡片 + 指示点 + ACTIVE 大字 + 层级面板）；图标走 SD 可替换资源 `/sdcard/app/<app>/icon.bin`（FFUI 容器，缺失/非法回退内置默认图）。旧的 DIRECT 封面菜单（`cover.film` + `APP_SWITCH_SELECT` 超时状态机）已删除**
- [x] 动图上传通道 —— **已确认：与图片同通道（BLE/WiFi/TF 直读），走同一 film 报文，不引入独立上传命令；接收/保存时按目录上下文自动落到 `/sdcard/animation`（切 app / 触发播放等控制指令仍走 `0x3E` 起新命令）**
- [x] 模板 app 推图协议 —— **已确认：蓝牙直传 / WiFi 下载统一走 film 报文落盘，app 侧主动 `service_file_load()` 装载缓存后整屏渲染**
- [x] 时钟 app 时间源 —— **已确认：WiFi + 蓝牙校时，本地 ESP32 RTC 兜底**
- [x] 非 3.7 屏上动图/模板/时钟是"不可达"还是"降级显示" —— **已修订：可达性由 `SYS_APP_SWITCH_MODE` 决定。`NONE`/`SIMPLE` 只注册 图片+模板（时钟/动图不注册，远程也切不到）；`FULL` 全量注册（含 主菜单/系统设置），跑不动主菜单的屏按键交互降级为 `SIMPLE` 但全部 app 仍可远程切换**
- [x] 各 app 默认启动 —— **已确认：首次开机默认进入图片（照片墙）**
- [x] ColorFast（`0x03`）刷新入口 —— **已确认：底层 `epd_spectra_display_color(index8, mode)` 已支持 mode 分派（0=ColorFast/1=ColorQual），只需新增公开接口 `hal_epd_display_8bpp_mode` 暴露 mode，由 film 头 `Format` 声明并映射**

---

## 15. 全局事件总线（sys_event）

### 15.1 目标与定位

服务层（file/wifi/ble/ota）产生的状态变化，不再逐条 `set_xxx_cb` 点对点回调 app 层，而是统一 **发布到全局事件总线**；app 层统一订阅，实现"事件上浮"。新增服务/新事件时无需改 app 入口接线。

- **发布方**：`film_service` / `film_hal`（如 BLE 断开、TF 挂载）
- **订阅方**：`app_manager`（通配订阅，归一化后入 app 队列）
- **执行方**：app 任务（真正的业务处理，不占用发布方上下文）

### 15.2 存放位置与依赖

`film_sys/inc/sys_event.h` + `src/sys_event.c`。

依赖链名义上为 `film_app → film_service → film_hal → film_sys`，但 `film_sys` 同时承担最底层（sys_log/sys_cfg）与最顶层编排（`film_sys_init`），其 CMake 已 `requires film_hal film_service film_app`，故 service 层可零新增依赖地 `#include "sys_event.h"`。

### 15.3 数据模型

```c
#define SYS_EVENT_MAX_SUBSCRIBERS   (8)     // 固定订阅表，免动态内存
#define SYS_EVENT_PAYLOAD_MAX       (16)    // 内联负载上限，值语义 memcpy

typedef struct {
    uint16_t id;                            // sys_event_id_t
    uint16_t len;                           // payload 有效字节数
    uint8_t  from_isr;                      // 1=ISR 上下文发布
    uint8_t  payload[SYS_EVENT_PAYLOAD_MAX];// 内联负载（值语义，投递后与发布方栈无关）
} sys_event_t;

typedef void (*sys_event_cb_t)(const sys_event_t *e, void *ctx);
```

**关键决策：固定内联 payload + 值语义，不用 `void *data`。** 异步投递时若只传指针，发布方栈/临时缓冲已失效会悬空；值语义拷贝 16 字节，安全且无动态内存。

### 15.4 事件 ID 分段

| 段 | 模块 | 事件 |
|---|---|---|
| `0x01xx` | 文件 / SD | `FILE_SAVED` `FILE_LIST` `SD_MOUNT` `SD_UNMOUNT` |
| `0x02xx` | WiFi | `WIFI_STATE` `WIFI_DL_DONE` |
| `0x03xx` | BLE | `BLE_CONN` `BLE_DISCONN` `BLE_APP_CMD` |
| `0x04xx` | OTA | `OTA_PROGRESS` |
| `0xFFFF` | 通配 | `SYS_EVT_ANY`（仅用于订阅） |

ID 一旦定义不再变更；新增事件在所属段内顺延。

### 15.5 投递模型：同步扇出 + app 任务入队

```
service (BLE/WiFi/OTA/HTTP 任务上下文)
   └─ sys_event_publish()  →  同步遍历订阅表
          └─ app_manager_on_sys_event()   ← 只做：归一化 + 非阻塞入队
                 └─ xQueueSend(app_queue, ..., 0)
                        └─ app 任务：app_handle_event() → 当前 app on_event()
```

- **同步扇出**：`publish` 在调用者上下文立即回调订阅者，无中间队列、低延迟、实现简单。
- **订阅回调必须短小非阻塞**：app_manager 的回调只做映射 + `xQueueSend`，业务一律留给 app 任务。
- **背压**：队列满时非阻塞发送失败即丢弃并 `sys_logw` 告警，绝不阻塞发布方（BLE/HTTP 栈任务）。OTA 进度按每 10% 分档发布，避免刷屏。

### 15.6 app 侧订阅方式

- **通用路径**：app 在 `app_entry_t.events` 声明关注的 ID 列表（0 结尾），总线事件以 `APP_EVT_SYS` 入队；`app_handle_event()` 用 `app_wants_sys_event()` 过滤，只把当前 app 关注的事件下发到其 `on_event()`。
- **各 app 关注的 ID**：图片 `FILE_SAVED`+`FILE_LIST`；模板 `FILE_SAVED`；时钟 `FILE_SAVED`；动图 `FILE_SAVED`。
- `app_handle_event()` 对 `APP_EVT_SYS` 先消费 `SYS_EVT_BLE_APP_CMD`（app 切换指令），再按白名单下发；不在白名单内的事件不下发到 `on_event()`。
- `events == NULL` 表示不订阅任何通用总线事件。

### 15.7 边界原则

1. **广播 ≠ 消费**：总线不做 `CONSUMED/PASS` 语义，每个订阅者都收到；"消费"只保留在 app_manager 的输入路由。
2. **查询不走总线**：`service_ble_set_app_id_get_cb()`、`service_file_get_count()` 等**同步取值**仍是直接函数调用；总线只承载**状态变化通知**。
3. **ISR 发布**：`sys_event_publish_isr()` 提供 ISR 路径，但此时订阅回调也在 ISR 上下文，必须 ISR-safe（如 `xQueueSendFromISR`）。当前无 ISR 发布方。
4. **订阅表初始化期构建**：`sys_event_init()` 在 `film_sys_init()` 首行调用，早于各层发布/订阅；运行期只读。

---

## 16. 已知限制（暂不修，记录备查）

1. **订阅表无并发保护**：`sys_event_subscribe / unsubscribe` 未加互斥，依赖"初始化期构建、运行期只读"约定；若未来在运行期动态订阅/退订，需与 `publish` 的遍历做互斥。
2. **`sys_event_publish_isr()` 暂无调用方**：ISR 发布路径已提供，但未接入任何中断源，订阅回调的 ISR-safe 约束未经运行时验证。
3. **无编译期尺寸断言**：`SYS_EVENT_PAYLOAD_MAX` 与 `APP_EVENT_PAYLOAD_MAX` 必须保持相等（app_manager 归一化时按 16B 整体拷贝），当前靠人工维护，无 `_Static_assert` 保护。
