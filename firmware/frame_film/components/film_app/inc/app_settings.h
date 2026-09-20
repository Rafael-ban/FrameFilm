#ifndef __APP_SETTINGS_H__
#define __APP_SETTINGS_H__

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
/* 参数行取值方式 */
#define SET_KIND_BAR        (0)   // 电量条（只读）
#define SET_KIND_TEXT       (1)   // 纯文本（只读）
#define SET_KIND_TOGGLE     (2)   // 开关：ENTER 切换
#define SET_KIND_CYCLE      (3)   // 枚举：ENTER 在候选值间循环

/* 行数：设备信息 6 行 + 参数 4 行 */
#define SETTINGS_ROW_NUM    (10)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief 设置页快照
 *
 * **页面（ui_task）不直接读 g_service_param / WiFi / 电池等** —— 那些没有线程安全保证。
 * 由 settings app 在 app_task 侧采集成本结构，经 ui_core_post(APP_UI_MSG_SETTINGS_SNAPSHOT)
 * 下发给页面；页面只渲染。改动则反向上报 APP_UI_REQ_SETTINGS_APPLY。
 */
typedef struct {
    /* 只读展示（已格式化的 ASCII） */
    char    bat[12];        // "82%"
    char    wifi[24];       // "CONNECTED 2.4G"
    char    bt[24];         // "CONNECTED"
    char    panel[24];      // "E6 3.70\" 720x480"
    char    storage[24];    // "23 FILM / 1.2G FREE"
    char    fw[24];         // "1.0.0  SN 000001"
    uint8_t bat_pct;        // 电量条填充百分比
    uint8_t wifi_conn;      // WiFi 已连接 0/1（状态栏第二档：开启=空框、连接=实心）
    uint8_t bt_conn;        // 蓝牙已连接 0/1

    /* 可操作项当前值 */
    uint8_t sleep_mode;     // 0/1
    uint8_t sleep_auto;     // 0/1
    uint8_t wifi_on;        // 0/1
    uint8_t bt_on;          // 0/1
    uint8_t wake_sel;       // WAKE INTERVAL 候选下标
    uint8_t hb_sel;         // HEARTBEAT 候选下标
} settings_snapshot_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 系统设置 app 接口实例（UI 层）
 */
extern const app_entry_t g_app_settings_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_SETTINGS_H__ */
