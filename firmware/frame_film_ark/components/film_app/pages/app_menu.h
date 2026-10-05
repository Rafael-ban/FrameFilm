#ifndef __APP_MENU_H__
#define __APP_MENU_H__

/*********************************************************************
 * INCLUDES
 */
#include "app_interface.h"
#include "ui_assets.h"

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 主菜单条目
 *
 * 文案全部为 ASCII（固件无 CJK 字形）。
 * 顺序即轮播顺序，**必须与 app_manager 的 m_menu_entries 一致**（选择索引即此表下标）。
 */
typedef struct {
    const char *name;      // 大字名（Montserrat）
    const char *code;      // 卡片短码（UNSCII 8）
    const char *desc;      // 一句话说明
    const char *layer;     // "DIRECT" / "UI"
    const char *entry;     // 进入代价说明
    ui_icon_id_t icon;     // 图标槽位
} menu_item_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 主菜单 app 接口实例（UI 层）
 */
extern const app_entry_t g_app_menu_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_MENU_H__ */
