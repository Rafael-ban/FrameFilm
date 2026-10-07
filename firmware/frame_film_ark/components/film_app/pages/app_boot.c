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
 * FileName : /film_app/pages/app_boot.c
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
#include "ui_fonts.h"
#include "ui_boot_typography.h"
#include "app_shell.h"
#include "app_language.h"
#include "app_manager.h"    /* app_manager_post_ui_msg：进度走完上报切页 */
#include "app_boot.h"
#include "hal_epd.h"        /* 黑白快刷会话：进度条逐格刷新期间帧间保持上电 */

/*********************************************************************
 * MACROS
 */
#define APP_BOOT_TAG        "app_boot"

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_obj_t *m_seg[APP_BOOT_SEG_NUM];
static lv_obj_t *m_pct_label = NULL;
static lv_obj_t *m_tele_ok[8] = {0};

/* 开机页不是 app（不参与切换、也没有 app 任务侧的 on_event），
   故状态栏取不到实时数据，保持初始的 "--%" 与空指示块。
   真实数值在紧接着的主菜单页即刻可见。
   居中时间是唯一例外：它由 app_shell 自己问 libc 得到，不需要跨任务采集
   （此时多半还没校时，会显示 "--:--"）。 */
static app_shell_t m_shell;

static app_boot_tele_t m_tele[8];
static uint8_t m_tele_num = 0;

/* 进度：每次回调**只点亮一格**，保证一格一格走（见 app_boot.h 的节拍说明） */
static lv_timer_t *m_prog_timer = NULL;
static uint8_t m_seg_on = 0;      // 已点亮格数
static uint8_t m_done_left = 0;   // 走满后还要停留的节拍数（建页时装载 APP_BOOT_DONE_HOLD_TICKS）

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

static const char *boot_tele_name(const char *name)
{
    if(strcmp(name, "PANEL") == 0) return app_text("屏幕", "PANEL");
    if(strcmp(name, "PSRAM") == 0 || strcmp(name, "MEMORY") == 0) return app_text("内存", "MEMORY");
    if(strcmp(name, "STORAGE") == 0) return app_text("存储", "STORAGE");
    if(strcmp(name, "FIRMWARE") == 0) return app_text("固件", "FIRMWARE");
    if(strcmp(name, "RADIO") == 0) return app_text("无线", "RADIO");
    if(strcmp(name, "WIFI") == 0) return app_text("网络", "WIFI");
    if(strcmp(name, "BLE") == 0) return app_text("蓝牙", "BLE");
    return name;
}

static const char *boot_tele_value(const char *value, char *buf, size_t len)
{
    unsigned kb;
    int end = 0;

    if(strcmp(value, "SD MOUNTED") == 0) return app_text("已挂载", "MOUNTED");
    if(strcmp(value, "NO SD") == 0) return app_text("未插卡", "NO SD");
    if(sscanf(value, "%u KB FREE%n", &kb, &end) == 1 && end > 0 && value[end] == '\0')
    {
        snprintf(buf, len, app_text("%u KB 可用", "%u KB FREE"), kb);
        return buf;
    }
    return value;
}

/**
 * @brief 按"已点亮格数"刷新进度条、百分比标签与遥测行的 OK/--
 *
 * 入参是**格数**而不是百分比：两者若来回换算（格数→pct→格数）会因为整除
 * 丢精度，第 1 格要点亮时算出来的 on 仍是 0（如 16 格时 1 格 = 6%，
 * 16×6/100 = 0），看起来像"卡了一格才动"。
 *
 * @param seg_on 已点亮格数 0~APP_BOOT_SEG_NUM
 */
static void boot_apply_progress(uint8_t seg_on)
{
    uint32_t pct;
    uint8_t i;

    if(seg_on > APP_BOOT_SEG_NUM)
    {
        seg_on = APP_BOOT_SEG_NUM;
    }
    pct = (uint32_t)seg_on * 100u / APP_BOOT_SEG_NUM;

    for(i = 0; i < APP_BOOT_SEG_NUM; i++)
    {
        if(m_seg[i] == NULL)
        {
            continue;
        }
        lv_obj_set_style_bg_color(m_seg[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_seg[i], (i < seg_on) ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }

    if(m_pct_label != NULL)
    {
        lv_label_set_text_fmt(m_pct_label, "%u%%", (unsigned)pct);
    }

    for(i = 0; i < m_tele_num && i < 8; i++)
    {
        if(m_tele_ok[i] != NULL)
        {
            /* 各项依次点亮：单调推进，不回退 */
            uint32_t due = (uint32_t)(i + 1) * 100u / (uint32_t)m_tele_num;

            lv_label_set_text(m_tele_ok[i], (pct >= due) ? app_text("完成", "OK") : app_text("等待", "--"));
        }
    }
}

/**
 * @brief 进度推进回调（ui_task 上下文）
 *
 * 每次回调**只点亮一格**，且节拍（APP_BOOT_STEP_MS）不小于 mono 单帧耗时，
 * 所以画面上永远是一格一格走，不会一次跳两格。
 * 走满后停留 APP_BOOT_DONE_HOLD_TICKS 个节拍，再上报"可以进主菜单"——由**页面**决定切页
 * 时机，而不是在 app_init 里用一个固定延时去猜首帧清场那 ~3.3s。
 */
static void boot_prog_cb(lv_timer_t *timer)
{
    (void)timer;

    if(m_seg_on < APP_BOOT_SEG_NUM)
    {
        m_seg_on++;
        boot_apply_progress(m_seg_on);
        return;
    }

    /* 走满后按配置停留几个节拍再上报（配 0 = 立刻上报）。
       用"剩余节拍"倒数而不是"已停留 >= N"：后者在 N 为 0 时对 uint8_t 恒真，
       会被 -Wtype-limits 判成无意义比较。 */
    if(m_done_left > 0)
    {
        m_done_left--;
        return;
    }

    /* 先删定时器再上报：避免上报后到切页之间又触发一轮 */
    lv_timer_delete(m_prog_timer);
    m_prog_timer = NULL;
    (void)app_manager_post_ui_msg(APP_UI_REQ_BOOT_DONE, NULL, 0);
}

static void boot_ui_create(lv_obj_t *root)
{
    lv_obj_t *body;
    lv_obj_t *badge;
    lv_obj_t *segs;
    lv_obj_t *tele_box;
    uint8_t i;
    /* logo 与彩蛋占位共用固定高度，后续内容由其底部推算。 */
    const int32_t badge_top = 44;
    const int32_t badge_bot = badge_top + APP_SHELL_BRAND_H;

    sys_logi(APP_BOOT_TAG, "create boot page, %u telemetry lines", (unsigned)m_tele_num);

    /* 进度条是"一个会话内逐格刷新"：打开黑白快刷会话，让每格只付一次波形刷新
       （约 200ms）而不是再各加一趟 PON/POF（约 200ms）。销毁页面时关会话断电。 */
    hal_epd_mono_session_begin();

    /* 外壳：与主菜单/设置页同一套版式（顶部状态栏 + 底部提示行） */
    app_shell_build(root, app_text("机型 " SYS_HAREWARE_VERSION " / 固件 " SYS_FIRMWARE_VERSION,
                                   "MODEL " SYS_HAREWARE_VERSION " / FW " SYS_FIRMWARE_VERSION),
                    app_text("启动", "BOOT"), &m_shell);

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
        lv_obj_set_size(row, LV_PCT(100), 22);
        lv_obj_set_pos(row, 0, 0);
        boot_label(row, &ui_font_14, lv_color_black(), app_text("启动序列 A-01", "BOOT SEQ A-01"));
    }
    {
        lv_obj_t *r = boot_rule(body);

        lv_obj_set_pos(r, 0, 22);
    }

    /* 与休眠卡共用用户提供的单色 logo 和概率彩蛋。 */
    badge = app_shell_brandmark(body);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, badge_top);

    /* ---- 品牌字标 ---- */
    {
        lv_obj_t *brand = lv_image_create(body);
        lv_image_set_src(brand, &ui_boot_title);
        lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, badge_bot + 20);
    }
    {
        lv_obj_t *r = boot_rule(body);

        lv_obj_set_pos(r, 60, badge_bot + 80);
        lv_obj_set_width(r, 320);
    }
    {
        lv_obj_t *sub = lv_image_create(body);
        lv_image_set_src(sub, &ui_boot_subtitle);
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, badge_bot + 95);
    }

    /* ---- 分段进度条 ---- */
    segs = lv_obj_create(body);
    lv_obj_remove_style_all(segs);
    lv_obj_set_scrollable(segs, false);
    lv_obj_set_size(segs, LV_PCT(100), 10);
    lv_obj_set_pos(segs, 0, badge_bot + 136);
    lv_obj_set_flex_flow(segs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(segs, 2, LV_PART_MAIN);
    for(i = 0; i < APP_BOOT_SEG_NUM; i++)
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
        lv_obj_set_size(row, LV_PCT(100), 22);
        lv_obj_set_pos(row, 0, badge_bot + 152);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        boot_label(row, &ui_font_14, lv_color_black(), app_text("系统初始化", "SYSTEM INIT"));
        {
            lv_obj_t *sp = lv_obj_create(row);

            lv_obj_remove_style_all(sp);
            lv_obj_set_scrollable(sp, false);
            lv_obj_set_size(sp, 1, 1);
            lv_obj_set_flex_grow(sp, 1);
        }
        m_pct_label = boot_label(row, &ui_font_14, lv_color_black(), "0%");
    }

    /* ---- 自检遥测（左名 / 右值 / 尾 OK） ---- */
    tele_box = lv_obj_create(body);
    lv_obj_remove_style_all(tele_box);
    lv_obj_set_scrollable(tele_box, false);
    lv_obj_set_size(tele_box, LV_PCT(100), 120);
    lv_obj_set_pos(tele_box, 0, badge_bot + 180);
    for(i = 0; i < m_tele_num && i < 8; i++)
    {
        char localized[40];
        int32_t y = (int32_t)i * 22;
        const char *value = boot_tele_value(m_tele[i].value, localized, sizeof(localized));
        lv_obj_t *n = boot_label(tele_box, &ui_font_14, lv_color_black(), boot_tele_name(m_tele[i].name));
        lv_obj_set_pos(n, 0, y);
        lv_obj_t *v = boot_label(tele_box, &ui_font_14, lv_color_black(), value);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -48, y);
        m_tele_ok[i] = boot_label(tele_box, &ui_font_14, lv_color_black(), app_text("等待", "--"));
        lv_obj_align(m_tele_ok[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }

    /* 进度：从 0 起，之后每 APP_BOOT_STEP_MS 点亮一格（见 boot_prog_cb） */
    m_seg_on = 0;
    m_done_left = APP_BOOT_DONE_HOLD_TICKS;
    boot_apply_progress(0);
    m_prog_timer = lv_timer_create(boot_prog_cb, APP_BOOT_STEP_MS, NULL);
}

static void boot_ui_destroy(void)
{
    /* 关掉建页时打开的黑白快刷会话：补一次 POF，面板不留在带电态 */
    hal_epd_mono_session_end();

    /* 页面内定时器必须在这里删掉：它挂在已销毁的控件上，不删会野指针 */
    if(m_prog_timer != NULL)
    {
        lv_timer_delete(m_prog_timer);
        m_prog_timer = NULL;
    }

    memset(m_seg, 0, sizeof(m_seg));
    memset(m_tele_ok, 0, sizeof(m_tele_ok));
    app_shell_release(&m_shell);   // 本页没启 tick，这里只是统一收尾
    m_pct_label = NULL;
    sys_logi(APP_BOOT_TAG, "destroy boot page");
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */
static const app_ui_ops_t g_boot_ui_ops = {
    .create  = boot_ui_create,
    .destroy = boot_ui_destroy,
    .on_msg  = NULL,     // 进度由页面内定时器自走，不需要外部投消息
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
