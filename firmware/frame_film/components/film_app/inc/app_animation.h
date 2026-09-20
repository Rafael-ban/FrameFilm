#ifndef __APP_ANIMATION_H__
#define __APP_ANIMATION_H__

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
 * TAG / 取值范围都放在 app 自己的头文件里：BLE 参数通道（0x49 / 0x4A）与
 * 设置页（app_manager_param_get / set）共用同一套语义，只此一份。 */

/* 每帧间隔（毫秒，即"播放速度"）：下限受 on_tick 周期（100ms）约束 */
#define APP_ANIM_FRAME_MS_MIN   (100)
#define APP_ANIM_FRAME_MS_MAX   (2000)

/* 播放模式 */
#define APP_ANIM_PLAY_SINGLE    (0)     // 单 film 循环：只播当前文件，播完等待后重播
#define APP_ANIM_PLAY_SEQ       (1)     // film 列表循环：依次播每个文件，到末尾回绕

/* TAG */
#define APP_ANIM_TAG_PLAY_MODE  (0x01)  // 1B：0=单 film 循环 1=列表循环
#define APP_ANIM_TAG_FRAME_MS   (0x02)  // 2B：100~2000 毫秒
#define APP_ANIM_TAG_LOOP_SEC   (0x03)  // 2B：0~600 秒
#define APP_ANIM_TAG_FILE_ID    (0x04)  // 4B：当前文件下标

/*********************************************************************
 * GLOBAL VARIABLES
 */

/**
 * @brief 动图 app 接口实例（独立目录 /sdcard/animation 的多帧循环播放）
 */
extern const app_entry_t g_app_animation_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_ANIMATION_H__ */
