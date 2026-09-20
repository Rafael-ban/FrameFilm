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
 * FileName : /film_app/src/app_settings.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: 系统设置（UI 层）：设备信息 + 系统参数，UP/DOWN 选择、ENTER 切换、长按退出
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_bat.h"
#include "hal_epd.h"
#include "hal_sd.h"
#include "service_file.h"
#include "service_param.h"
#include "service_wifi.h"
#include "service_ble.h"        /* service_ble_apply_enable：蓝牙开关运行期起停 */
#include "service_ble_gatts.h"  /* 蓝牙连接状态（状态栏第二档） */

#include "ui_conf.h"    /* UI_CMD_DATA_MAX（快照尺寸断言） */
#include "ui_ops.h"
#include "app_shell.h"
#include "app_settings.h"
#include "app_manager.h"

/*********************************************************************
 * MACROS
 */
#define APP_SET_TAG         "app_set"

#define ROW_H               (30)
#define Y_LIST              (66)     // 列表起点（表头 + 分节标题之下）

/* 循环取值的候选项（也是校验表，上报值必须落在其中） */
#define WAKE_OPT_NUM        (8)
static const char *const WAKE_OPTS[WAKE_OPT_NUM] = {
    "10 MIN", "30 MIN", "60 MIN", "2 H", "6 H", "12 H", "24 H", "48 H"
};
static const uint16_t WAKE_OPTS_MIN[WAKE_OPT_NUM] = {
    10, 30, 60, 120, 360, 720, 1440, 2880       // 与 WAKE_OPTS 一一对应的分钟值
};
#define BTMODE_OPT_NUM      (2)
static const char *const BTMODE_OPTS[BTMODE_OPT_NUM] = {
    "ALWAYS ON", "MANUAL (2X KEY)"
};
#define HB_OPT_NUM          (6)
static const char *const HB_OPTS[HB_OPT_NUM] = {
    "5 S", "10 S", "30 S", "60 S", "120 S", "180 S"
};
static const uint16_t HB_OPTS_SEC[HB_OPT_NUM] = {
    5, 10, 30, 60, 120, 180                      // 与 HB_OPTS 一一对应的秒值
};

/* 快照要经 ui_core_post 一次性下发，长度受命令负载上限约束 */
_Static_assert(sizeof(settings_snapshot_t) <= UI_CMD_DATA_MAX,
               "settings_snapshot_t exceeds ui cmd payload");

/* 列表总高：2 个分节标题（22 + 4 间距） + 11 行。
   正文可用高度 = 720 - 状态栏 30 - 提示行 28 - 上下留白 36 = 626，
   扣掉表头 16 + 间距 24 后余 586 —— 列表 382 有富余，不会溢出到提示行。 */
#define SET_LIST_H          (2 * (22 + 4) + SETTINGS_ROW_NUM * ROW_H)

/*********************************************************************
* TYPEDEFS
*/
typedef struct {
    uint8_t sec;        // 0=DEVICE 1=PARAMETERS（分节首行）
    const char *key;
    uint8_t kind;
    uint8_t field;      // 在 settings_snapshot_t 中的取值字段编号
} settings_row_t;

/* 字段编号（避免依赖结构体偏移，页面对快照只读） */
enum {
    F_BAT = 0, F_WIFI, F_BT, F_PANEL, F_STORAGE, F_FW,
    F_SLEEP, F_AUTOWAKE, F_WIFI_ON, F_BT_ON, F_WAKE_SEL, F_BTMODE_SEL, F_HB_SEL,
};

/* 行表。**顺序即显示顺序，下标即上报的行号。** */
static const settings_row_t ROWS[SETTINGS_ROW_NUM] = {
    { 0, "BATTERY",      SET_KIND_BAR,    F_BAT        },
    { 0, "WIFI",         SET_KIND_TOGGLE, F_WIFI_ON    },
    { 0, "BLUETOOTH",    SET_KIND_TOGGLE, F_BT_ON      },
    { 0, "PANEL",        SET_KIND_TEXT,   F_PANEL      },
    { 0, "STORAGE",      SET_KIND_TEXT,   F_STORAGE    },
    { 0, "FIRMWARE",     SET_KIND_TEXT,   F_FW         },
    { 1, "SLEEP MODE",   SET_KIND_TOGGLE, F_SLEEP      },
    { 1, "AUTO WAKE",    SET_KIND_TOGGLE, F_AUTOWAKE   },
    { 1, "WAKE INTERVAL",SET_KIND_CYCLE,  F_WAKE_SEL   },
    { 1, "BT MODE",      SET_KIND_CYCLE,  F_BTMODE_SEL },
    { 1, "HEARTBEAT",    SET_KIND_CYCLE,  F_HB_SEL     },
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_rows[SETTINGS_ROW_NUM];
static lv_obj_t *m_row_fill[SETTINGS_ROW_NUM];   // 电量条填充块（仅 BATTERY 行非空）
static lv_obj_t *m_row_val[SETTINGS_ROW_NUM];    // 右侧取值标签
static lv_obj_t *m_row_tog[SETTINGS_ROW_NUM];    // 开关块
static lv_obj_t *m_row_ptr[SETTINGS_ROW_NUM];    // 选择指针（只读行恒隐藏）
static lv_obj_t *m_row_st[SETTINGS_ROW_NUM];     // 开关行左侧的状态文案（仅 WiFi/蓝牙行非空）
static lv_obj_t *m_row_idx_label = NULL;

static app_shell_t m_shell;      // 顶部状态栏（数据复用设置快照，不额外往返）
static settings_snapshot_t m_snap;
static uint8_t m_sel = 6;      // 默认落在第一个可操作行（SLEEP MODE）

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void set_ui_create(lv_obj_t *root);
static void set_ui_destroy(void);
static void set_ui_on_key(input_press_type_t key);
static void set_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);
static void settings_on_event(const app_event_t *e);

static lv_obj_t *set_label(lv_obj_t *parent, const lv_font_t *font,
                           lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static int row_is_ro(uint8_t idx)
{
    return (ROWS[idx].kind == SET_KIND_BAR || ROWS[idx].kind == SET_KIND_TEXT);
}

/**
 * @brief 取某行当前显示文本（只读行给设备信息，可操作行给候选值）
 */
static const char *row_text(uint8_t idx)
{
    switch(ROWS[idx].field)
    {
    case F_BAT:        return m_snap.bat;
    case F_WIFI:       return m_snap.wifi;
    case F_BT:         return m_snap.bt;
    case F_PANEL:      return m_snap.panel;
    case F_STORAGE:    return m_snap.storage;
    case F_FW:         return m_snap.fw;
    case F_WAKE_SEL:   return WAKE_OPTS[m_snap.wake_sel % WAKE_OPT_NUM];
    case F_BTMODE_SEL: return BTMODE_OPTS[m_snap.bt_mode_sel % BTMODE_OPT_NUM];
    case F_HB_SEL:     return HB_OPTS[m_snap.hb_sel % HB_OPT_NUM];
    default:           return "";
    }
}

static uint8_t row_on(uint8_t idx)
{
    switch(ROWS[idx].field)
    {
    case F_WIFI_ON:  return m_snap.wifi_on;
    case F_BT_ON:    return m_snap.bt_on;
    case F_SLEEP:    return m_snap.sleep_mode;
    case F_AUTOWAKE: return m_snap.sleep_auto;
    default:         return 0;
    }
}

/**
 * @brief 在候选值表里反查下标（设备侧存的是物理量，页面用的是候选下标）
 */
static uint8_t idx_of_u16(const uint16_t *tab, uint8_t num, uint16_t v)
{
    uint8_t i;

    for(i = 0; i < num; i++)
    {
        if(tab[i] == v)
        {
            return i;
        }
    }
    return 0;   // 不在候选表内（如手机侧下发过任意值）：显示为第一项
}

/**
 * @brief 采集一次设备状态快照（app_task 上下文）
 *
 * 只读各服务/驱动，无副作用。**页面侧不得直接读这些接口**：它们没有跨任务
 * 保证，ui_task 与各服务任务并发访问会取到撕裂值。
 */
static void settings_snapshot_pull(void)
{
    int level = hal_bat_get_percent();
    uint16_t panel_w = (uint16_t)EPD_WIDTH;
    uint16_t panel_h = (uint16_t)EPD_HEIGHT;
    uint8_t  panel_id = (uint8_t)EPD_PANEL_ID;
    uint8_t  pct;

    if(level < 0)
    {
        level = 0;
    }
    if(level > 100)
    {
        level = 100;
    }
    pct = (uint8_t)level;

    /* 格式化前先把取值收敛到窄类型：GCC 按 int 全域推演 %d 时会判为可能截断
       （-Wformat-truncation 在本工程是错误级），窄类型的值域编译器是确定的 */
    m_snap.bat_pct = pct;
    snprintf(m_snap.bat, sizeof(m_snap.bat), "%u%%", (unsigned)pct);

    m_snap.wifi_on = g_service_param.network.wifi_enable ? 1 : 0;
    m_snap.bt_on   = g_service_param.ble.ble_enable ? 1 : 0;
    m_snap.wifi_conn = (m_snap.wifi_on && service_wifi_get_connect_status()) ? 1 : 0;
    m_snap.bt_conn   = (m_snap.bt_on && service_ble_gatts_get_connect()) ? 1 : 0;
    snprintf(m_snap.wifi, sizeof(m_snap.wifi), "%s",
             m_snap.wifi_on ? (m_snap.wifi_conn ? "CONNECTED" : "IDLE") : "OFF");
    snprintf(m_snap.bt, sizeof(m_snap.bt), "%s",
             m_snap.bt_on ? (m_snap.bt_conn ? "CONNECTED" : "ADVERTISING") : "OFF");

    snprintf(m_snap.panel, sizeof(m_snap.panel), "E6 %ux%u ID%02X",
             (unsigned)panel_w, (unsigned)panel_h, (unsigned)panel_id);
    if(hal_sd_get_status() == SD_MOUNT)
    {
        snprintf(m_snap.storage, sizeof(m_snap.storage), "%u FILM", (unsigned)service_file_get_count());
    }
    else
    {
        snprintf(m_snap.storage, sizeof(m_snap.storage), "NO SD");
    }
    snprintf(m_snap.fw, sizeof(m_snap.fw), "%s", SYS_FIRMWARE_VERSION);

    m_snap.sleep_mode = g_service_param.sleep.sleep_mode ? 1 : 0;
    m_snap.sleep_auto = g_service_param.sleep.sleep_auto ? 1 : 0;
    m_snap.wake_sel   = idx_of_u16(WAKE_OPTS_MIN, WAKE_OPT_NUM, g_service_param.sleep.sleep_time);
    m_snap.bt_mode_sel = (g_service_param.ble.ble_mode < BTMODE_OPT_NUM) ? g_service_param.ble.ble_mode : 0;
    m_snap.hb_sel     = idx_of_u16(HB_OPTS_SEC, HB_OPT_NUM, g_service_param.network.film_heartbeat_interval);
}

/**
 * @brief 落地一条页面改动（app_task 上下文）
 *
 * 值先校验再写：页面只上报"行号 + 候选下标"，行号/值都可能因版本不匹配而越界。
 * WiFi 开关与 BLE 通道设置走同一条路径（置参数 + 落盘 + 起停协议栈）。
 */
static void settings_apply(uint8_t row, uint8_t value)
{
    if(row >= SETTINGS_ROW_NUM)
    {
        return;
    }

    switch(ROWS[row].field)
    {
    case F_WIFI_ON:
        if(value > 1) { return; }
        g_service_param.network.wifi_enable = value;
        service_param_save();
        if(value)
        {
            service_wifi_init();
        }
        else
        {
            service_wifi_disconnect();
            service_wifi_deinit();
        }
        break;

    case F_BT_ON:
        if(value > 1) { return; }
        /* 与 WiFi 同一套语义：置参数 + 立刻起停协议栈（不再等重启） */
        service_ble_apply_enable(value);
        service_param_save();
        break;

    case F_SLEEP:
        if(value > 1) { return; }
        g_service_param.sleep.sleep_mode = value;
        service_param_save();
        break;

    case F_AUTOWAKE:
        if(value > 1) { return; }
        g_service_param.sleep.sleep_auto = value;
        service_param_save();
        break;

    case F_WAKE_SEL:
        if(value >= WAKE_OPT_NUM) { return; }
        g_service_param.sleep.sleep_time = WAKE_OPTS_MIN[value];
        service_param_save();
        break;

    case F_BTMODE_SEL:
        if(value >= BTMODE_OPT_NUM) { return; }
        g_service_param.ble.ble_mode = value;
        service_param_save();
        break;

    case F_HB_SEL:
        if(value >= HB_OPT_NUM) { return; }
        g_service_param.network.film_heartbeat_interval = (uint8_t)HB_OPTS_SEC[value];
        service_param_save();
        break;

    default:
        return;   // 只读行不接受写入
    }

    sys_logi(APP_SET_TAG, "apply row=%u(%s) value=%u",
             (unsigned)row, ROWS[row].key, (unsigned)value);
}

/**
 * @brief 把当前快照下发给页面（下行动作，只投递不渲染）
 */
static void settings_snapshot_push(void)
{
    settings_snapshot_pull();
    (void)ui_core_post(APP_UI_MSG_SETTINGS_SNAPSHOT, &m_snap, (uint8_t)sizeof(m_snap));
}

/**
 * @brief 设置 app 的事件处理（app_task 上下文）
 *
 * 只处理页面发来的上行请求；其余事件（BLE/网络）本 app 不关心。
 */
static void settings_on_event(const app_event_t *e)
{
    if(e->type != APP_EVT_UI_MSG)
    {
        return;
    }

    if(e->cmd == APP_UI_REQ_SETTINGS_SYNC)
    {
        settings_snapshot_push();   // 页面刚建好，下发首帧真实值
        return;
    }

    if(e->cmd == APP_UI_REQ_SETTINGS_APPLY && e->len >= 2)
    {
        settings_apply(e->payload[0], e->payload[1]);
        /* 回刷以设备侧真实值为准：如 WiFi 打开后状态文案由 OFF 变 IDLE */
        settings_snapshot_push();
    }
}

/**
 * @brief 按快照与当前选择刷新整页
 *
 * 选中行 = 实心黑底 + 反白文字（1-bit 下最强的对比），并在行首给三角指针；
 * 只读行不给指针，明确传达"这行不可操作"。
 */
static void set_apply(void)
{
    uint8_t i;

    for(i = 0; i < SETTINGS_ROW_NUM; i++)
    {
        int sel = (i == m_sel);
        lv_color_t fg = sel ? lv_color_white() : lv_color_black();
        const settings_row_t *r = &ROWS[i];

        if(m_rows[i] == NULL)
        {
            continue;
        }

        lv_obj_set_style_bg_opa(m_rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_rows[i], lv_color_black(), LV_PART_MAIN);

        if(m_row_ptr[i] != NULL)
        {
            /* 只读行不给指针 */
            lv_obj_set_style_bg_opa(m_row_ptr[i], (sel && !row_is_ro(i)) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    LV_PART_MAIN);
        }
        if(m_row_val[i] != NULL)
        {
            lv_obj_set_style_text_color(m_row_val[i], fg, LV_PART_MAIN);
            if(r->kind == SET_KIND_TOGGLE)
            {
                lv_label_set_text(m_row_val[i], row_text(i));
            }
            else if(r->kind != SET_KIND_BAR)
            {
                lv_label_set_text(m_row_val[i], row_text(i));
            }
        }
        if(m_row_tog[i] != NULL)
        {
            /* 开关块：ON 实心（黑底白字 / 选中行白底黑字），OFF 空心描边 */
            uint8_t on = row_on(i);

            lv_obj_set_style_border_color(m_row_tog[i], fg, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m_row_tog[i], fg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_row_tog[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            if(m_row_val[i] != NULL)
            {
                lv_label_set_text(m_row_val[i], on ? "ON" : "OFF");
                /* ON 时文字要相对开关自身底色反色 */
                lv_obj_set_style_text_color(m_row_val[i],
                                            on ? (sel ? lv_color_black() : lv_color_white()) : fg,
                                            LV_PART_MAIN);
            }
        }
        if(m_row_st[i] != NULL)
        {
            /* 开关行左侧的状态文案（WiFi / 蓝牙）：取设备侧真实状态，随快照刷新 */
            lv_obj_set_style_text_color(m_row_st[i], fg, LV_PART_MAIN);
            lv_label_set_text(m_row_st[i], row_text(i));
        }
        if(m_row_fill[i] != NULL)
        {
            lv_obj_set_style_bg_color(m_row_fill[i], fg, LV_PART_MAIN);
            lv_obj_set_width(m_row_fill[i], (int32_t)(62 * m_snap.bat_pct / 100));
        }
    }

    if(m_row_idx_label != NULL)
    {
        lv_label_set_text_fmt(m_row_idx_label, "ROW %02u / %02u",
                              (unsigned)(m_sel + 1), (unsigned)SETTINGS_ROW_NUM);
    }
}

/**
 * @brief 跳到下一个可操作行（跳过只读行）
 */
static void set_move(int delta)
{
    uint8_t i = m_sel;
    uint8_t step;

    for(step = 0; step < SETTINGS_ROW_NUM; step++)
    {
        i = (uint8_t)((i + delta + SETTINGS_ROW_NUM) % SETTINGS_ROW_NUM);
        if(!row_is_ro(i))
        {
            m_sel = i;
            set_apply();
            return;
        }
    }
}

/**
 * @brief ENTER：切换开关 / 循环取值，并把结果上报给 app 任务落盘
 *
 * 页面只做"乐观更新"以即时反馈；真正的 g_service_param 写入由 app 任务串行执行
 * （service_param 模块无内部锁）。
 */
static void set_toggle(void)
{
    uint8_t req[2];
    const settings_row_t *r = &ROWS[m_sel];

    if(row_is_ro(m_sel))
    {
        return;
    }

    req[0] = m_sel;

    switch(r->field)
    {
    case F_WIFI_ON:     m_snap.wifi_on    = (uint8_t)!m_snap.wifi_on;    req[1] = m_snap.wifi_on;    break;
    case F_BT_ON:       m_snap.bt_on      = (uint8_t)!m_snap.bt_on;      req[1] = m_snap.bt_on;      break;
    case F_SLEEP:       m_snap.sleep_mode = (uint8_t)!m_snap.sleep_mode; req[1] = m_snap.sleep_mode; break;
    case F_AUTOWAKE:    m_snap.sleep_auto = (uint8_t)!m_snap.sleep_auto; req[1] = m_snap.sleep_auto; break;
    case F_WAKE_SEL:    m_snap.wake_sel   = (uint8_t)((m_snap.wake_sel + 1) % WAKE_OPT_NUM);
                        req[1] = m_snap.wake_sel;   break;
    case F_BTMODE_SEL:  m_snap.bt_mode_sel = (uint8_t)((m_snap.bt_mode_sel + 1) % BTMODE_OPT_NUM);
                        req[1] = m_snap.bt_mode_sel; break;
    case F_HB_SEL:      m_snap.hb_sel     = (uint8_t)((m_snap.hb_sel + 1) % HB_OPT_NUM);
                        req[1] = m_snap.hb_sel;     break;
    default:            return;
    }

    set_apply();
    app_manager_post_ui_msg(APP_UI_REQ_SETTINGS_APPLY, req, sizeof(req));
}

static void set_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *head;
    lv_obj_t *list;
    uint8_t i;
    uint8_t cur_sec = 0xFF;
    int32_t y = 0;

    sys_logi(APP_SET_TAG, "create settings page");

    /* 外壳：顶部状态栏 + 底部提示行（与主菜单同一套版式） */
    app_shell_build(root, "UP/DOWN SELECT   ENTER TOGGLE   HOLD ENTER EXIT",
                    "SETTINGS", &m_shell);

    /* 正文：夹在状态栏与提示行之间，左右留安全边距 */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);

    /* 表头 */
    head = lv_obj_create(body);
    lv_obj_remove_style_all(head);
    lv_obj_set_scrollable(head, false);
    lv_obj_set_size(head, LV_PCT(100), 16);
    lv_obj_set_pos(head, 0, 0);
    set_label(head, &lv_font_unscii_8, lv_color_black(), "SYSTEM CONFIGURATION");
    m_row_idx_label = set_label(head, &lv_font_unscii_8, lv_color_black(), "");
    lv_obj_align(m_row_idx_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_scrollable(list, false);
    lv_obj_set_size(list, LV_PCT(100), SET_LIST_H);
    lv_obj_set_pos(list, 0, 24);

    for(i = 0; i < SETTINGS_ROW_NUM; i++)
    {
        const settings_row_t *r = &ROWS[i];
        lv_obj_t *row;

        /* 分节标题：实心小标签 + 横线 */
        if(r->sec != cur_sec)
        {
            lv_obj_t *sh = lv_obj_create(list);
            lv_obj_t *tab;
            lv_obj_t *line;

            cur_sec = r->sec;
            lv_obj_remove_style_all(sh);
            lv_obj_set_scrollable(sh, false);
            lv_obj_set_size(sh, LV_PCT(100), 22);
            lv_obj_set_pos(sh, 0, y);
            y += 26;

            tab = lv_obj_create(sh);
            lv_obj_remove_style_all(tab);
            lv_obj_set_scrollable(tab, false);
            lv_obj_set_size(tab, 78, 18);
            lv_obj_set_style_bg_color(tab, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
            {
                lv_obj_t *t = set_label(tab, &lv_font_unscii_8, lv_color_white(),
                                        (cur_sec == 0) ? "DEVICE" : "PARAMETERS");
                lv_obj_center(t);
            }
            line = lv_obj_create(sh);
            lv_obj_remove_style_all(line);
            lv_obj_set_scrollable(line, false);
            lv_obj_set_size(line, 340, 1);
            lv_obj_set_style_bg_color(line, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_align(line, LV_ALIGN_LEFT_MID, 86, 0);
        }

        /* 行 */
        row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_black(), LV_PART_MAIN);
        y += ROW_H;

        m_rows[i] = row;

        /* 选择指针（只读行恒隐藏） */
        {
            lv_obj_t *p = lv_obj_create(row);
            lv_obj_remove_style_all(p);
            lv_obj_set_scrollable(p, false);
            lv_obj_set_size(p, 9, 12);
            lv_obj_set_style_bg_color(p, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_align(p, LV_ALIGN_LEFT_MID, 0, 0);
            m_row_ptr[i] = p;
        }

        {
            lv_obj_t *k = set_label(row, &lv_font_montserrat_14, lv_color_black(), r->key);
            lv_obj_set_pos(k, 14, 7);
            m_row_val[i] = k;   /* 键名标签先占位，取值标签下面重建 */
            m_row_val[i] = NULL;
        }

        if(r->kind == SET_KIND_BAR)
        {
            lv_obj_t *track = lv_obj_create(row);
            lv_obj_t *fill;

            lv_obj_remove_style_all(track);
            lv_obj_set_scrollable(track, false);
            lv_obj_set_size(track, 62, 9);
            lv_obj_set_style_border_width(track, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(track, lv_color_black(), LV_PART_MAIN);
            lv_obj_align(track, LV_ALIGN_RIGHT_MID, -54, 0);

            fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_scrollable(fill, false);
            lv_obj_set_size(fill, 30, 7);
            lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
            m_row_fill[i] = fill;

            m_row_val[i] = set_label(row, &lv_font_unscii_8, lv_color_black(), "");
            lv_obj_align(m_row_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
        }
        else if(r->kind == SET_KIND_TOGGLE)
        {
            lv_obj_t *tog = lv_obj_create(row);
            lv_obj_t *t;

            lv_obj_remove_style_all(tog);
            lv_obj_set_scrollable(tog, false);
            lv_obj_set_size(tog, 40, 18);
            lv_obj_set_style_border_width(tog, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(tog, lv_color_black(), LV_PART_MAIN);
            lv_obj_align(tog, LV_ALIGN_RIGHT_MID, 0, 0);
            m_row_tog[i] = tog;

            t = set_label(tog, &lv_font_unscii_8, lv_color_black(), "ON");
            lv_obj_center(t);
            m_row_val[i] = t;

            /* 开关行左侧的状态文案（WiFi / 蓝牙：状态 + 开关同一行） */
            if(ROWS[i].field == F_WIFI_ON || ROWS[i].field == F_BT_ON)
            {
                lv_obj_t *st = set_label(row, &lv_font_unscii_8, lv_color_black(), "");
                lv_obj_align(st, LV_ALIGN_RIGHT_MID, -48, 0);
                m_row_st[i] = st;   // 文字与颜色由 set_apply 按快照刷新
            }
        }
        else
        {
            m_row_val[i] = set_label(row, &lv_font_unscii_8, lv_color_black(), "");
            lv_obj_align(m_row_val[i], LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    set_apply();

    /* 首帧只是静态骨架（可见项为页面初始值）；真实设备状态由 app 任务侧采集后回投
       —— ui_task 不直接读 g_service_param / 电池 / WiFi，那些没有跨任务保证 */
    (void)app_manager_post_ui_msg(APP_UI_REQ_SETTINGS_SYNC, NULL, 0);
}

static void set_ui_destroy(void)
{
    memset(m_rows, 0, sizeof(m_rows));
    memset(m_row_fill, 0, sizeof(m_row_fill));
    memset(m_row_val, 0, sizeof(m_row_val));
    memset(m_row_tog, 0, sizeof(m_row_tog));
    memset(m_row_ptr, 0, sizeof(m_row_ptr));
    memset(m_row_st, 0, sizeof(m_row_st));
    m_row_idx_label = NULL;
    memset(&m_shell, 0, sizeof(m_shell));
    sys_logi(APP_SET_TAG, "destroy settings page");
}

static void set_ui_on_key(input_press_type_t key)
{
    switch(key)
    {
    case INPUT_PRESS_UP:    set_move(+1);   break;
    case INPUT_PRESS_DOWN:  set_move(-1);   break;
    case INPUT_PRESS_SHORT: set_toggle();   break;
    default:                                break;   // 长按由 app_manager 处理（退回主菜单）
    }
}

static void set_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_SETTINGS_SNAPSHOT && data != NULL && len == sizeof(settings_snapshot_t))
    {
        app_status_t st;

        memcpy(&m_snap, data, sizeof(m_snap));
        set_apply();

        /* 状态栏要的几项快照里都有，直接复用，省一次上行/下行往返 */
        st.bat_pct = m_snap.bat_pct;
        st.wifi_on = m_snap.wifi_on;
        st.wifi_conn = m_snap.wifi_conn;
        st.bt_on   = m_snap.bt_on;
        st.bt_conn = m_snap.bt_conn;
        app_shell_apply(&m_shell, &st);
        return;
    }

    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_settings_ui_ops = {
    .create  = set_ui_create,
    .destroy = set_ui_destroy,
    .on_msg  = set_ui_on_msg,
    .on_key  = set_ui_on_key,
};

const app_entry_t g_app_settings_entry = {
    .id = APP_ID_SETTINGS,
    .name = "settings",
    .data_dir = NULL,
    .keys = APP_KEY_UP | APP_KEY_DOWN | APP_KEY_SHORT,   // 长按不占用：留给"退出到主菜单"
    .tick_ms = 0,
    .events = NULL,
    .on_event = settings_on_event,
    .layer = APP_LAYER_UI,
    .ui_ops = &g_settings_ui_ops,
};
