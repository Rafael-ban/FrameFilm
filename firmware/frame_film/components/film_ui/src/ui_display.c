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
 * FileName : /film_ui/src/ui_display.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: UI 层显示链路：LVGL I1 缓冲 → 90° 转置 → mono 位图 → EPD
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "sys_log.h"
#include "hal_epd.h"

#include "ui_conf.h"
#include "ui_display.h"

#if (SYS_UI_ENABLE == 0)

/*********************************************************************
 * UI 层被裁剪：空实现，且不引用任何 lv_* 符号（LVGL 静态库不会被链接）
 *********************************************************************/

int  ui_display_acquire(void)               { return -1; }
void ui_display_release(void)               { }
void ui_display_set_output(int enable)      { (void)enable; }
void ui_display_invalidate_all(void)        { }

#else /* SYS_UI_ENABLE */

#include "esp_heap_caps.h"

#include "lvgl.h"

/*********************************************************************
 * MACROS
 */
#define UI_DISPLAY_TAG      "ui_disp"

/*********************************************************************
 * LOCAL VARIABLES
 */
static lv_display_t *m_disp = NULL;
static uint8_t *m_i1_buf = NULL;      /* LVGL I1 渲染缓冲（逻辑竖屏 UI_LOGICAL_W x UI_LOGICAL_H） */
static uint8_t *m_mono_buf = NULL;    /* 送驱动的 mono 位图（物理横向 UI_MONO_W x UI_MONO_H） */

/* 输出闸门：app_task 侧 pause/page_exit 需要"立即"生效（防止正在进行的 flush
 * 覆盖随后绘制的直绘内容），而 ui_task 侧每帧都要读，故用 volatile 避免被缓存。 */
static volatile uint8_t m_output_enabled = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 读 I1 缓冲某像素的位值（每字节 8 像素，MSB 在前）
 */
static inline int ui_i1_get_bit(const uint8_t *buf, int x, int y)
{
    uint32_t idx = (uint32_t)y * UI_LOGICAL_W + (uint32_t)x;
    return (buf[idx >> 3] >> (7u - (idx & 7u))) & 1u;
}

/**
 * @brief 写 mono 位图某像素（每字节 8 像素，MSB 在前）
 */
static inline void ui_mono_put_pixel(uint8_t *buf, int x, int y, int black)
{
    uint32_t idx = (uint32_t)y * UI_MONO_W + (uint32_t)x;
    uint8_t mask = (uint8_t)(0x80u >> (idx & 7u));

    if(black)
    {
        buf[idx >> 3] |= mask;
    }
    else
    {
        buf[idx >> 3] &= (uint8_t)~mask;
    }
}

/**
 * @brief 分配显示缓冲：优先 PSRAM，失败回退内部 RAM
 *
 * 两块缓冲共 86KB。UI 层与 DIRECT 层**互斥**，故这两块只在 UI 页存在期间持有
 * （进入页面申请、退出页面归还），不会与图片 app 需要的大块连续 PSRAM 抢内存。
 * 优先 PSRAM：内部 RAM 还要留给 WiFi/BLE 与文件服务；UI 层低频刷新，PSRAM 带宽足够。
 */
static uint8_t *ui_display_alloc(size_t size, const char *what)
{
    uint8_t *p = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

    if(p != NULL)
    {
        sys_logi(UI_DISPLAY_TAG, "%s: %u bytes from PSRAM", what, (unsigned)size);
        return p;
    }

    p = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(p != NULL)
    {
        sys_logw(UI_DISPLAY_TAG, "%s: %u bytes from internal RAM (PSRAM unavailable)",
                 what, (unsigned)size);
    }
    return p;
}

/**
 * @brief I1 缓冲 → mono 位图：90° 转置 + 极性对齐
 *
 * 逻辑坐标 (sx, sy) ∈ 480x720 → 物理坐标 (dx, dy) ∈ 720x480。
 * 逐像素实现（345,600 像素，240MHz 下约 1~3ms）；UI 层是低频更新场景，
 * 不做位级快速转置以换取可读性与正确性。
 */
static void ui_i1_to_mono(const uint8_t *i1, uint8_t *mono)
{
    /* 先整体填"白"：若转置存在未覆盖像素，失败姿态是白底而不是花屏 */
    memset(mono, UI_MONO_WHITE_BYTE, UI_MONO_BYTES);

    for(int sy = 0; sy < UI_LOGICAL_H; sy++)
    {
        for(int sx = 0; sx < UI_LOGICAL_W; sx++)
        {
            /* I1 位值 → 是否黑（UI_I1_BIT_BLACK 定义 I1 缓冲中"黑"的位值） */
            int black = (ui_i1_get_bit(i1, sx, sy) == UI_I1_BIT_BLACK);
            int dx;
            int dy;

#if UI_ROTATE_90_CW
            /* 顺时针：源左上角 → 目标右上角 */
            dx = UI_MONO_W - 1 - sy;
            dy = sx;
#else
            /* 逆时针：源左上角 → 目标左下角 */
            dx = sy;
            dy = UI_MONO_H - 1 - sx;
#endif
            ui_mono_put_pixel(mono, dx, dy, black);
        }
    }
}

/**
 * @brief LVGL flush 回调
 *
 * RENDER_MODE_FULL 下 area 恒为整屏，px_map 即整块 I1 缓冲，
 * 故一帧只推一次面板；推送在 flush_ready 之前，天然形成"面板能多快就多快"的节流。
 */
static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    if(m_output_enabled && lv_display_flush_is_last(disp))
    {
        ui_i1_to_mono(px_map, m_mono_buf);
        hal_epd_display_mono(m_mono_buf);   /* 阻塞：约 35ms@40MHz + 波形时间 */
    }

    lv_display_flush_ready(disp);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

int ui_display_acquire(void)
{
    if(m_disp != NULL)
    {
        return 0;   // 已申请
    }

    /* 两块缓冲共 86KB；只在 UI 页存在期间持有（见 ui_display_acquire 说明） */
    m_i1_buf = ui_display_alloc(UI_FB_BYTES, "i1 framebuffer");
    m_mono_buf = ui_display_alloc(UI_MONO_BYTES, "mono frame");
    if(m_i1_buf == NULL || m_mono_buf == NULL)
    {
        sys_loge(UI_DISPLAY_TAG, "alloc failed (fb=%d mono=%d): psram_free=%u psram_largest=%u internal_free=%u internal_largest=%u",
                 UI_FB_BYTES, UI_MONO_BYTES,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        ui_display_release();
        return -1;
    }
    memset(m_i1_buf, UI_I1_WHITE_BYTE, UI_FB_BYTES);
    memset(m_mono_buf, UI_MONO_WHITE_BYTE, UI_MONO_BYTES);

    m_disp = lv_display_create(UI_LOGICAL_W, UI_LOGICAL_H);
    if(m_disp == NULL)
    {
        sys_loge(UI_DISPLAY_TAG, "lv_display_create failed");
        ui_display_release();
        return -1;
    }

    lv_display_set_color_format(m_disp, LV_COLOR_FORMAT_I1);
    lv_display_set_flush_cb(m_disp, ui_flush_cb);
    /* 全屏单缓冲：flush_cb 一次拿到整屏，与 EPD 的整帧推送模型吻合 */
    lv_display_set_buffers(m_disp, m_i1_buf, NULL, UI_FB_BYTES, LV_DISPLAY_RENDER_MODE_FULL);

    m_output_enabled = 0;
    sys_logi(UI_DISPLAY_TAG, "display acquired: %dx%d I1, fb=%d bytes",
             UI_LOGICAL_W, UI_LOGICAL_H, UI_FB_BYTES);
    return 0;
}

void ui_display_release(void)
{
    m_output_enabled = 0;

    if(m_disp != NULL)
    {
        lv_display_delete(m_disp);
        m_disp = NULL;
    }
    if(m_i1_buf != NULL)
    {
        heap_caps_free(m_i1_buf);
        m_i1_buf = NULL;
    }
    if(m_mono_buf != NULL)
    {
        heap_caps_free(m_mono_buf);
        m_mono_buf = NULL;
    }
}

void ui_display_set_output(int enable)
{
    m_output_enabled = enable ? 1 : 0;
}

void ui_display_invalidate_all(void)
{
    lv_obj_t *scr = lv_screen_active();

    if(scr != NULL)
    {
        (void)lv_obj_invalidate(scr);
    }
}

#endif /* SYS_UI_ENABLE */
