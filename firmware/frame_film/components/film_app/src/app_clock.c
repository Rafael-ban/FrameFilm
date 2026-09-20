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
 * Author: Kiritro  Version: v0.3  Date: 2026/9/19
 * Description: 时钟 app（UI 框架层）：计时仪表 —— 主读数 / 分钟尺 / 星期寄存器 / 设备面板
 * ChangeLog:
 *   v0.2  改为 UI 框架层实现（LVGL 页面），从"自绘 mono 帧"迁移
 *   v0.3  按设计稿重做版式（tools/ui-mockup §07）；接入 app_shell 状态栏
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>
#include <time.h>

#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_epd.h"    /* EPD_WIDTH / EPD_HEIGHT / EPD_PANEL_ID（面板行用） */

#include "ui_ops.h"     /* app_ui_ops_t（含 lvgl.h） */
#include "ui_conf.h"    /* UI_LOGICAL_W：正文宽度按屏宽算，不写死 */
#include "app_shell.h"  /* 顶部状态栏 + 底部提示行 */
#include "app_clock.h"

/*********************************************************************
 * MACROS
 */
#define APP_CLOCK_TAG       "app_clock"

/* 定时器周期（毫秒）：1s 一拍以便及时捕获"分钟跳变"。
 * 注意：只有内容真正变化时才会 set_text，LVGL 也只在 dirty 时渲染，
 * 因此实际约每分钟上屏一次，而不是每秒刷新 EPD。 */
#define CLOCK_TICK_MS       (1000)

/* 实际刷新节奏（秒）：由"只在分钟/日变化时上屏"推导 —— 约 60s 一帧。
 * 面板行里显示的就是这个值，别拿 CLOCK_TICK_MS 去除 —— 那是轮询周期，不是刷新周期。 */
#define CLOCK_REFRESH_S     (60)

/* ---- 版式 ----
 * 全部是"正文区内"的 y / h，与设计稿 tools/ui-mockup §07 的表格逐行对应
 * （正文区 = 屏宽 − 2×SHELL_BODY_PAD_X，屏高 − SHELL_BODY_TOP − SHELL_BODY_BOTTOM
 *  − 上下留白 = 440 × 626）。改这里就等于改那一节的表。 */
#define CK_HEADER_Y         (0)
#define CK_HEADER_H         (14)
#define CK_RULE1_Y          (22)
#define CK_HERO_Y           (56)
#define CK_HERO_H           (104)
#define CK_GAUGE_Y          (176)
#define CK_GAUGE_H          (12)
#define CK_CAP_Y            (196)
#define CK_CAP_H            (14)
#define CK_RULE2_Y          (236)
#define CK_DATE_Y           (264)
#define CK_DATE_H           (56)
#define CK_RULE3_Y          (344)
#define CK_WEEK_Y           (368)
#define CK_WEEK_H           (64)
#define CK_WEEKCAP_Y        (442)
#define CK_PANEL_Y          (486)
#define CK_PANEL_H          (104)

/* 分钟尺：30 格 × 2 分钟。取 2 分钟一格而不是 1 分钟一格 ——
 * 格数一多格宽就只剩几像素，且尺子每分钟都要动（每次动都是一次全屏刷新）。 */
#define CK_SEG_NUM          (30)

/* 星期寄存器 */
#define CK_WK_NUM           (7)

/* 日期行里的星期黑标与它的左下切角 */
#define CK_TAB_W            (112)
#define CK_TAB_H            (44)
#define CK_CUT              (9)

/* 主读数两侧的实心三角 */
#define CK_TRI_W            (16)
#define CK_TRI_H            (22)

/* 设备面板四角取景标记 */
#define CK_MARK_L           (16)    // 臂长
#define CK_MARK_T           (2)     // 线宽

/* 面板文字行距与首行偏移 */
#define CK_PANEL_PITCH      (22)
#define CK_PANEL_Y0         (4)

/*********************************************************************
 * CONSTANTS
 */
/* 星期缩写：不使用中文（LVGL 内置字体不含 CJK），用拉丁三字母 */
static const char *const WK_CODE[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static app_shell_t m_shell;

static lv_obj_t *m_hm_label    = NULL;   // 主读数 "14:32"
static lv_obj_t *m_min_label   = NULL;   // "MIN 32 / 060"
static lv_obj_t *m_hour_label  = NULL;   // "HOUR 14"
static lv_obj_t *m_doy_label   = NULL;   // "DOY 262 / 365"
static lv_obj_t *m_week_label  = NULL;   // "WEEK 38 / 52"
static lv_obj_t *m_date_label  = NULL;   // "2026-09-19"
static lv_obj_t *m_wday_label  = NULL;   // 黑标里的 "SAT"

static lv_obj_t *m_seg[CK_SEG_NUM] = {0};      // 分钟尺
static lv_obj_t *m_wk_cell[CK_WK_NUM] = {0};   // 星期寄存器格子
static lv_obj_t *m_wk_code[CK_WK_NUM] = {0};   // 格子里的三字母（高亮时要反白）

static lv_timer_t *m_timer = NULL;

/* 上次刷新的"分钟/日"，用于抑制无谓的 set_text（每次上屏都是一次 EPD 全帧刷新） */
static int m_last_min  = -1;
static int m_last_mday = -1;

/* 三角位图的像素缓冲（L8：0x00 = 黑，0xFF = 白）。
   页面重进时重算一遍即可 —— 面积合计不到 800 像素，代价可忽略。 */
static uint8_t m_tri_r_px[CK_TRI_W * CK_TRI_H];
static uint8_t m_tri_l_px[CK_TRI_W * CK_TRI_H];
static uint8_t m_cut_px[CK_CUT * CK_CUT];
static lv_image_dsc_t m_tri_r_dsc;
static lv_image_dsc_t m_tri_l_dsc;
static lv_image_dsc_t m_cut_dsc;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void clock_ui_create(lv_obj_t *root);
static void clock_ui_destroy(void);
static void clock_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);
static void clock_timer_cb(lv_timer_t *timer);
static void clock_ui_update(void);

static lv_obj_t *clock_label(lv_obj_t *parent, const lv_font_t *font,
                             lv_color_t color, const char *txt);
static lv_obj_t *clock_box(lv_obj_t *parent, int32_t y, int32_t h);
static lv_obj_t *clock_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
static lv_obj_t *clock_hline(lv_obj_t *parent);
static lv_obj_t *clock_gap(lv_obj_t *parent);
static void clock_row_flex(lv_obj_t *row, int32_t pad_col);
static void clock_rule(lv_obj_t *parent, int32_t y);
static void clock_marks(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
static void clock_tri_px(uint8_t *buf, int32_t w, int32_t h,
                         int32_t ax, int32_t ay, int32_t bx, int32_t by,
                         int32_t cx, int32_t cy);
static void clock_dsc_init(lv_image_dsc_t *dsc, uint8_t *buf, uint16_t w, uint16_t h);
static unsigned clock_days_in_year(unsigned y);

/*********************************************************************
 * GLOBAL VARIABLES
 */
/**
 * @brief UI 页面契约
 *
 * 三个回调都在 ui_task 上下文执行（独占 LVGL），故可安全调用 lv_*。
 * 本页无交互，on_key 留空（按键由 app_manager 吞掉，不往下传）。
 */
static const app_ui_ops_t g_clock_ui_ops = {
    .create  = clock_ui_create,
    .destroy = clock_ui_destroy,
    .on_msg  = clock_ui_on_msg,
    .on_key  = NULL,
};

const app_entry_t g_app_clock_entry = {
    .id = APP_ID_CLOCK,
    .name = "clock",
    .data_dir = NULL,        // 时钟为设备端生成，不占用文件目录
    .keys = APP_KEY_NONE,    // 不占用按键
    .tick_ms = 0,            // UI 层不用 app_task 的 tick（周期行为由页面内 lv_timer 承担）
    .events = NULL,

    /* UI 层：页面由 film_ui 在 ui_task 上下文创建，故 on_enter/on_exit/on_tick 均不参与。
       on_event 只服务顶部状态栏：app 任务侧采集电量/WiFi/蓝牙后回投（页面不读服务层）。 */
    .layer = APP_LAYER_UI,
    .ui_ops = &g_clock_ui_ops,
    .on_event = app_shell_on_event,
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 建标签（统一字体/颜色，避免主题的中间灰被 I1 阈值化后不可预期）
 */
static lv_obj_t *clock_label(lv_obj_t *parent, const lv_font_t *font,
                             lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

/**
 * @brief 建一条"正文行"容器：整宽、指定 y / h、无样式
 */
static lv_obj_t *clock_box(lv_obj_t *parent, int32_t y, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, LV_PCT(100), h);
    lv_obj_set_pos(o, 0, y);
    return o;
}

/**
 * @brief 建一个实心黑块（角标、刻度、分隔线都用它）
 */
static lv_obj_t *clock_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

/**
 * @brief 行内"弹性横线"：把同一行的两端内容撑开（表头 / 尺注用它）
 */
static lv_obj_t *clock_hline(lv_obj_t *parent)
{
    lv_obj_t *o = clock_bar(parent, 0, 0, 0, 1);

    lv_obj_set_flex_grow(o, 1);
    return o;
}

/**
 * @brief 行内"弹性空档"：只撑开、不画线（尺注 / 寄存器注用它）
 */
static lv_obj_t *clock_gap(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, 0, 1);
    lv_obj_set_flex_grow(o, 1);
    return o;
}

static void clock_row_flex(lv_obj_t *row, int32_t pad_col)
{
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, pad_col, LV_PART_MAIN);
}

/**
 * @brief 通栏 1px 分隔线
 */
static void clock_rule(lv_obj_t *parent, int32_t y)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, LV_PCT(100), 1);
    lv_obj_set_pos(o, 0, y);
    lv_obj_set_style_bg_color(o, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
}

/**
 * @brief 取景框四角：每角两条短臂（叠在面板边框的四个角上，把"读数面板"框起来）
 *
 * 不画成整圈是因为设计语汇要的是"角标"，且 4 个角只需 8 个细块。
 */
static void clock_marks(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint8_t i;

    for(i = 0; i < 4; i++)
    {
        int left = (i == 0 || i == 2);
        int top  = (i == 0 || i == 1);
        int32_t hx = left ? x : (x + w - CK_MARK_L);
        int32_t hy = top ? y : (y + h - CK_MARK_T);
        int32_t vx = left ? x : (x + w - CK_MARK_T);
        int32_t vy = top ? y : (y + h - CK_MARK_L);

        (void)clock_bar(parent, hx, hy, CK_MARK_L, CK_MARK_T);
        (void)clock_bar(parent, vx, vy, CK_MARK_T, CK_MARK_L);
    }
}

/**
 * @brief 把三角形光栅化进 L8 缓冲（顶点用**像素坐标 ×2**表示，避开半像素的浮点）
 *
 * LVGL 没有"填充三角形"图元：CSS 那套"零宽边框拼三角"在 LVGL 里没有等价物
 * （边框宽度是全边统一的，不能按边指定），而实心三角恰好是这套视觉语言的核心符号，
 * 所以自带一个最简光栅化 —— 逐像素做三次叉积定号判断。
 *
 * 只在页面创建时跑一次，最大面积 16×22 = 352 像素，代价可忽略。
 *
 * @param buf 输出缓冲（w × h，L8：三角内 0x00 黑，三角外 0xFF 白）
 */
static void clock_tri_px(uint8_t *buf, int32_t w, int32_t h,
                         int32_t ax, int32_t ay, int32_t bx, int32_t by,
                         int32_t cx, int32_t cy)
{
    int32_t x;
    int32_t y;

    for(y = 0; y < h; y++)
    {
        for(x = 0; x < w; x++)
        {
            int32_t px = 2 * x + 1;      /* 像素中心 */
            int32_t py = 2 * y + 1;
            int32_t e1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
            int32_t e2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
            int32_t e3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
            int inside = ((e1 >= 0) && (e2 >= 0) && (e3 >= 0))
                      || ((e1 <= 0) && (e2 <= 0) && (e3 <= 0));

            buf[(uint32_t)y * (uint32_t)w + (uint32_t)x] = inside ? 0x00 : 0xFF;
        }
    }
}

/**
 * @brief 组装 L8 图像描述（与 ui_assets 的做法一致：亮度直接表达黑白，无调色板语义）
 */
static void clock_dsc_init(lv_image_dsc_t *dsc, uint8_t *buf, uint16_t w, uint16_t h)
{
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_L8;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = w;
    dsc->data_size = (uint32_t)w * (uint32_t)h;
    dsc->data = buf;
}

/**
 * @brief 该年的天数（表头 DOY 的分母；闰年 366）
 *
 * 只是个显示用的读数，但分母错了会一眼看出来，所以老实算。
 */
static unsigned clock_days_in_year(unsigned y)
{
    if((y % 4u == 0u) && ((y % 100u != 0u) || (y % 400u == 0u)))
    {
        return 366u;
    }
    return 365u;
}

/**
 * @brief 按当前时间刷新整页读数
 *
 * 仅在"分钟"或"日"变化时更新：EPD 每次上屏都是全帧刷新、代价与改动面积无关，
 * 所以抑制无谓的 set_text 比减少控件重绘更重要。
 */
static void clock_ui_update(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    uint8_t i;
    unsigned year, month, mday, hour, minute, wday, yday;
    uint8_t seg_on;

    if(m_hm_label == NULL)
    {
        return;   // 页面未建成
    }

    localtime_r(&now, &tmv);

    if(tmv.tm_min == m_last_min && tmv.tm_mday == m_last_mday)
    {
        return;   // 显示内容未变，跳过（否则每秒一次全屏刷新）
    }
    m_last_min  = tmv.tm_min;
    m_last_mday = tmv.tm_mday;

    /* 按字段宽度收敛值域：本地时间字段本不会越界，但 -Wformat-truncation 按 int 全域
       （每项最多 11 字节）推演，会判定 lv_label_set_text_fmt 可能截断；本项目开了 -Werror。
       掩码后 GCC 可推导出上界。 */
    year   = (unsigned)(tmv.tm_year + 1900) & 0xFFFFu;
    month  = (unsigned)(tmv.tm_mon + 1)     & 0xFFu;
    mday   = (unsigned)tmv.tm_mday          & 0xFFu;
    hour   = (unsigned)tmv.tm_hour          & 0xFFu;
    minute = (unsigned)tmv.tm_min           & 0xFFu;
    wday   = ((tmv.tm_wday >= 0) && (tmv.tm_wday < 7)) ? (unsigned)tmv.tm_wday : 0u;
    yday   = (unsigned)tmv.tm_yday & 0x1FFu;    // 0 基

    /* ---- 主读数 + 尺注 ---- */
    lv_label_set_text_fmt(m_hm_label, "%02u:%02u", hour, minute);
    if(m_min_label != NULL)
    {
        lv_label_set_text_fmt(m_min_label, "MIN %02u / 060", minute);
    }
    if(m_hour_label != NULL)
    {
        lv_label_set_text_fmt(m_hour_label, "HOUR %02u", hour);
    }

    /* ---- 分钟尺：每 2 分钟点亮一格 ---- */
    seg_on = (uint8_t)(minute / 2u);
    for(i = 0; i < CK_SEG_NUM; i++)
    {
        if(m_seg[i] != NULL)
        {
            lv_obj_set_style_bg_opa(m_seg[i], (i < seg_on) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                    LV_PART_MAIN);
        }
    }

    /* ---- 星期黑标 + 日期 ---- */
    if(m_wday_label != NULL)
    {
        lv_label_set_text(m_wday_label, WK_CODE[wday]);
    }
    if(m_date_label != NULL)
    {
        lv_label_set_text_fmt(m_date_label, "%04u-%02u-%02u", year, month, mday);
    }

    /* ---- 星期寄存器：今日格实心 + 文字反白 ---- */
    for(i = 0; i < CK_WK_NUM; i++)
    {
        int cur = ((unsigned)i == wday);

        if(m_wk_cell[i] != NULL)
        {
            lv_obj_set_style_border_width(m_wk_cell[i], cur ? 3 : 1, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_wk_cell[i], cur ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        }
        if(m_wk_code[i] != NULL)
        {
            lv_obj_set_style_text_color(m_wk_code[i], cur ? lv_color_white() : lv_color_black(),
                                        LV_PART_MAIN);
        }
    }

    /* ---- 表头 / 寄存器注：年内序号与自然周 ---- */
    if(m_doy_label != NULL)
    {
        lv_label_set_text_fmt(m_doy_label, "DOY %03u / %03u",
                              yday + 1u, clock_days_in_year(year));
    }
    if(m_week_label != NULL)
    {
        /* 自然周（非 ISO 周）：按 7 天切分，年末最多到 53，显示上按 52 饱和 */
        unsigned wk = yday / 7u + 1u;

        if(wk > 52u)
        {
            wk = 52u;
        }
        lv_label_set_text_fmt(m_week_label, "WEEK %02u / 52", wk);
    }

    sys_logi(APP_CLOCK_TAG, "clock update: %04u-%02u-%02u %s %02u:%02u",
             year, month, mday, WK_CODE[wday], hour, minute);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    clock_ui_update();
}

static void clock_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *row;
    lv_obj_t *img;
    uint8_t i;

    sys_logi(APP_CLOCK_TAG, "create clock page");

    /* 三角与切角位图：▶ / ◀ 与星期黑标的左下角 */
    clock_tri_px(m_tri_r_px, CK_TRI_W, CK_TRI_H, 0, 0, 0, 2 * CK_TRI_H, 2 * CK_TRI_W, CK_TRI_H);
    clock_tri_px(m_tri_l_px, CK_TRI_W, CK_TRI_H, 2 * CK_TRI_W, 0, 2 * CK_TRI_W, 2 * CK_TRI_H, 0, CK_TRI_H);
    clock_tri_px(m_cut_px, CK_CUT, CK_CUT, 0, 0, 0, 2 * CK_CUT, 2 * CK_CUT, 2 * CK_CUT);
    clock_dsc_init(&m_tri_r_dsc, m_tri_r_px, CK_TRI_W, CK_TRI_H);
    clock_dsc_init(&m_tri_l_dsc, m_tri_l_px, CK_TRI_W, CK_TRI_H);
    clock_dsc_init(&m_cut_dsc, m_cut_px, CK_CUT, CK_CUT);

    /* ============ 外壳：顶部状态栏 + 底部提示行 ============ */
    app_shell_build(root, "DBL ENTER EXIT   HOLD SLEEP", "CLOCK", &m_shell);

    /* ============ 正文：夹在状态栏与提示行之间 ============ */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);

    /* ---- 表头：TIMEKEEPING ──────── DOY 262 / 365 ---- */
    row = clock_box(body, CK_HEADER_Y, CK_HEADER_H);
    clock_row_flex(row, 8);
    (void)clock_label(row, &lv_font_unscii_8, lv_color_black(), "TIMEKEEPING");
    (void)clock_hline(row);
    m_doy_label = clock_label(row, &lv_font_unscii_8, lv_color_black(), "DOY --- / ---");
    clock_rule(body, CK_RULE1_Y);

    /* ---- 主读数：两侧实心三角把视线压向中心 ---- */
    row = clock_box(body, CK_HERO_Y, CK_HERO_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, LV_PART_MAIN);
    img = lv_image_create(row);
    lv_image_set_src(img, &m_tri_r_dsc);
    m_hm_label = clock_label(row, &lv_font_montserrat_48, lv_color_black(), "--:--");
    lv_obj_set_style_text_letter_space(m_hm_label, 3, LV_PART_MAIN);
    img = lv_image_create(row);
    lv_image_set_src(img, &m_tri_l_dsc);

    /* ---- 分钟尺：30 格，每格 2 分钟 ---- */
    row = clock_box(body, CK_GAUGE_Y, CK_GAUGE_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 2, LV_PART_MAIN);
    for(i = 0; i < CK_SEG_NUM; i++)
    {
        lv_obj_t *s = lv_obj_create(row);

        lv_obj_remove_style_all(s);
        lv_obj_set_scrollable(s, false);
        lv_obj_set_height(s, CK_GAUGE_H);
        lv_obj_set_flex_grow(s, 1);
        lv_obj_set_style_border_width(s, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, LV_PART_MAIN);
        m_seg[i] = s;
    }

    /* ---- 尺注：MIN 32 / 060 ····· HOUR 14 ---- */
    row = clock_box(body, CK_CAP_Y, CK_CAP_H);
    clock_row_flex(row, 8);
    m_min_label = clock_label(row, &lv_font_unscii_8, lv_color_black(), "MIN -- / 060");
    (void)clock_gap(row);
    m_hour_label = clock_label(row, &lv_font_unscii_8, lv_color_black(), "HOUR --");
    clock_rule(body, CK_RULE2_Y);

    /* ---- 日期行：星期黑标（左下切角）+ 日期 ---- */
    row = clock_box(body, CK_DATE_Y, CK_DATE_H);
    clock_row_flex(row, 16);
    {
        lv_obj_t *tab = lv_obj_create(row);

        lv_obj_remove_style_all(tab);
        lv_obj_set_scrollable(tab, false);
        lv_obj_set_size(tab, CK_TAB_W, CK_TAB_H);
        lv_obj_set_style_bg_color(tab, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);

        m_wday_label = clock_label(tab, &lv_font_unscii_8, lv_color_white(), "---");
        lv_obj_set_style_text_letter_space(m_wday_label, 4, LV_PART_MAIN);
        lv_obj_center(m_wday_label);

        /* 左下切角：压一个与底色同色的三角上去。
           LVGL 没有 clip-path 式的切角图元，这是唯一能用基础图元做出"切削"的办法。 */
        img = lv_image_create(tab);
        lv_image_set_src(img, &m_cut_dsc);
        lv_obj_set_pos(img, 0, CK_TAB_H - CK_CUT);
    }
    m_date_label = clock_label(row, &lv_font_montserrat_24, lv_color_black(), "----.--.--");
    clock_rule(body, CK_RULE3_Y);

    /* ---- 星期寄存器：7 格等宽，今日实心 ---- */
    row = clock_box(body, CK_WEEK_Y, CK_WEEK_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);
    for(i = 0; i < CK_WK_NUM; i++)
    {
        lv_obj_t *c = lv_obj_create(row);

        lv_obj_remove_style_all(c);
        lv_obj_set_scrollable(c, false);
        lv_obj_set_height(c, CK_WEEK_H);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(c, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(c, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
        m_wk_code[i] = clock_label(c, &lv_font_unscii_8, lv_color_black(), WK_CODE[i]);
        lv_obj_center(m_wk_code[i]);
        m_wk_cell[i] = c;
    }

    /* ---- 寄存器注：WEEK REGISTER ····· WEEK 38 / 52 ---- */
    row = clock_box(body, CK_WEEKCAP_Y, CK_CAP_H);
    clock_row_flex(row, 8);
    (void)clock_label(row, &lv_font_unscii_8, lv_color_black(), "WEEK REGISTER");
    (void)clock_gap(row);
    m_week_label = clock_label(row, &lv_font_unscii_8, lv_color_black(), "WEEK -- / 52");

    /* ---- 设备面板：1px 描边 + 四角取景标记 + 三行只读遥测 ----
       内容全是编译期常量（面板/固件版本）或设计常量（刷新节奏），
       不引入任何跨线程读数 —— 页面只读得到时间。 */
    row = clock_box(body, CK_PANEL_Y, CK_PANEL_H);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 12, LV_PART_MAIN);
    {
        uint16_t panel_w = (uint16_t)EPD_WIDTH;
        uint16_t panel_h = (uint16_t)EPD_HEIGHT;
        uint8_t  panel_id = (uint8_t)EPD_PANEL_ID;
        lv_obj_t *l = clock_label(row, &lv_font_unscii_8, lv_color_black(), "");

        lv_label_set_text_fmt(l, "PANEL     E6 %ux%u ID%02X",
                              (unsigned)panel_w, (unsigned)panel_h, (unsigned)panel_id);
        lv_obj_set_pos(l, 0, CK_PANEL_Y0);
    }
    {
        lv_obj_t *l = clock_label(row, &lv_font_unscii_8, lv_color_black(), "");

        lv_label_set_text_fmt(l, "FIRMWARE  %s %s", SYS_FIRMWARE_VERSION, SYS_HAREWARE_VERSION);
        lv_obj_set_pos(l, 0, CK_PANEL_Y0 + CK_PANEL_PITCH);
    }
    {
        lv_obj_t *l = clock_label(row, &lv_font_unscii_8, lv_color_black(), "");

        /* 刷新节奏见 CLOCK_REFRESH_S：只在"分钟/日"变化时上屏 */
        lv_label_set_text_fmt(l, "REFRESH   MONO FULL FRAME / %u S", (unsigned)CLOCK_REFRESH_S);
        lv_obj_set_pos(l, 0, CK_PANEL_Y0 + 2 * CK_PANEL_PITCH);
    }
    clock_marks(body, 0, CK_PANEL_Y, UI_LOGICAL_W - 2 * SHELL_BODY_PAD_X, CK_PANEL_H);

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

    /* 状态栏的电量/WiFi/蓝牙由 app 任务侧采集后回投（页面不读服务层） */
    app_shell_request_status();
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

    m_hm_label   = NULL;
    m_min_label  = NULL;
    m_hour_label = NULL;
    m_doy_label  = NULL;
    m_week_label = NULL;
    m_date_label = NULL;
    m_wday_label = NULL;
    memset(m_seg, 0, sizeof(m_seg));
    memset(m_wk_cell, 0, sizeof(m_wk_cell));
    memset(m_wk_code, 0, sizeof(m_wk_code));
    memset(&m_shell, 0, sizeof(m_shell));
    m_last_min  = -1;
    m_last_mday = -1;
}

static void clock_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    /* 本页只有状态栏一条下行消息，其余交给外壳处理 */
    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}
