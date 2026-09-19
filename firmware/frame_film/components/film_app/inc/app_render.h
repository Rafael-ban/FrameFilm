#ifndef __APP_RENDER_H__
#define __APP_RENDER_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 查询当前屏的渲染能力位
 *
 * 返回 hal_epd_get_capabilities() 的 EPD_CAP_* 组合，供 app 层运行时
 * 选择渲染路径（全屏 film / 8bpp 索引色 / MonoFast 差分局刷）。
 *
 * @return EPD_CAP_* 组合
 */
uint32_t app_render_get_capabilities(void);

/**
 * @brief 主菜单是否可交互
 *
 * 运行期按能力判定：面板需具备 MonoFast 快刷（3.7" 屏）且设备有上/下导航与
 * 确认键。不满足时 app 层应降级为简易切换模式。
 *
 * @return 1 可交互，0 不可交互
 */
int app_render_has_app_menu(void);

/**
 * @brief 当前屏是否支持 8bpp 索引色（ColorQual / ColorFast）
 *
 * @return 1 支持，0 不支持
 */
int app_render_has_8bpp(void);

/**
 * @brief 当前屏是否支持 MonoFast 差分局刷（时钟 / 切换封面用）
 *
 * @return 1 支持，0 不支持
 */
int app_render_has_monofast(void);

/**
 * @brief 渲染完整 .film 文件（v1/v2 单帧或首帧）
 *
 * 由文件头 Format 自动分派到对应驱动（4bpp / 8bpp / mono），
 * 内部完成初始化 + 完整刷新 + 关电。
 *
 * @param filmData .film 文件数据指针（含 32 字节文件头）
 */
void app_render_display_full(const unsigned char *filmData);

/**
 * @brief 渲染 8bpp 索引色数据（仅 3.7 屏）
 *
 * @param index8Data 8bpp 颜色索引缓冲（W*H 字节）
 * @param mode 刷新模式：0=ColorFast（2 相），1=ColorQual（3 相）
 */
void app_render_display_8bpp(const unsigned char *index8Data, uint8_t mode);

/**
 * @brief 渲染 1bpp MonoFast 位图（差分局刷，仅 3.7 屏）
 *
 * @param mono_bitmap 1bpp 位图缓冲（W*H/8 字节，MSB 在前，1 黑 0 白）
 */
void app_render_display_mono(const unsigned char *mono_bitmap);

/**
 * @brief 清屏为空白
 */
void app_render_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RENDER_H__ */
