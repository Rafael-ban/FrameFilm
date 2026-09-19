#ifndef __APP_MANAGER_H__
#define __APP_MANAGER_H__

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

/**
 * @brief 按键切换交互模式（生效值）
 *
 * 由 sys_cfg.h 的 SYS_APP_SWITCH_MODE 配置，运行期解析：
 * FULL 在跑不动主菜单（UI 层不可用）的屏上自动降级为 SIMPLE。
 */
typedef enum {
    APP_SWITCH_MODE_NONE = 0,   // 关闭按键切换（BLE 远程切换仍有效）
    APP_SWITCH_MODE_SIMPLE,     // 简易：图片 <-> 最近推送的 app
    APP_SWITCH_MODE_FULL,       // 全功能：主菜单（上电落在它上面；其余 app 长按退出回它）
} app_switch_mode_t;

/**
 * @brief 初始化 app 调度器
 *
 * 创建 app 任务与事件队列，并统一注册输入回调转发到 app_manager。
 * 由 film_app_init() 调用。
 */
void app_manager_init(void);

/**
 * @brief 注册一个 app 到调度器
 *
 * @param app app 接口实例
 */
void app_manager_register(const app_entry_t *app);

/**
 * @brief 请求切换到指定 app
 *
 * 通过事件队列异步触发，实际 on_exit/on_enter 在 app 任务上下文执行。
 * 若队列尚未就绪（初始化早期），则同步执行切换。
 *
 * @param id 目标 app 的 ID
 */
void app_manager_switch(app_id_t id);

/**
 * @brief 输入事件转发入口（由 app_init 注册的 hal_input 回调调用）
 *
 * 将 hal_input 按键回调统一转发为 APP_EVT_INPUT 事件排队，供 app 任务消费。
 *
 * @param key 按键类型
 */
void app_manager_on_input(input_press_type_t key);

/**
 * @brief 获取当前运行的 app ID
 *
 * @return app_id_t 当前 app ID
 */
app_id_t app_manager_get_current(void);

/**
 * @brief 获取生效的按键切换模式
 *
 * 供上层查询/日志使用（FULL 可能已被降级为 SIMPLE）。
 *
 * @return app_switch_mode_t 生效模式
 */
app_switch_mode_t app_manager_get_switch_mode(void);

/**
 * @brief 指定 app 是否已注册
 *
 * 供启动恢复时校验“上次运行的 app 是否在当前模式下仍可用”（被裁剪的 app 需回落）。
 *
 * @param id 目标 app ID
 * @return 1 已注册；0 未注册
 */
int app_manager_is_registered(app_id_t id);

/**
 * @brief 向首个 app 投递一次 APP_EVT_BOOT
 *
 * 由 app_init 在恢复上次 app 后调用，驱动“开机自动”行为（自动切图 / 自动拉取）。
 */
void app_manager_notify_boot(void);

/**
 * @brief 展示开机画面（上电流程专用）
 *
 * 开机画面不是 app：不在注册表里、不参与切换。但占屏这段时间必须由调度器托管 ——
 * 否则此刻没有任何 app 处于运行态，一个按键就会经 app_ensure_running() 直接拉起
 * 某个 app 抢面板，与 ui_task 的 flush 竞争 SPI。托管期间（到 app_manager_boot_end()
 * 为止）按键一律丢弃。
 *
 * @param ops 开机页契约（常驻实例，见 app_boot_ops()）
 */
void app_manager_boot_show(const app_ui_ops_t *ops);

/**
 * @brief 结束开机画面，切到主菜单
 *
 * 页面销毁由 ui_core 在建立主菜单页面时完成（page_enter 内部先 teardown）。
 */
void app_manager_boot_end(void);

/**
 * @brief 展示休眠卡（手动休眠专用）
 *
 * 与开机画面同理：休眠卡不是 app，但占屏期间必须由调度器托管，否则任何事件
 * 都可能经 app_ensure_running() 拉起某个 app 刷屏。区别是**它之后不再切回来** ——
 * 页面画完后设备就进 deep sleep，这一帧是唯一一帧，所以托管期间除"切换 app"
 * 外一律丢弃（实际上这段时间 app 任务也正阻塞在等按键松开，不会消费队列）。
 *
 * @param ops 休眠卡契约（常驻实例，见 app_sleep_ops()）
 */
void app_manager_sleep_show(const app_ui_ops_t *ops);

/**
 * @brief 向当前 app 投递一条来自 UI 页面的请求（ui_task -> app_task）
 *
 * UI 页面在 ui_task 上下文运行，不能直接写 g_service_param（service_param
 * 无内部锁，依赖"只在 app 任务串行调用"）。页面把改动意向通过本接口投递回来，
 * 由 app 的 on_event 在 app_task 侧落地并落盘。
 *
 * @param cmd  请求码（APP_UI_REQ_*）
 * @param data 负载，可为 NULL
 * @param len  负载长度，上限 APP_EVENT_PAYLOAD_MAX
 * @return 0 成功；-1 队列未就绪或已满
 */
int app_manager_post_ui_msg(uint32_t cmd, const void *data, uint8_t len);

/**
 * @brief 保存当前 app 的状态到 NVS
 *
 * app 未声明状态（state/state_size 为空）时是 no-op。
 *
 * @return 0 成功；负值失败
 */
int app_state_save(void);

/**
 * @brief 从 NVS 载入当前 app 的状态
 *
 * 失败（无数据 / 校验不过 / 版本不符）时套用 app 声明的 state_default。
 *
 * @return 0 成功；负值已回落默认值
 */
int app_state_load(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MANAGER_H__ */
