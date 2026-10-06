#ifndef __SERVICE_MONITOR_H__
#define __SERVICE_MONITOR_H__

#include <stdbool.h>


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
 * MACROS
 */


/*********************************************************************
* TYPEDEFS
*/
enum
{
    MSG_LED_MANAGER = 0x01,
    MSG_BATTERY_MANAGER,
    MSG_AUTO_SLEEP_MANAGER,
    MSG_ENTER_SLEEP,        // 手动休眠（主菜单长按）：不经过休眠开关判定，直接进低功耗
};

typedef struct
{
    uint8_t ID;
} monitor_msg_t;

/*********************************************************************
 * CONSTANTS
 */


/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */


/*********************************************************************
 * GLOBAL FUNCTIONS
 */
extern void service_monitor_init(void);

/**
 * @brief 请求立即进入低功耗（手动休眠）
 *
 * 只投一条消息，真正的 deinit + deep sleep 在 monitor 任务里做（那里是既有的、
 * 已验证过的入睡路径）。与自动休眠不同，**不看休眠模式开关** —— 用户明确按下的
 * 动作就该执行；定时唤醒参数仍按既有规则生效（sleep_auto && sleep_time > 0）。
 *
 * 调用方须自行确认唤醒条件已解除（见 hal_pwr_wake_condition_met），
 * 否则唤醒源的电平条件当场成立、设备会立刻醒回来。
 */
extern void service_monitor_request_sleep(void);
/* 入睡已请求时不再接受新的临时文件传输。 */
extern bool service_monitor_sleep_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_MONITOR_H__ */
