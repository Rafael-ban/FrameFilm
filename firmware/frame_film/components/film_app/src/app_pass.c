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
 * FileName : /film_app/src/app_pass.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/21
 * Description: 通行证 app（UI 层占位页）：菜单有卡、进入后只提示"待开发"
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "sys_log.h"

#include "ui_ops.h"
#include "ui_assets.h"
#include "app_shell.h"
#include "app_pass.h"

/*********************************************************************
 * MACROS
 */
#define APP_PASS_TAG        "app_pass"

/* ---- 版式 ----
 * 正文是 flex 竖排居中：图标 → 大字 → 细线 → 两行说明。
 * 无交互，故没有行列坐标表；改文案不用动这里。 */
#define PASS_RULE_W         (240)    // 分隔线宽
#define PASS_WORD_LS        (8)      // "PASS" 字距（末字也会补一次，故要半格回正）

/* 待开发提示。固件字体无 CJK 字形，一律 ASCII。 */
#define PASS_NOTE_BIG       "UNDER DEVELOPMENT"
#define PASS_NOTE_SMALL     "PLACEHOLDER / NOT IMPLEMENTED YET"
#define PASS_FOOT_HINT      "DBL ENTER EXIT   HOLD SLEEP"

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_shell_t m_shell;      // 顶部状态栏 + 底部提示行

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void pass_ui_create(lv_obj_t *root);
static void pass_ui_destroy(void);
static void pass_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);

/**
 * @brief 建标签（字体/颜色显式指定：中途灰在 I1 下会被阈值化，结果不可预期）
 */
static lv_obj_t *pass_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

/**
 * @brief 通栏细线（居中、定宽）
 */
static void pass_rule(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, PASS_RULE_W, 1);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
}

static void pass_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *o;
    lv_obj_t *img;
    const void *src;

    sys_logi(APP_PASS_TAG, "create pass page (placeholder)");

    /* 外壳：顶部状态栏 + 底部提示行（与主菜单/时钟同一套版式） */
    app_shell_build(root, PASS_FOOT_HINT, "PASS", &m_shell);

    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 18, LV_PART_MAIN);

    /* 图标：与菜单卡片同一份资源（SD 可替换 / 缺失回退内置默认图） */
    img = lv_image_create(body);
    src = ui_assets_icon(UI_ICON_PASS);
    if(src != NULL)
    {
        lv_image_set_src(img, src);
    }

    /* 大字：与休眠卡的"状态字"同一套语汇（实心大写 + 字距） */
    o = pass_label(body, &lv_font_montserrat_48, lv_color_black(), "PASS");
    lv_obj_set_style_text_letter_space(o, PASS_WORD_LS, LV_PART_MAIN);
    /* LVGL 给末字也补一次字距，居中会整体偏左半格 —— 平移补回来 */
    lv_obj_set_style_translate_x(o, PASS_WORD_LS / 2, LV_PART_MAIN);

    pass_rule(body);

    (void)pass_label(body, &lv_font_unscii_16, lv_color_black(), PASS_NOTE_BIG);
    (void)pass_label(body, &lv_font_unscii_8, lv_color_black(), PASS_NOTE_SMALL);

    /* 状态栏的电量/WiFi/蓝牙由 app 任务侧采集后回投（页面不读服务层） */
    app_shell_request_status();
}

static void pass_ui_destroy(void)
{
    memset(&m_shell, 0, sizeof(m_shell));
    sys_logi(APP_PASS_TAG, "destroy pass page");
}

static void pass_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    /* 本页只有状态栏一条下行消息，其余交给外壳处理 */
    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_pass_ui_ops = {
    .create  = pass_ui_create,
    .destroy = pass_ui_destroy,
    .on_msg  = pass_ui_on_msg,
    .on_key  = NULL,
};

const app_entry_t g_app_pass_entry = {
    .id = APP_ID_PASS,
    .name = "pass",
    .data_dir = NULL,        // 占位页无数据目录
    .keys = APP_KEY_NONE,    // 不占用按键（双击退出/长按休眠由 app_manager 统一处理）
    .tick_ms = 0,            // UI 层不用 app_task 的 tick
    .events = NULL,

    /* UI 层：页面由 film_ui 在 ui_task 上下文创建，故 on_enter/on_exit/on_tick 均不参与。
       on_event 只服务顶部状态栏：app 任务侧采集电量/WiFi/蓝牙后回投。 */
    .layer = APP_LAYER_UI,
    .ui_ops = &g_pass_ui_ops,
    .on_event = app_shell_on_event,
};
