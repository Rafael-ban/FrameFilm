#ifndef __APP_BOOT_H__
#define __APP_BOOT_H__

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
 * MACROS
 */
/* 开机自检步骤总数（进度条分段数按此计算） */
#define APP_BOOT_STEP_NUM       (5)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 开机自检遥测项
 *
 * 只放**真实值**，不放假数据：面板/内存/存储都是固件确实知道的。
 */
typedef struct {
    const char *name;    // "PANEL" / "MEMORY" / "STORAGE" / "DISPLAY"
    const char *value;   // 已格式化的 ASCII
} app_boot_tele_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 取得开机页的页面契约（由 app_init 直接用 ui_core_page_enter 展示）
 */
const app_ui_ops_t *app_boot_ops(void);

/**
 * @brief 推进开机自检步骤（0 ~ APP_BOOT_STEP_NUM）
 *
 * app_init 在开机时按节奏调用；页面据此更新遥测行与进度条。
 *
 * @param step 已完成的步骤数
 */
void app_boot_advance(uint8_t step);

/**
 * @brief 登记遥测项（页面创建时读取）
 *
 * 由 app_init 在展示开机页之前填好，页面只渲染。
 */
void app_boot_set_telemetry(const app_boot_tele_t *items, uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* __APP_BOOT_H__ */
