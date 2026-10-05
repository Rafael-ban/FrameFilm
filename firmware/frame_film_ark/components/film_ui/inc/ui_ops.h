#ifndef __UI_OPS_H__
#define __UI_OPS_H__

/*********************************************************************
 * INCLUDES
 */
#include "lvgl.h"
#include "hal_input.h"
#include "ui_core.h"    /* app_ui_ops_t 的前向声明（typedef 在那里，避免重复定义） */

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
 * @brief UI 页面契约（app_ui_ops_t 的完整定义）
 *
 * 每个 UI 层 app 提供一个 const 实例，通过 app_entry_t.ui_ops 注册。
 * 除 create 外的回调均可为 NULL。
 *
 * 所有回调都在 ui_task 上下文执行（独占 LVGL），因此可以安全调用 lv_*。
 * app_entry.on_event 则跑在 app_task，禁止在其中调用 lv_*；需要更新 UI 时
 * 用 ui_core_post() 投递，由 on_msg 在 ui_task 侧处理。
 */
struct app_ui_ops_t
{
    /**
     * @brief 创建页面内容
     *
     * root 是由 ui_core 创建并已设为白色的 screen 对象，app 在其下挂控件。
     * 页面清理无需在此处理：退出时 ui_core 会连同 root 一并删除。
     *
     * @param root 页面根对象（screen）
     */
    void (*create)(lv_obj_t *root);

    /**
     * @brief 页面销毁前的额外清理（可选）
     *
     * 仅用于释放 app 自己持有的资源（如 lv_timer、PSRAM 缓冲）；
     * 控件与 root 由 ui_core 统一删除。
     */
    void (*destroy)(void);

    /**
     * @brief 接收 ui_core_post() 投递的消息（可选）
     *
     * @param cmd  命令号
     * @param data 负载，可能为 NULL
     * @param len  负载长度
     */
    void (*on_msg)(uint32_t cmd, const void *data, uint8_t len);

    /**
     * @brief 按键回调（可选）
     *
     * @param key 按键类型
     */
    void (*on_key)(input_press_type_t key);
};

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

#ifdef __cplusplus
}
#endif

#endif /* __UI_OPS_H__ */
