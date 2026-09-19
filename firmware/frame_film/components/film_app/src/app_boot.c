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
 * FileName : /film_app/src/app_boot.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: 开机画面（UI 页）：徽章 + 自检遥测 + 分段进度
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

#include "ui_assets.h"
#include "ui_conf.h"    /* UI_CALIB_FRAME：上机标定帧开关 */
#include "ui_ops.h"
#include "app_shell.h"
#include "app_boot.h"

/*********************************************************************
 * MACROS
 */
#define APP_BOOT_TAG        "app_boot"
#define BOOT_SEG_NUM        (24)     // 进度条分段数

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_seg[BOOT_SEG_NUM];
static lv_obj_t *m_pct_label = NULL;
static lv_obj_t *m_tele_ok[8] = {0};
static lv_obj_t *m_bar_fill = NULL;

/* 开机页不是 app（不参与切换、也没有 app 任务侧的 on_event），
   故状态栏取不到实时数据，保持初始的 "BAT --%" 与空指示块。
   真实数值在紧接着的主菜单页即刻可见。 */
static app_shell_t m_shell;

static app_boot_tele_t m_tele[8];
static uint8_t m_tele_num = 0;
static uint8_t m_step = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static lv_obj_t *boot_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt);

#if (UI_CALIB_FRAME == 1)
/**
 * @brief 上机标定帧（临时，见 ui_conf.h 的 UI_CALIB_FRAME）
 *
 * 三件事一次看懂：
 *   ① 边框四边是否都在 —— 都在=无位移；缺一边+对边多一条=环绕位移
 *   ② 顶部/左侧标尺的数字读偏移量 —— 判断是整体位移还是环绕位移，并读出像素数
 *   ③ 四角条数 1/2/3/4 是否与 TL/TR/BL/BR 对应 —— 复核旋转方向与镜像
 */
static void boot_ui_create_calib(lv_obj_t *body)
{
    /* 正文内容区：宽 = 480 - 2*20，高 = 720 - 状态栏30 - 提示行28 - 上下留白36 */
    const int32_t W = 440;
    const int32_t H = 626;
    const int32_t MARK_INSET = 14;
    static const char *const MARK[4] = { "TL", "TR", "BL", "BR" };
    /* 四角标记的落点（顺序与 MARK 一致）：1/2/3/4 条数 */
    const int32_t mx[4] = { MARK_INSET, W - MARK_INSET, MARK_INSET, W - MARK_INSET };
    const int32_t my[4] = { MARK_INSET, MARK_INSET, H - MARK_INSET - 34, H - MARK_INSET - 34 };
    int32_t b;
    int32_t i;

    /* ---- ① 边框（2px，贴正文四边） ---- */
    {
        lv_obj_t *box = lv_obj_create(body);

        lv_obj_remove_style_all(box);
        lv_obj_set_scrollable(box, false);
        lv_obj_set_size(box, W, H);
        lv_obj_set_pos(box, 0, 0);
        lv_obj_set_style_border_width(box, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(box, lv_color_black(), LV_PART_MAIN);
    }

    /* ---- ② 横向标尺：每 40 一格；每 80 加高并标"逻辑 x"（正文左边 = 逻辑 20） ---- */
    for(b = 0; b <= W; b += 40)
    {
        int big = ((b % 80) == 0);
        lv_obj_t *t = lv_obj_create(body);

        lv_obj_remove_style_all(t);
        lv_obj_set_scrollable(t, false);
        lv_obj_set_size(t, 2, big ? 16 : 8);
        lv_obj_set_pos(t, b, 4);
        lv_obj_set_style_bg_color(t, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);

        if(big)
        {
            char num[8];

            snprintf(num, sizeof(num), "%d", (int)(b + 20));
            {
                lv_obj_t *lbl = boot_label(body, &lv_font_unscii_8, lv_color_black(), num);

                lv_obj_set_pos(lbl, b + 4, 24);
            }
        }
    }

    /* ---- ③ 纵向标尺（贴左边框内侧）：每 40 一格；每 80 标注"逻辑 y" ----
     * 横向看水平偏移，纵向看垂直偏移，一张照片两个轴都能读。 */
    for(b = 80; b <= H - 26; b += 40)
    {
        int big = ((b % 80) == 0);
        lv_obj_t *t = lv_obj_create(body);

        lv_obj_remove_style_all(t);
        lv_obj_set_scrollable(t, false);
        lv_obj_set_size(t, big ? 16 : 8, 2);
        lv_obj_set_pos(t, 6, b);
        lv_obj_set_style_bg_color(t, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);

        if(big)
        {
            char num[8];

            snprintf(num, sizeof(num), "%d", (int)(b + SHELL_BODY_TOP));
            {
                lv_obj_t *lbl = boot_label(body, &lv_font_unscii_8, lv_color_black(), num);

                lv_obj_set_pos(lbl, 26, b - 6);
            }
        }
    }

    /* ---- ④ 四角标记：标签 + 1/2/3/4 条横杠 ---- */
    for(i = 0; i < 4; i++)
    {
        lv_obj_t *lbl = boot_label(body, &lv_font_unscii_8, lv_color_black(), MARK[i]);
        int32_t k;

        lv_obj_set_pos(lbl, mx[i], my[i]);
        for(k = 0; k <= i; k++)
        {
            lv_obj_t *bar = lv_obj_create(body);

            lv_obj_remove_style_all(bar);
            lv_obj_set_scrollable(bar, false);
            lv_obj_set_size(bar, 34, 5);
            lv_obj_set_pos(bar, mx[i], my[i] + 16 + k * 8);
            lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        }
    }

    /* ---- 中线：横向/纵向各一条，交点在正文中心 ---- */
    {
        lv_obj_t *h = lv_obj_create(body);
        lv_obj_t *v = lv_obj_create(body);

        lv_obj_remove_style_all(h);
        lv_obj_set_scrollable(h, false);
        lv_obj_set_size(h, W, 1);
        lv_obj_set_pos(h, 0, H / 2);
        lv_obj_set_style_bg_color(h, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_remove_style_all(v);
        lv_obj_set_scrollable(v, false);
        lv_obj_set_size(v, 1, H);
        lv_obj_set_pos(v, W / 2, 0);
        lv_obj_set_style_bg_color(v, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    }
}
#endif /* UI_CALIB_FRAME */

static void boot_ui_create(lv_obj_t *root);
static void boot_ui_destroy(void);
static void boot_ui_on_msg(uint32_t cmd, const void *data, uint8_t len);
#if (UI_CALIB_FRAME == 1)
static void boot_ui_create_calib(lv_obj_t *body);
#endif

static lv_obj_t *boot_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *boot_rule(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);

    lv_obj_remove_style_all(r);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_size(r, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(r, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    return r;
}

/**
 * @brief 按步骤刷新进度条与遥测行的 OK/--
 */
static void boot_apply_step(void)
{
    uint8_t on = (uint8_t)((uint32_t)BOOT_SEG_NUM * m_step / APP_BOOT_STEP_NUM);
    uint8_t i;

    for(i = 0; i < BOOT_SEG_NUM; i++)
    {
        if(m_seg[i] == NULL)
        {
            continue;
        }
        lv_obj_set_style_bg_color(m_seg[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_seg[i], (i < on) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }

    if(m_pct_label != NULL)
    {
        lv_label_set_text_fmt(m_pct_label, "%u%%", (unsigned)(on * 100u / BOOT_SEG_NUM));
    }

    for(i = 0; i < m_tele_num && i < 8; i++)
    {
        if(m_tele_ok[i] != NULL)
        {
            /* 第 i 步完成即该项 OK：单调推进，不回退 */
            lv_label_set_text(m_tele_ok[i], (m_step > i) ? "OK" : "--");
        }
    }
}

static void boot_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *badge;
    const void *src;
    lv_obj_t *segs;
    lv_obj_t *tele_box;
    uint8_t i;

    sys_logi(APP_BOOT_TAG, "create boot page, %u telemetry lines", (unsigned)m_tele_num);

    /* 外壳：与主菜单/设置页同一套版式（顶部状态栏 + 底部提示行） */
    app_shell_build(root, "MODEL " SYS_HAREWARE_VERSION " / FW " SYS_FIRMWARE_VERSION,
                    "BOOT", &m_shell);

    /* 正文：夹在状态栏与提示行之间，左右留安全边距 */
    body = lv_obj_create(root);
    lv_obj_remove_style_all(body);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(body, SHELL_BODY_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(body, SHELL_BODY_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(body, SHELL_BODY_PAD_X, LV_PART_MAIN);

#if (UI_CALIB_FRAME == 1)
    /* 临时标定帧：取代正常开机内容（见 ui_conf.h） */
    boot_ui_create_calib(body);
    return;
#endif

    /* ---- 顶部：自检序号 ---- */
    {
        lv_obj_t *row = lv_obj_create(body);

        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, LV_PCT(100), 14);
        lv_obj_set_pos(row, 0, 0);
        boot_label(row, &lv_font_unscii_8, lv_color_black(), "BOOT SEQ A-01");
    }
    {
        lv_obj_t *r = boot_rule(body);

        lv_obj_set_pos(r, 0, 22);
    }

    /* ---- 身份徽章（可由 SD 卡 /app/_ui/badge.bin 替换） ---- */
    src = ui_assets_badge();
    badge = lv_image_create(body);
    if(src != NULL)
    {
        lv_image_set_src(badge, src);
    }
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 44);

    /* ---- 品牌字标 ---- */
    {
        lv_obj_t *brand = boot_label(body, &lv_font_montserrat_48, lv_color_black(), "FRAMEFILM");

        lv_obj_set_style_text_letter_space(brand, 6, LV_PART_MAIN);
        lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 232);
    }
    {
        lv_obj_t *r = boot_rule(body);

        lv_obj_set_pos(r, 60, 300);
        lv_obj_set_width(r, 320);
    }
    {
        lv_obj_t *sub = boot_label(body, &lv_font_unscii_8, lv_color_black(), "COLOR E-PAPER TERMINAL");

        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 312);
    }

    /* ---- 分段进度条 ---- */
    segs = lv_obj_create(body);
    lv_obj_remove_style_all(segs);
    lv_obj_set_scrollable(segs, false);
    lv_obj_set_size(segs, LV_PCT(100), 10);
    lv_obj_set_pos(segs, 0, 356);
    lv_obj_set_flex_flow(segs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(segs, 2, LV_PART_MAIN);
    for(i = 0; i < BOOT_SEG_NUM; i++)
    {
        lv_obj_t *s = lv_obj_create(segs);

        lv_obj_remove_style_all(s);
        lv_obj_set_scrollable(s, false);
        lv_obj_set_height(s, 10);
        lv_obj_set_flex_grow(s, 1);
        lv_obj_set_style_border_width(s, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s, lv_color_black(), LV_PART_MAIN);
        m_seg[i] = s;
    }
    /* 进度行：左 "SYSTEM INIT" / 右 百分比（与设计稿一致） */
    {
        lv_obj_t *row = lv_obj_create(body);

        lv_obj_remove_style_all(row);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_size(row, LV_PCT(100), 14);
        lv_obj_set_pos(row, 0, 372);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        boot_label(row, &lv_font_unscii_8, lv_color_black(), "SYSTEM INIT");
        {
            lv_obj_t *sp = lv_obj_create(row);

            lv_obj_remove_style_all(sp);
            lv_obj_set_scrollable(sp, false);
            lv_obj_set_size(sp, 1, 1);
            lv_obj_set_flex_grow(sp, 1);
        }
        m_pct_label = boot_label(row, &lv_font_unscii_8, lv_color_black(), "0%");
    }

    /* ---- 自检遥测（左名 / 右值 / 尾 OK） ---- */
    tele_box = lv_obj_create(body);
    lv_obj_remove_style_all(tele_box);
    lv_obj_set_scrollable(tele_box, false);
    lv_obj_set_size(tele_box, LV_PCT(100), 120);
    lv_obj_set_pos(tele_box, 0, 400);
    for(i = 0; i < m_tele_num && i < 8; i++)
    {
        int32_t y = (int32_t)i * 22;

        lv_obj_t *n = boot_label(tele_box, &lv_font_unscii_8, lv_color_black(), m_tele[i].name);
        lv_obj_set_pos(n, 0, y);
        lv_obj_t *v = boot_label(tele_box, &lv_font_unscii_8, lv_color_black(), m_tele[i].value);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -34, y);
        m_tele_ok[i] = boot_label(tele_box, &lv_font_unscii_8, lv_color_black(), "--");
        lv_obj_align(m_tele_ok[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }

    boot_apply_step();
}

static void boot_ui_destroy(void)
{
    memset(m_seg, 0, sizeof(m_seg));
    memset(m_tele_ok, 0, sizeof(m_tele_ok));
    memset(&m_shell, 0, sizeof(m_shell));
    m_pct_label = NULL;
    m_bar_fill = NULL;
    sys_logi(APP_BOOT_TAG, "destroy boot page");
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
static const app_ui_ops_t g_boot_ui_ops = {
    .create  = boot_ui_create,
    .destroy = boot_ui_destroy,
    .on_msg  = boot_ui_on_msg,
    .on_key  = NULL,
};

const app_ui_ops_t *app_boot_ops(void)
{
    return &g_boot_ui_ops;
}

void app_boot_set_telemetry(const app_boot_tele_t *items, uint8_t count)
{
    if(items == NULL || count > 8)
    {
        return;
    }
    memcpy(m_tele, items, sizeof(app_boot_tele_t) * count);
    m_tele_num = count;
}

void app_boot_advance(uint8_t step)
{
    if(step > APP_BOOT_STEP_NUM)
    {
        step = APP_BOOT_STEP_NUM;
    }
    /* 本函数由 app_init 在 app_task 调用，**不能直接碰 lv_***：
       投递给 ui_task，由页面的 on_msg 落地 */
    (void)ui_core_post(APP_UI_MSG_BOOT_STEP, &step, 1);
}

static void boot_ui_on_msg(uint32_t cmd, const void *data, uint8_t len)
{
    if(cmd == APP_UI_MSG_BOOT_STEP && data != NULL && len >= 1)
    {
        m_step = ((const uint8_t *)data)[0];
        if(m_step > APP_BOOT_STEP_NUM)
        {
            m_step = APP_BOOT_STEP_NUM;
        }
        boot_apply_step();
    }
}
