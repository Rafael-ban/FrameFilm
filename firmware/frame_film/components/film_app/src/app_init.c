/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * FileName : /film_app/src/app_init.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层统一入口
 * ChangeLog: Change Notes
 *
 *********************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_heap_caps.h"

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_epd.h"
#include "hal_sd.h"
#include "service_ble.h"
#include "service_param.h"
#include "ui_conf.h"    /* UI_CALIB_FRAME：标定帧模式下开机页暂停自动前进 */
#include "app_init.h"
#include "app_manager.h"
#include "app_render.h"
#include "app_interface.h"
#include "app_image.h"
#include "app_template.h"
#include "app_clock.h"
#include "app_animation.h"
#include "app_menu.h"
#include "app_settings.h"
#include "app_boot.h"

/*********************************************************************
 * MACROS
 */
#define APP_INIT_TAG    "app_init"

#define APP_BOOT_STEP_MS    (500)   // 开机自检每步停留时长（5 步 ≈ 2.5s）

/*********************************************************************
 * LOCAL VARIABLES
 */
static TimerHandle_t m_boot_timer = NULL;
static uint8_t m_boot_step = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void app_init_input_short(void);
static void app_init_input_long(void);
static void app_init_input_up(void);
static void app_init_input_down(void);
static void app_init_register_inputs(void);
static uint8_t app_init_ble_app_id_get(void);
static void app_init_boot_start(void);
static void app_init_boot_tick(TimerHandle_t xTimer);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void film_app_init(void)
{
    sys_logi(APP_INIT_TAG, "app layer init start");

    /* 启动内存快照：PSRAM 的分片/余量直接决定能否加载大 film
       （8bpp v2 film 需 345KB 连续，4bpp v1 需 172KB） */
    sys_logi(APP_INIT_TAG, "heap: psram total=%u free=%u largest=%u | internal total=%u free=%u largest=%u",
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    /* 1. 创建 app 任务 + 事件队列 + 周期 tick 心跳定时器 */
    app_manager_init();

    /* 2. UI 框架层（LVGL）：必须在任何 app 切换之前就绪，
          否则首个 UI 层 app 的 page_enter 会因队列未建而被丢弃。
          面板不支持 mono 时内部直接返回错误，UI 层保持不可用。 */
    if(ui_core_init() != 0)
    {
        sys_logw(APP_INIT_TAG, "ui core init failed, ui layer unavailable");
    }

    /* 3. 注册 app：图片/模板为通用底座（模板承接蓝牙/WiFi 的任意实时推送内容）；
          仅全功能模式额外注册其余 app，简易/关闭模式省下这部分 flash 与运行开销。
          注意按“配置模式”而非“生效模式”判定：FULL 在跑不动主菜单的屏上虽降级为
          简易交互，仍保留 4 个内容 app 的注册，使连接端远程切换（BLE 0x4B）行为不变。 */
    app_manager_register(&g_app_image_entry);
    app_manager_register(&g_app_template_entry);
#if (SYS_APP_SWITCH_MODE == SYS_APP_SWITCH_FULL)
    app_manager_register(&g_app_animation_entry);   // 直绘层，不依赖 UI 框架
#if (SYS_UI_ENABLE == 1)
    /* 以下三个是 UI 层页面：UI 层没编进来（SYS_UI_ENABLE=0）时无从渲染，故不注册。
       主菜单是调度器的“根”状态（上电后落在它上面），设置页只能经主菜单进入 */
    app_manager_register(&g_app_clock_entry);
    app_manager_register(&g_app_menu_entry);
    app_manager_register(&g_app_settings_entry);
#endif
#endif

    /* 4. 切换交互形态日志（cfg 与生效值可能不同：FULL 在跑不动主菜单的屏上降级为 SIMPLE） */
    sys_logi(APP_INIT_TAG, "app switch cfg=%d effective=%d app_menu=%d panel=%#x",
             (int)SYS_APP_SWITCH_MODE, (int)app_manager_get_switch_mode(),
             app_render_has_app_menu(), (unsigned)EPD_PANEL_ID);

    /* 5. 注册 BLE→app 查询回调（切换/保存完成等上行事件已走 sys_event 总线） */
    service_ble_set_app_id_get_cb(app_init_ble_app_id_get);

    /* 6. 注册输入回调并统一转发到 app_manager */
    app_init_register_inputs();

    /* 7. 上电流程：
          · 生效模式为 FULL 且 UI 层就绪 → 先出开机画面（首帧 mono 顺带完成全屏清场），
            自检结束后自动落到主菜单
          · 其余情况（跑不动 UI 层的机型 / 关闭按键切换）→ 恢复上次运行的 app：
            被当前模式裁剪掉的 app、首次开机（无记录）或记录非法 → 回落图片（照片墙） */
    if(app_manager_get_switch_mode() == APP_SWITCH_MODE_FULL
       && ui_core_is_ready()
       && app_manager_is_registered(APP_ID_MENU))
    {
        app_init_boot_start();
        sys_logi(APP_INIT_TAG, "app layer init done (boot page)");
        return;
    }

    int last = service_param_app_current_get();
    if(last < 0 || last >= APP_ID_MAX || !app_manager_is_registered((app_id_t)last))
    {
        last = APP_ID_IMAGE;
    }
    app_manager_switch((app_id_t)last);

    /* 8. 投递一次 BOOT 事件：首个 app 据此执行“开机自动”行为（自动切图 / 自动拉取） */
    app_manager_notify_boot();

    sys_logi(APP_INIT_TAG, "app layer init done");
}

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 展示开机画面并启动自检步进
 *
 * 开机画面是本次上电的首帧 mono，顺带完成整屏清场（实测约 3s），故这条路径
 * 不额外多付一次闪屏。遥测行取真实值，全部只读、无副作用。
 */
static void app_init_boot_start(void)
{
    static app_boot_tele_t s_tele[4];
    static char s_val[4][24];
    uint16_t panel_w = (uint16_t)EPD_WIDTH;
    uint16_t panel_h = (uint16_t)EPD_HEIGHT;

    /* 取值先收敛到窄类型再格式化：GCC 按 int 全域推演会判为可能截断
       （-Wformat-truncation 在本工程是错误级） */
    snprintf(s_val[0], sizeof(s_val[0]), "%ux%u E6", (unsigned)panel_w, (unsigned)panel_h);
    snprintf(s_val[1], sizeof(s_val[1]), "%u KB FREE",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    snprintf(s_val[2], sizeof(s_val[2]), "%s",
             (hal_sd_get_status() == SD_MOUNT) ? "SD MOUNTED" : "NO SD");
    snprintf(s_val[3], sizeof(s_val[3]), "%s", SYS_FIRMWARE_VERSION);

    s_tele[0].name = "PANEL";    s_tele[0].value = s_val[0];
    s_tele[1].name = "PSRAM";    s_tele[1].value = s_val[1];
    s_tele[2].name = "STORAGE";  s_tele[2].value = s_val[2];
    s_tele[3].name = "FIRMWARE"; s_tele[3].value = s_val[3];
    app_boot_set_telemetry(s_tele, 4);

    /* 交给调度器托管（按键在此期间被丢弃），页面在 ui_task 异步建立 */
    app_manager_boot_show(app_boot_ops());
    app_boot_advance(0);

    m_boot_timer = xTimerCreate("boot_seq", pdMS_TO_TICKS(APP_BOOT_STEP_MS), pdTRUE, NULL, app_init_boot_tick);
    if(m_boot_timer != NULL)
    {
        xTimerStart(m_boot_timer, 0);
    }
    else
    {
        /* 定时器创建失败（内存不足）：直接进主菜单，别把用户卡在开机页 */
        sys_loge(APP_INIT_TAG, "boot timer create failed, jump to menu");
        app_manager_boot_end();
    }
}

/**
 * @brief 开机自检步进：推进一步，走完最后一步即切到主菜单
 *
 * 定时器任务上下文：只做计数 + 投递（ui_core_post / 队列），不触碰 lv_*。
 */
static void app_init_boot_tick(TimerHandle_t xTimer)
{
#if (UI_CALIB_FRAME == 1)
    /* 标定帧模式：停在开机页不自动前进，短按确认键才继续（见 ui_conf.h） */
    (void)xTimer;
    (void)m_boot_step;
#else
    m_boot_step++;
    app_boot_advance(m_boot_step);

    if(m_boot_step >= APP_BOOT_STEP_NUM)
    {
        xTimerStop(xTimer, 0);
        app_manager_boot_end();
    }
#endif
}

static void app_init_input_short(void)
{
    app_manager_on_input(INPUT_PRESS_SHORT);
}

static void app_init_input_long(void)
{
    app_manager_on_input(INPUT_PRESS_LONG);
}

static void app_init_input_up(void)
{
    app_manager_on_input(INPUT_PRESS_UP);
}

static void app_init_input_down(void)
{
    app_manager_on_input(INPUT_PRESS_DOWN);
}

static void app_init_register_inputs(void)
{
    /* hal_input 支持同一类型多回调注册；这里回调无参，按类型固定转发 key */
    hal_input_register_cb(INPUT_PRESS_SHORT, app_init_input_short);
    hal_input_register_cb(INPUT_PRESS_LONG,  app_init_input_long);
    hal_input_register_cb(INPUT_PRESS_UP,    app_init_input_up);
    hal_input_register_cb(INPUT_PRESS_DOWN,  app_init_input_down);
}

/**
 * @brief 当前 app 查询回调（APP_CURRENT_GET 回包用）
 */
static uint8_t app_init_ble_app_id_get(void)
{
    return (uint8_t)app_manager_get_current();
}
