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
 * 运行在 APP_LAYER_UI：页面由 film_ui 用 LVGL 创建，版式见 tools/ui-mockup §07 ——
 * 主读数（时:分）+ 分钟尺 + 星期黑标 + 日期 + 星期寄存器 + 设备面板。
 * 周期刷新由页面内的 lv_timer 承担（1s 一拍，仅"分钟/日"变化才上屏），不走 app_task 的 tick。
 */
extern const app_entry_t g_app_clock_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_CLOCK_H__ */
