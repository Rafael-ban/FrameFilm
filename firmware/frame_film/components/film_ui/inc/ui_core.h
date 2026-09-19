#ifndef __UI_CORE_H__
#define __UI_CORE_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

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
 * @brief UI 页面契约（前向声明）
 *
 * 完整定义在 ui_ops.h——那个头才 include lvgl.h。本头保持 lvgl-free，
 * 以便 film_app 的公共头（app_interface.h）在不引入 LVGL 的前提下引用本层。
 */
typedef struct app_ui_ops_t app_ui_ops_t;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化 UI 框架层
 *
 * 创建命令队列与 ui_task；LVGL 本体（lv_init / display / 缓冲）由 ui_task
 * 启动时建立，以保证所有 lv_* 调用都发生在同一任务上下文。
 *
 * 可在 app_task 上下文调用。未通过能力判定时直接返回错误，不创建任务。
 *
 * @return 0 成功；-1 失败（能力不支持或资源不足）
 */
int ui_core_init(void);

/**
 * @brief 当前屏幕是否支持 UI 框架层
 *
 * 编译期看 SYS_UI_ENABLE，运行期看面板是否具备 MonoFast 能力（3.7" 屏）。
 * 供 app_manager 决定 UI 层 app 能否进入；不支持时应回落 DIRECT 层行为。
 *
 * @return 1 支持；0 不支持
 */
int ui_core_is_ready(void);

/**
 * @brief 进入一个 UI 层 app 的页面
 *
 * 异步：投递命令给 ui_task，由其在自身上下文创建页面（先清掉旧页面）。
 *
 * @param app_id 目标 app 的 ID（透传给页面，供日志/调试）
 * @param ops    页面契约，必须常驻（各 app 的 static const 实例）
 */
void ui_core_page_enter(uint8_t app_id, const app_ui_ops_t *ops);

/**
 * @brief 退出当前页面（销毁控件与页面对象）
 *
 * 异步。display 与显存保留，便于下次快速进入。
 */
void ui_core_page_exit(void);

/**
 * @brief 暂停 UI 输出（保留页面）
 *
 * 用于直绘场景临时占用面板：停止 lv_timer_handler 驱动，
 * 并让 flush 不再把帧推给面板，避免覆盖直绘内容。
 */
void ui_core_pause(void);

/**
 * @brief 恢复 UI 输出
 *
 * 恢复驱动，并强制整屏重绘一次（直绘内容需要被页面覆盖回来）。
 */
void ui_core_resume(void);

/**
 * @brief 向当前页面投递一条消息（app_task → ui_task 的唯一跨线程通道）
 *
 * 满队时丢弃并告警，绝不阻塞调用方。
 *
 * @param cmd  命令号（由各 app 自行解释，避免使用 0）
 * @param data 负载，可为 NULL
 * @param len  负载长度，上限 UI_CMD_DATA_MAX
 * @return 0 成功；-1 未就绪或队列满
 */
int ui_core_post(uint32_t cmd, const void *data, uint8_t len);

/**
 * @brief 向当前页面投递一次按键
 *
 * 由 app_manager 在"当前 app 属 UI 层"时调用。
 *
 * @param key 按键类型
 */
void ui_core_post_key(uint8_t key);

#ifdef __cplusplus
}
#endif

#endif /* __UI_CORE_H__ */
