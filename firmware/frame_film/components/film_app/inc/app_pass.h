#ifndef __APP_PASS_H__
#define __APP_PASS_H__

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
 * @brief 通行证 app 接口实例（UI 层占位页）
 *
 * 菜单里有独立卡片（第 2 项），进入后只显示"待开发"占位画面；
 * 后续要做真实功能时，把 ui_ops 换成实际页面即可，身份/位置不用改。
 */
extern const app_entry_t g_app_pass_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_PASS_H__ */
