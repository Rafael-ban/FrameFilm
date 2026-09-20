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
 * FileName : /film_app/src/app_shell.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/19
 * Description: UI 页公共外壳：顶部状态栏（品牌 + 电量/WiFi/蓝牙）+ 底部提示行
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>

#include "sys_log.h"
#include "hal_bat.h"
#include "service_param.h"
#include "service_wifi.h"      /* WiFi 连接状态（状态栏第二档） */
#include "service_ble_gatts.h" /* 蓝牙连接状态 */

#include "ui_core.h"
#include "app_shell.h"
#include "app_manager.h"

/*********************************************************************
 * MACROS
 */
#define APP_SHELL_TAG       "app_shell"

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static lv_obj_t *shell_label(lv_obj_t *parent, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

/**
 * @brief 弹性空档（把同一 flex 行里的元素推到两端）
 */
static void shell_spacer(lv_obj_t *parent)
{
    lv_obj_t *s = lv_obj_create(parent);

    lv_obj_remove_style_all(s);
    lv_obj_set_scrollable(s, false);
    lv_obj_set_size(s, 1, 1);
    lv_obj_set_flex_grow(s, 1);
}

/**
 * @brief 状态指示块（9x9 方块）
 *
 * 1-bit 面板没有灰度，"亮/暗"只能靠"实心/描边"表达。三档状态：
 *   关闭   → 整项（文字 + 方块）都不显示
 *   已开启 → 文字 + 空框（描边）
 *   已连接 → 文字 + 实心框
 */
static lv_obj_t *shell_pip(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);

    lv_obj_remove_style_all(p);
    lv_obj_set_scrollable(p, false);
    lv_obj_set_size(p, 9, 9);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(p, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, LV_PART_MAIN);
    return p;
}

/**
 * @brief 落一个指示项的三档状态（文字 + 方块一起管）
 */
static void shell_ind_set(lv_obj_t *label, lv_obj_t *pip, uint8_t on, uint8_t conn)
{
    if(label != NULL)
    {
        /* 用 HIDDEN 而不是透明：隐藏的 flex 子项不占位，关闭时右侧直接收成 "BAT 82%" */
        if(on)
        {
            lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(pip == NULL)
    {
        return;
    }

    if(!on)
    {
        lv_obj_add_flag(pip, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(pip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(pip, conn ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void app_shell_build(lv_obj_t *root, const char *hint, const char *page, app_shell_t *out)
{
    lv_obj_t *status;
    lv_obj_t *foot;

    if(root == NULL)
    {
        return;
    }
    if(out != NULL)
    {
        out->bat_label = NULL;
        out->bat_pip = NULL;
        out->wifi_label = NULL;
        out->wifi_pip = NULL;
        out->bt_label = NULL;
        out->bt_pip = NULL;
    }

    /* ============ 顶部状态栏：品牌 + 电量 / WiFi / 蓝牙 ============ */
    status = lv_obj_create(root);
    lv_obj_remove_style_all(status);
    lv_obj_set_scrollable(status, false);
    lv_obj_set_size(status, LV_PCT(100), SHELL_STATUS_H);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(status, SHELL_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_border_side(status, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(status, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, LV_PART_MAIN);
    {
        /* 品牌标记：1-bit 无灰度，用实心方块代替设计稿的三角 */
        lv_obj_t *mark = lv_obj_create(status);

        lv_obj_remove_style_all(mark);
        lv_obj_set_scrollable(mark, false);
        lv_obj_set_size(mark, 7, 7);
        lv_obj_set_style_bg_color(mark, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, LV_PART_MAIN);
    }
    {
        lv_obj_t *brand = shell_label(status, lv_color_black(), "ENDFIELD");

        lv_obj_set_style_text_letter_space(brand, 1, LV_PART_MAIN);
    }
    shell_spacer(status);
    {
        lv_obj_t *bat = shell_label(status, lv_color_black(), "BAT --%");
        lv_obj_t *bat_pip = shell_pip(status);
        lv_obj_t *wifi_label;
        lv_obj_t *wifi_pip;
        lv_obj_t *bt_label;
        lv_obj_t *bt_pip;

        wifi_label = shell_label(status, lv_color_black(), "WIFI");
        wifi_pip = shell_pip(status);
        bt_label = shell_label(status, lv_color_black(), "BT");
        bt_pip = shell_pip(status);

        if(out != NULL)
        {
            out->bat_label = bat;
            out->bat_pip = bat_pip;
            out->wifi_label = wifi_label;
            out->wifi_pip = wifi_pip;
            out->bt_label = bt_label;
            out->bt_pip = bt_pip;
        }
    }

    /* ============ 底部提示行：操作提示 + 页面名 ============ */
    foot = lv_obj_create(root);
    lv_obj_remove_style_all(foot);
    lv_obj_set_scrollable(foot, false);
    lv_obj_set_size(foot, LV_PCT(100), SHELL_FOOT_H);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(foot, SHELL_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(foot, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(foot, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    shell_label(foot, lv_color_black(), (hint != NULL) ? hint : "");
    shell_spacer(foot);
    shell_label(foot, lv_color_black(), (page != NULL) ? page : "");
}

void app_shell_request_status(void)
{
    (void)app_manager_post_ui_msg(APP_UI_REQ_STATUS_SYNC, NULL, 0);
}

void app_shell_apply(app_shell_t *s, const app_status_t *st)
{
    if(s == NULL || st == NULL)
    {
        return;
    }

    if(s->bat_label != NULL)
    {
        lv_label_set_text_fmt(s->bat_label, "BAT %u%%", (unsigned)st->bat_pct);
    }
    /* 电量块只有"实心/留空"两态（低电量留空），不参与隐藏逻辑 */
    if(s->bat_pip != NULL)
    {
        lv_obj_set_style_bg_opa(s->bat_pip, (st->bat_pct > 20) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }
    shell_ind_set(s->wifi_label, s->wifi_pip, st->wifi_on, st->wifi_conn);
    shell_ind_set(s->bt_label, s->bt_pip, st->bt_on, st->bt_conn);
}

int app_shell_handle_msg(app_shell_t *s, uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd != APP_UI_MSG_STATUS || data == NULL || len < sizeof(app_status_t))
    {
        return 0;
    }

    app_shell_apply(s, (const app_status_t *)data);
    return 1;
}

void app_shell_on_event(const app_event_t *e)
{
    app_status_t st;
    int level;

    if(e->type != APP_EVT_UI_MSG || e->cmd != APP_UI_REQ_STATUS_SYNC)
    {
        return;
    }

    level = hal_bat_get_percent();
    if(level < 0)
    {
        level = 0;
    }
    if(level > 100)
    {
        level = 100;
    }
    st.bat_pct = (uint8_t)level;
    st.wifi_on = g_service_param.network.wifi_enable ? 1 : 0;
    st.bt_on   = g_service_param.ble.ble_enable ? 1 : 0;
    /* 只有"开着"才去问"连上没有"：栈没起时那两个查询只会返回未连接，
       但"关着"与"连着"在状态栏是两档不同的表现，别让它们混起来 */
    st.wifi_conn = (st.wifi_on && service_wifi_get_connect_status()) ? 1 : 0;
    st.bt_conn   = (st.bt_on && service_ble_gatts_get_connect()) ? 1 : 0;

    (void)ui_core_post(APP_UI_MSG_STATUS, &st, (uint8_t)sizeof(st));
}
