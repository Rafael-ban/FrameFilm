#ifndef __APP_CLOCK_H__
#define __APP_CLOCK_H__

/*********************************************************************
 * INCLUDES
 */
#include "app_interface.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 时钟 app 接口实例（UI 框架层）
 *
 * 运行在 APP_LAYER_UI：页面由 film_ui 用 LVGL 创建（竖屏显示 年月日 / 时分 / 星期），
 * 周期刷新由页面内的 lv_timer 承担，不走 app_task 的 tick。
 */
extern const app_entry_t g_app_clock_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_CLOCK_H__ */
