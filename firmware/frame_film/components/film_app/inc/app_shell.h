#ifndef __APP_SHELL_H__
#define __APP_SHELL_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

#include "lvgl.h"

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
/* 外壳尺寸（逻辑竖屏 480x720，与 tools/ui-mockup 的 .status/.foot/.body 一一对应）。
   页面的"正文"排在这两条之间，故对外只暴露边界，不暴露内部实现。 */
#define SHELL_STATUS_H          (30)     // 顶部状态栏高度
#define SHELL_FOOT_H            (28)     // 底部提示行高度
#define SHELL_PAD_X             (14)     // 状态栏/提示行左右内边距
#define SHELL_BODY_PAD_Y        (18)     // 正文上下留白
#define SHELL_BODY_PAD_X        (20)     // 正文左右安全边距

/* 正文可用区域（页面据此排自己的内容） */
#define SHELL_BODY_TOP          (SHELL_STATUS_H + SHELL_BODY_PAD_Y)
#define SHELL_BODY_BOTTOM       (SHELL_FOOT_H + SHELL_BODY_PAD_Y)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 状态栏里需要动态刷新的控件
 */
typedef struct {
    lv_obj_t *bat_label;    // "BAT 82%"
    lv_obj_t *bat_pip;      // 电量指示块（低于 20% 留空）
    lv_obj_t *wifi_pip;
    lv_obj_t *bt_pip;
} app_shell_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 建外壳：顶部状态栏 + 底部提示行（正文由页面自己在两者之间排）
 *
 * 只建"壳"，不建正文容器——各页面的正文布局差异较大，硬套一种容器反而更绕。
 * 页面用 SHELL_BODY_TOP / SHELL_BODY_BOTTOM 决定内容的起止。
 *
 * @param root 页面根对象
 * @param hint 底部左侧操作提示（如 "UP/DOWN SELECT   ENTER OPEN"）
 * @param page 底部右侧页面名（如 "MENU"）
 * @param out  输出：状态栏里需要动态刷新的控件（可为 NULL）
 */
void app_shell_build(lv_obj_t *root, const char *hint, const char *page, app_shell_t *out);

/**
 * @brief 请求刷新状态栏数据（页面 create 时调用一次即可）
 *
 * 上行到 app 任务采集，再由 app_shell_on_event() 回投；页面本身不读服务层。
 */
void app_shell_request_status(void);

/**
 * @brief 把状态栏数据落到控件（页面 on_msg 里调用）
 */
void app_shell_apply(app_shell_t *s, const app_status_t *st);

/**
 * @brief 处理状态栏消息（页面 on_msg 里统一转调）
 *
 * @return 1 已消费；0 与本模块无关（页面继续处理自己的 cmd）
 */
int app_shell_handle_msg(app_shell_t *s, uint32_t cmd, const void *data, uint8_t len);

/**
 * @brief 采集并回投状态栏数据（app 的 on_event 里转调）
 *
 * 采集必须留在 app 任务侧：电池 / g_service_param 没有跨任务保证。
 */
void app_shell_on_event(const app_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SHELL_H__ */
