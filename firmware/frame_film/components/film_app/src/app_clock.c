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
 * FileName : /film_app/src/app_clock.c
 * Author: Kiritro  Version: v0.2  Date: 2026/9/17
 * Description: 时钟 app（UI 框架层）：LVGL 竖屏页面显示 年月日 / 时分 / 星期
 * ChangeLog:
 *   v0.2  改为 UI 框架层实现（LVGL 页面），从"自绘 mono 帧"迁移
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <time.h>

#include "sys_log.h"

#include "ui_ops.h"     /* app_ui_ops_t（含 lvgl.h） */
#include "app_clock.h"

/*********************************************************************
 * MACROS
 */
#define APP_CLOCK_TAG       "app_clock"

/* 定时器周期（毫秒）：1s 一拍以便及时捕获"分钟跳变"。
 * 注意：只有内容真正变化时才会 set_text，LVGL 也只在 dirty 时渲染，
 * 因此实际约每分钟上屏一次，而不是每秒刷新 EPD。 */
#define CLOCK_TICK_MS       (1000)

/* 页面布局（逻辑竖屏 480x720，以屏幕中心为基准的纵向偏移） */
#define CLOCK_DATE_Y_OFS    (-140)
#define CLOCK_WEEK_Y_OFS    (140)

/*********************************************************************
 * CONSTANTS
 */
/* 星期缩写：不使用中文（LVGL 内置字体不含 CJK），用拉丁三字母 */
static const char *const WEEK_NAME[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t   *m_time_label = NULL;
static lv_obj_t   *m_date_label = NULL;
static lv_obj_t   *m_week_label = NULL;
static lv_timer_t *m_timer = NULL;

/* 上次刷新的"分钟/日"，用于抑制无谓的 set_text（每次上屏都是一次 EPD 全帧刷新） */
static int m_last_min  = -1;
static int m_last_mday = -1;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void clock_ui_create(lv_obj_t *root);
static void clock_ui_destroy(void);
static void clock_timer_cb(lv_timer_t *timer);
static void clock_ui_update(void);

/*********************************************************************
 * GLOBAL VARIABLES
 */
/**
 * @brief UI 页面契约
 *
 * 四个回调都在 ui_task 上下文执行（独占 LVGL），故可安全调用 lv_*。
 * 时钟页面无交互、无外部消息，on_msg / on_key 留空。
 */
static const app_ui_ops_t g_clock_ui_ops = {
    .create  = clock_ui_create,
    .destroy = clock_ui_destroy,
    .on_msg  = NULL,
    .on_key  = NULL,
};

const app_entry_t g_app_clock_entry = {
    .id = APP_ID_CLOCK,
    .name = "clock",
    .data_dir = NULL,        // 时钟为设备端生成，不占用文件目录
    .keys = APP_KEY_NONE,    // 不占用按键
    .tick_ms = 0,            // UI 层不用 app_task 的 tick（周期行为由页面内 lv_timer 承担）
    .events = NULL,

    /* UI 层：页面由 film_ui 在 ui_task 上下文创建，故 on_enter/on_exit/on_tick 均不参与 */
    .layer = APP_LAYER_UI,
    .ui_ops = &g_clock_ui_ops,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 按当前时间刷新三个标签
 *
 * 仅在"分钟"或"日"变化时更新：EPD 每次上屏都是全帧刷新，代价与改动面积无关，
 * 所以抑制无谓的 set_text 比减少控件重绘更重要。
 */
static void clock_ui_update(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    char buf[40];
    int wday;
    unsigned year, month, mday, hour, minute;

    if(m_time_label == NULL || m_date_label == NULL || m_week_label == NULL)
    {
        return;
    }

    localtime_r(&now, &tmv);

    if(tmv.tm_min == m_last_min && tmv.tm_mday == m_last_mday)
    {
        return;   // 显示内容未变，跳过
    }
    m_last_min  = tmv.tm_min;
    m_last_mday = tmv.tm_mday;

    /* 按字段宽度收敛值域：本地时间字段本不会越界，但 -Wformat-truncation 按 int 全域
       （每项最多 11 字节）推演，会判定 snprintf 可能截断；本项目开了 -Werror，直接编译失败。
       掩码后 GCC 可推导出上界；缓冲另按最保守推演留足余量，双保险。 */
    year   = (unsigned)(tmv.tm_year + 1900) & 0xFFFFu;   // ≤ 5 位
    month  = (unsigned)(tmv.tm_mon + 1)     & 0xFFu;     // ≤ 3 位
    mday   = (unsigned)tmv.tm_mday          & 0xFFu;     // ≤ 3 位
    hour   = (unsigned)tmv.tm_hour          & 0xFFu;     // ≤ 3 位
    minute = (unsigned)tmv.tm_min           & 0xFFu;     // ≤ 3 位

    snprintf(buf, sizeof(buf), "%02u:%02u", hour, minute);
    lv_label_set_text(m_time_label, buf);

    snprintf(buf, sizeof(buf), "%04u-%02u-%02u", year, month, mday);
    lv_label_set_text(m_date_label, buf);

    wday = (tmv.tm_wday >= 0 && tmv.tm_wday < 7) ? tmv.tm_wday : 0;
    lv_label_set_text(m_week_label, WEEK_NAME[wday]);

    sys_logi(APP_CLOCK_TAG, "clock update: %04u-%02u-%02u %s %02u:%02u",
             year, month, mday, WEEK_NAME[wday], hour, minute);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    clock_ui_update();
}

/**
 * @brief 创建时间标签（统一字体/颜色，避免主题的中间灰被 I1 阈值化后不可预期）
 */
static lv_obj_t *clock_make_label(lv_obj_t *root, const lv_font_t *font, int32_t y_ofs)
{
    lv_obj_t *label = lv_label_create(root);

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_ofs);

    return label;
}

static void clock_ui_create(lv_obj_t *root)
{
    sys_logi(APP_CLOCK_TAG, "create clock page");

    /* 竖屏 480x720：上部日期、中部时间（最大字号）、下部星期 */
    m_date_label = clock_make_label(root, &lv_font_montserrat_24, CLOCK_DATE_Y_OFS);
    lv_label_set_text(m_date_label, "----.--.--");

    m_time_label = clock_make_label(root, &lv_font_montserrat_48, 0);
    lv_label_set_text(m_time_label, "--:--");

    m_week_label = clock_make_label(root, &lv_font_montserrat_24, CLOCK_WEEK_Y_OFS);
    lv_label_set_text(m_week_label, "---");

    /* 立即填一次真实时间，避免首帧停留在占位文本 */
    m_last_min  = -1;
    m_last_mday = -1;
    clock_ui_update();

    /* 周期行为由页面内的 LVGL 定时器承担：app_task 完全不参与，零跨线程 */
    m_timer = lv_timer_create(clock_timer_cb, CLOCK_TICK_MS, NULL);
    if(m_timer == NULL)
    {
        sys_loge(APP_CLOCK_TAG, "create timer failed, clock will not tick");
    }
}

static void clock_ui_destroy(void)
{
    sys_logi(APP_CLOCK_TAG, "destroy clock page");

    /* 定时器不在 root 的对象树里，必须自己删；标签由 ui_core 随 root 一并删除 */
    if(m_timer != NULL)
    {
        lv_timer_delete(m_timer);
        m_timer = NULL;
    }

    m_time_label = NULL;
    m_date_label = NULL;
    m_week_label = NULL;
    m_last_min  = -1;
    m_last_mday = -1;
}
