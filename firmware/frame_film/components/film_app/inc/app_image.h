#ifndef __APP_IMAGE_H__
#define __APP_IMAGE_H__

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
/* ---- 参数通道契约（payload = TLV 列表，多字节大端）----
 * TAG / 取值范围都放在 app 自己的头文件里：BLE 参数通道（0x45 / 0x46）与
 * 设置页（app_manager_param_get / set）共用同一套语义，只此一份。 */

/* 播放模式 */
#define APP_IMAGE_PLAY_MANUAL   (0)     // 手动：仅按键/蓝牙切换
#define APP_IMAGE_PLAY_AUTO     (1)     // 自动：定时切换 / 开机自动切换（形态由休眠开关决定）

/* 自动切换间隔（分钟）：仅休眠关闭时生效 */
#define APP_IMAGE_INTERVAL_MIN  (1)
#define APP_IMAGE_INTERVAL_MAX  (120)

/* TAG */
#define APP_IMAGE_TAG_PLAY_MODE (0x01)  // 1B：0=手动 1=自动
#define APP_IMAGE_TAG_INTERVAL  (0x02)  // 2B：1~120 分钟
#define APP_IMAGE_TAG_FILE_ID   (0x03)  // 4B：当前文件下标

/**
 * @brief 图片 app 接口实例（本地 TF / BLE / WiFi 三来源）
 */
extern const app_entry_t g_app_image_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_IMAGE_H__ */
