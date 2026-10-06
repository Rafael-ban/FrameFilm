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
 * FileName : /film_app/core/app_render.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/9
 * Description: App 层渲染/显示抽象（能力位 / 完整帧 / 8bpp / MonoFast）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "sys_log.h"
#include "sys_cfg.h"
#include "hal_epd.h"
#include "app_render.h"

/*********************************************************************
 * MACROS
 */
#define APP_RENDER_TAG  "App_Render"

// .film 文件头
#define FILM_HDR_SIZE           (32)
#define FILM_HDR_OFFSET_FORMAT  (0x09)

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

uint32_t app_render_get_capabilities(void)
{
    return hal_epd_get_capabilities();
}

int app_render_has_app_menu(void)
{
    /* 主菜单是 UI 层页面：UI 层没编进来（SYS_UI_ENABLE=0）时无从渲染，
       面板还需具备 MonoFast 差分局刷，交互则依赖上/下导航与确认键 */
    if(SYS_UI_ENABLE == 0)
    {
        return 0;
    }
    return (app_render_has_monofast() && SYS_INPUT_HAS_NAV_ENTER) ? 1 : 0;
}

int app_render_has_8bpp(void)
{
    return (app_render_get_capabilities() & EPD_CAP_8BPP) != 0;
}

int app_render_has_monofast(void)
{
    return (app_render_get_capabilities() & EPD_CAP_MONOFAST) != 0;
}

void app_render_display_full(const unsigned char *filmData)
{
    if(filmData == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_full: NULL data");
        return;
    }

    /* app 层是 .film Format 的唯一分派点：
     * 0x01 MonoFast / 0x02 ColorQual / 0x03 ColorFast 走对应驱动，
     * 其余（v1 4bpp）交给 hal_epd_display_film。
     * 分派前按能力位判定，避免在不支持的屏上调用空实现。 */
    uint8_t format = filmData[FILM_HDR_OFFSET_FORMAT];
    uint32_t caps = app_render_get_capabilities();

    /* MonoFast 单独处理，不能走 hal_epd_display_init()/hal_epd_pwroff()：
     * 前者会硬复位面板（控制器里缓存的上一帧是差分基准），后者内部是 DSLP 深睡，
     * 两者都会让下一次 mono 刷新退化成整屏刷新（翻封面/时钟每帧整屏闪）。
     * 这里是一次性绘制（画一张就完），不打开黑白快刷会话，故 hal_epd_display_mono
     * 会自己走完 PON/REF/POF，面板不会留在带电态。
     * 连续逐帧刷新（动图播放）才用 hal_epd_mono_session_begin/end 省掉帧间的 PON/POF。 */
    if(format == 0x01)
    {
        if(caps & EPD_CAP_MONOFAST)
        {
            hal_epd_display_mono(filmData + FILM_HDR_SIZE);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "monofast film but panel unsupported");
        }
        return;
    }

    hal_epd_display_init();
    switch(format)
    {
    case 0x02:  // v2 ColorQual（3 相）
        if(caps & EPD_CAP_8BPP)
        {
            hal_epd_display_8bpp_mode(filmData + FILM_HDR_SIZE, 1);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "8bpp film but panel unsupported");
        }
        break;
    case 0x03:  // v2 ColorFast（2 相）
        if(caps & EPD_CAP_8BPP)
        {
            hal_epd_display_8bpp_mode(filmData + FILM_HDR_SIZE, 0);
        }
        else
        {
            sys_logw(APP_RENDER_TAG, "8bpp film but panel unsupported");
        }
        break;
    default:    // v1 4bpp 单帧
        hal_epd_display_film(filmData);
        break;
    }
    hal_epd_pwroff();
}

void app_render_display_8bpp(const unsigned char *index8Data, uint8_t mode)
{
    if(index8Data == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_8bpp: NULL data");
        return;
    }

    hal_epd_display_init();
    hal_epd_display_8bpp_mode(index8Data, mode);
    hal_epd_pwroff();
}

void app_render_display_mono(const unsigned char *mono_bitmap)
{
    if(mono_bitmap == NULL)
    {
        sys_loge(APP_RENDER_TAG, "display_mono: NULL data");
        return;
    }

    /* 同 display_full 的 MonoFast 分支：不能复位/深睡，否则时钟每秒一帧都会整屏闪。
       一次性绘制不打开黑白快刷会话，电源由 hal_epd_display_mono 自己走完 PON/REF/POF。 */
    hal_epd_display_mono(mono_bitmap);
}

void app_render_clean_panel(void)
{
    /* 与 ui_core.c 的 ui_clean_panel() 同一套做法：硬复位把 mono 会话置为无效，
       下一次 mono 刷新会重建会话并先走一次完整清场（epd_spectra_full_clear）。
       直绘层与 UI 页共用同一个 mono 差分会话，不复位就会把上一屏"同色"的残留
       像素漏在新画面上（快刷波形没有彻底擦除的相位），即残影。 */
    hal_epd_display_init();
}

void app_render_clear(void)
{
    hal_epd_display_init();
    hal_epd_display_white();
    hal_epd_pwroff();
}
