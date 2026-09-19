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
 * FileName : /film_app/src/app_menu.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: 主菜单（UI 层）：横向轮播选择 app，单击进入
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <string.h>

#include "sys_log.h"

#include "ui_assets.h"
#include "ui_conf.h"    /* UI_LOGICAL_W/H：正文宽度按屏宽算，不写死 */
#include "ui_ops.h"
#include "app_shell.h"
#include "app_menu.h"
/*********************************************************************
 * MACROS
 */
#define APP_MENU_TAG        "app_menu"

/* 版式（逻辑竖屏 480x720，与 tools/ui-mockup 的 .device 同尺寸，故设计稿数值可 1:1 照搬）
 *
 * 外壳（状态栏 / 提示行 / 正文留白）见 app_shell.h；本页只排"正文"里的行。
 * 正文行宽 = 屏宽 - 两侧安全边距；轮播那一行例外，取整屏宽好让卡片延伸到边缘。 */
#define CONTENT_W           (UI_LOGICAL_W - 2 * SHELL_BODY_PAD_X)
#define SHELL_W             (UI_LOGICAL_W)

#define ROW_HEADER_H        (14)     // SELECT APPLICATION --- 01 / 05
#define CAROUSEL_H          (290)    // 轮播（比当前卡片高，给上下留呼吸）
#define ROW_DOTS_H          (5)      // 指示点
#define DOTS_CUR_W          (26)     // 当前项指示点宽（实心）
#define DOTS_SIDE_W         (18)     // 其余指示点宽（描边）
#define DOTS_PAD_TOP        (8)
#define DOTS_PAD_BOT        (20)
#define ROW_RULE_H          (1)
#define ROW_ACTIVE_H        (64)     // ACTIVE 行（含上下 14 内边距；名字用 Montserrat 24，行高约 29px）
#define ROW_DESC_H          (23)
#define ROW_SPACER_H        (16)
#define ROW_PANEL_H         (68)     // APP/ENTRY 面板

/* 轮播卡片：侧卡 132x200、当前卡 178x238、间距 10。
 * 5 张卡片总宽 746 > 屏宽，两侧被容器边界裁掉——这正是设计稿"继续延伸"的观感。 */
#define PLATE_CUR_W         (178)
#define PLATE_CUR_H         (238)
#define PLATE_SIDE_W        (132)
#define PLATE_SIDE_H        (200)
#define PLATE_GAP           (10)
#define PLATE_SPAN          (2)      // 当前项左右各画 2 张（共 5 张，正好覆盖全部条目）

/*********************************************************************
 * CONSTANTS
 */
/* 轮播内容表。**顺序必须与 app_manager 的 m_menu_entries 一致**（选择索引即此表下标）。
 * 文案全部为 ASCII：固件字体只有 Montserrat 与 UNSCII，无 CJK 字形。 */
static const menu_item_t MENU_ITEMS[APP_MENU_ENTRY_NUM] = {
    { "IMAGE",     "IMG", "SD PHOTO WALL / UP-DOWN PAGING",        "DIRECT", "MONO SESSION REBUILD", UI_ICON_IMAGE     },
    { "TEMPLATE",  "TPL", "LIVE PUSH CONTENT / WEATHER, CALENDAR", "DIRECT", "MONO SESSION REBUILD", UI_ICON_TEMPLATE  },
    { "CLOCK",     "CLK", "DEVICE TIME / DATE AND WEEKDAY",        "UI",     "UI PAGE (LOW COST)",   UI_ICON_CLOCK     },
    { "ANIMATION", "ANI", "FRAME ANIMATION / ADJUSTABLE RATE",     "DIRECT", "MONO SESSION REBUILD", UI_ICON_ANIMATION },
    { "SETTINGS",  "SET", "DEVICE INFO AND SYSTEM PARAMETERS",     "UI",     "UI PAGE (LOW COST)",   UI_ICON_SETTINGS  },
};

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_carousel = NULL;   // 轮播容器（选中项变化时整块重建）
static lv_obj_t *m_dots[APP_MENU_ENTRY_NUM] = {0};  // 指示点（选中项变化时改宽/填充）
static lv_obj_t *m_idx_label = NULL;  // "03 / 05"
static lv_obj_t *m_name_label = NULL; // ACTIVE 后面的 app 名
static lv_obj_t *m_desc_label = NULL;
static lv_obj_t *m_panel_line1 = NULL;
static lv_obj_t *m_panel_line2 = NULL;
static app_shell_t m_shell;           // 顶部状态栏（电量/WiFi/蓝牙）的动态控件
static uint8_t   m_sel = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void menu_ui_create(lv_obj_t *root);
static void menu_ui_destroy(void);
static void menu_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);

/* 统一的小工具：本工程是 1-bit 面板，颜色只有黑/白，故一切颜色显式指定，
   不依赖主题的中间灰（I1 下中间灰会被亮度阈值化，结果不可预期）。 */
static lv_obj_t *menu_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *menu_rule(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);

    lv_obj_remove_style_all(r);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_size(r, LV_PCT(100), ROW_RULE_H);
    lv_obj_set_style_bg_color(r, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    return r;
}

/**
 * @brief 建一块轮播卡片
 *
 * @param idx 条目下标
 * @param cur 是否当前选中项（实心黑底 + 反白图标 + 反白文字 + 加粗描边）
 */
static lv_obj_t *menu_make_plate(lv_obj_t *parent, uint8_t idx, int cur)
{
    const menu_item_t *it = &MENU_ITEMS[idx];
    lv_color_t fg = cur ? lv_color_white() : lv_color_black();
    lv_obj_t *plate = lv_obj_create(parent);
    lv_obj_t *grp;
    lv_obj_t *img;
    const void *src;

    lv_obj_remove_style_all(plate);
    lv_obj_set_scrollable(plate, false);
    lv_obj_set_size(plate, cur ? PLATE_CUR_W : PLATE_SIDE_W,
                           cur ? PLATE_CUR_H : PLATE_SIDE_H);
    lv_obj_set_style_border_width(plate, cur ? 3 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(plate, lv_color_black(), LV_PART_MAIN);
    if(cur)
    {
        lv_obj_set_style_bg_color(plate, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, LV_PART_MAIN);
        /* 选中项左下角切一刀（终末地的"切削"语汇） */
        lv_obj_set_style_clip_corner(plate, true, LV_PART_MAIN);
    }

    /* 左上角编号 */
    {
        lv_obj_t *n = menu_label(plate, &lv_font_unscii_8, fg, "");

        lv_label_set_text_fmt(n, "%02u", (unsigned)(idx + 1));
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 8, 6);
    }

    /* 图标 + 短码：竖排居中（选中项用反色版，保证黑底上可见） */
    grp = lv_obj_create(plate);
    lv_obj_remove_style_all(grp);
    lv_obj_set_scrollable(grp, false);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grp, 12, LV_PART_MAIN);

    src = cur ? ui_assets_icon_inverted(it->icon) : ui_assets_icon(it->icon);
    img = lv_image_create(grp);
    if(src != NULL)
    {
        lv_image_set_src(img, src);
    }
    menu_label(grp, &lv_font_unscii_8, fg, it->code);
    lv_obj_center(grp);

    return plate;
}

/**
 * @brief 重建轮播：当前项 ±PLATE_SPAN，循环取模
 *
 * 5 个条目取 ±2 正好覆盖全部（不重复），故不需要"两端空位"——
 * 超出的部分由屏幕边界自然裁掉，形成设计稿的"继续延伸"观感。
 */
static void menu_rebuild_carousel(void)
{
    uint8_t i;

    if(m_carousel == NULL)
    {
        return;
    }

    lv_obj_clean(m_carousel);
    for(i = 0; i < (uint8_t)(PLATE_SPAN * 2 + 1); i++)
    {
        int off = (int)i - PLATE_SPAN;                                    /* -2..+2 */
        uint8_t idx = (uint8_t)((m_sel + APP_MENU_ENTRY_NUM + off) % APP_MENU_ENTRY_NUM);

        menu_make_plate(m_carousel, idx, (off == 0) ? 1 : 0);
    }
}

/**
 * @brief 把当前选中项同步到所有控件
 */
static void menu_apply_sel(void)
{
    const menu_item_t *it = &MENU_ITEMS[m_sel];
    uint8_t i;

    menu_rebuild_carousel();

    /* 指示点：当前项 26x5 实心，其余 18x5 描边。宽度变化会影响 flex 重排，
       所以"高亮"是改宽度 + 填充两件事，都要跟着 m_sel 走。 */
    for(i = 0; i < APP_MENU_ENTRY_NUM; i++)
    {
        if(m_dots[i] == NULL)
        {
            continue;
        }
        lv_obj_set_width(m_dots[i], (i == m_sel) ? DOTS_CUR_W : DOTS_SIDE_W);
        lv_obj_set_style_bg_opa(m_dots[i], (i == m_sel) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }

    if(m_idx_label != NULL)
    {
        lv_label_set_text_fmt(m_idx_label, "%02u / %02u",
                              (unsigned)(m_sel + 1), (unsigned)APP_MENU_ENTRY_NUM);
    }
    if(m_name_label != NULL)
    {
        lv_label_set_text(m_name_label, it->name);
    }
    if(m_desc_label != NULL)
    {
        lv_label_set_text(m_desc_label, it->desc);
    }
    if(m_panel_line1 != NULL)
    {
        lv_label_set_text_fmt(m_panel_line1, "APP %02u   LAYER %s",
                              (unsigned)(m_sel + 1), it->layer);
    }
    if(m_panel_line2 != NULL)
    {
        lv_label_set_text_fmt(m_panel_line2, "ENTRY %s", it->entry);
    }
}

/*********************************************************************
 * 页面契约
 */

static void menu_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *stack;
    lv_obj_t *header;
    lv_obj_t *dots;
    lv_obj_t *tab;
    lv_obj_t *panel;
    uint8_t i;

    sys_logi(APP_MENU_TAG, "create menu page (%u entries)", (unsigned)APP_MENU_ENTRY_NUM);

    /* ============ 外壳：顶部状态栏 + 底部提示行 ============ */
    app_shell_build(root, "UP/DOWN SELECT   ENTER OPEN", "MENU", &m_shell);

    /* ============ 正文：夹在状态栏与提示行之间 ============ */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    /* 左右不留内边距：正文各行自带 CONTENT_W 宽度并居中，
       这样轮播才能横向延伸到屏幕边缘（由容器边界裁掉超出的卡片） */

    /* 正文按列排：每行高度与设计稿一致 */
    stack = lv_obj_create(body);
    lv_obj_remove_style_all(stack);
    lv_obj_set_scrollable(stack, false);
    lv_obj_set_size(stack, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stack, 0, LV_PART_MAIN);

    /* ---- 表头：SELECT APPLICATION ──── 01 / 05 ---- */
    header = lv_obj_create(stack);
    lv_obj_remove_style_all(header);
    lv_obj_set_scrollable(header, false);
    lv_obj_set_size(header, CONTENT_W, ROW_HEADER_H);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);
    menu_label(header, &lv_font_unscii_8, lv_color_black(), "SELECT APPLICATION");
    {
        /* 中间的弹性横线：宽度全交给 flex 分配（显式给宽度会与分配到的空间叠加而溢出） */
        lv_obj_t *sp = menu_rule(header, lv_color_black());

        lv_obj_set_width(sp, 0);
        lv_obj_set_flex_grow(sp, 1);
    }
    m_idx_label = menu_label(header, &lv_font_unscii_8, lv_color_black(), "01 / 05");

    /* ---- 轮播：当前项 ±2 共 5 张。容器取整屏宽，超出的卡片由容器边界裁掉 ---- */
    m_carousel = lv_obj_create(stack);
    lv_obj_remove_style_all(m_carousel);
    lv_obj_set_scrollable(m_carousel, false);
    lv_obj_set_size(m_carousel, SHELL_W, CAROUSEL_H);
    lv_obj_set_flex_flow(m_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_carousel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_carousel, PLATE_GAP, LV_PART_MAIN);

    /* ---- 指示点（当前 26x5 实心，其余 18x5 描边） ---- */
    dots = lv_obj_create(stack);
    lv_obj_remove_style_all(dots);
    lv_obj_set_scrollable(dots, false);
    lv_obj_set_size(dots, CONTENT_W, ROW_DOTS_H);
    lv_obj_set_style_margin_top(dots, DOTS_PAD_TOP, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(dots, DOTS_PAD_BOT, LV_PART_MAIN);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, LV_PART_MAIN);
    for(i = 0; i < APP_MENU_ENTRY_NUM; i++)
    {
        lv_obj_t *d = lv_obj_create(dots);

        lv_obj_remove_style_all(d);
        lv_obj_set_scrollable(d, false);
        lv_obj_set_size(d, (i == m_sel) ? DOTS_CUR_W : DOTS_SIDE_W, ROW_DOTS_H);
        lv_obj_set_style_border_width(d, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(d, lv_color_black(), LV_PART_MAIN);
        /* 底色一律先写好，只靠 opa 开关填充：这样 menu_apply_sel() 里
           来回切高亮时不用再设颜色 */
        lv_obj_set_style_bg_color(d, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, (i == m_sel) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        m_dots[i] = d;
    }

    /* ---- 分隔线 ---- */
    {
        lv_obj_t *r = menu_rule(stack, lv_color_black());

        lv_obj_set_width(r, CONTENT_W);
    }

    /* ---- ACTIVE + app 名 ---- */
    {
        lv_obj_t *row = lv_obj_create(stack);

        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, CONTENT_W, ROW_ACTIVE_H);
        lv_obj_set_style_pad_ver(row, 14, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);

        tab = lv_obj_create(row);
        lv_obj_remove_style_all(tab);
        lv_obj_set_scrollable(tab, false);
        lv_obj_set_size(tab, 62, 22);
        lv_obj_set_style_bg_color(tab, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
        {
            lv_obj_t *t = menu_label(tab, &lv_font_unscii_8, lv_color_white(), "ACTIVE");

            lv_obj_center(t);
        }
        m_name_label = menu_label(row, &lv_font_montserrat_24, lv_color_black(), "IMAGE");
    }

    /* ---- 一句话说明 ---- */
    m_desc_label = menu_label(stack, &lv_font_unscii_8, lv_color_black(), "");
    lv_obj_set_size(m_desc_label, CONTENT_W, ROW_DESC_H);

    /* ---- 间隙 ---- */
    {
        lv_obj_t *sp = lv_obj_create(stack);

        lv_obj_remove_style_all(sp);
        lv_obj_set_scrollable(sp, false);
        lv_obj_set_size(sp, CONTENT_W, ROW_SPACER_H);
    }

    /* ---- 层级/进入代价面板 ---- */
    panel = lv_obj_create(stack);
    lv_obj_remove_style_all(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_size(panel, CONTENT_W, ROW_PANEL_H);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 10, LV_PART_MAIN);

    m_panel_line1 = menu_label(panel, &lv_font_unscii_8, lv_color_black(), "");
    lv_obj_set_pos(m_panel_line1, 0, 4);
    m_panel_line2 = menu_label(panel, &lv_font_unscii_8, lv_color_black(), "");
    lv_obj_set_pos(m_panel_line2, 0, 22);

    menu_apply_sel();

    /* 状态栏的电量/WiFi/蓝牙由 app 任务侧采集后回投（页面不读服务层） */
    app_shell_request_status();
}

static void menu_ui_destroy(void)
{
    sys_logi(APP_MENU_TAG, "destroy menu page");

    m_carousel = NULL;
    memset(m_dots, 0, sizeof(m_dots));
    m_idx_label = NULL;
    m_name_label = NULL;
    m_desc_label = NULL;
    m_panel_line1 = NULL;
    m_panel_line2 = NULL;
    memset(&m_shell, 0, sizeof(m_shell));
}

static void menu_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_MENU_SEL && data != NULL && len >= 1)
    {
        uint8_t sel = ((const uint8_t *)data)[0];

        if(sel < APP_MENU_ENTRY_NUM)
        {
            /* 与 app_manager 的选择索引对齐：值相同也要落一次（初次进入） */
            m_sel = sel;
            menu_apply_sel();
        }
        return;
    }

    (void)app_shell_handle_msg(&m_shell, cmd, data, len);
}

/*********************************************************************
 * GLOBAL VARIABLES
 */
static const app_ui_ops_t g_menu_ui_ops = {
    .create  = menu_ui_create,
    .destroy = menu_ui_destroy,
    .on_msg  = menu_ui_on_msg,
    .on_key  = NULL,     // 按键由 app_manager（app 任务）统一裁决，页面只负责显示
};

const app_entry_t g_app_menu_entry = {
    .id = APP_ID_MENU,
    .name = "menu",
    .data_dir = NULL,
    .keys = APP_KEY_NONE,
    .tick_ms = 0,
    .events = NULL,
    .on_event = app_shell_on_event,   // 只服务状态栏（上行采集 → 回投）
    .layer = APP_LAYER_UI,
    .ui_ops = &g_menu_ui_ops,
};
