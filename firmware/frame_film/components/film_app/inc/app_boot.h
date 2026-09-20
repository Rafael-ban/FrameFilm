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
 * MACROS
 */
/* 开机页的进度节奏与在屏时长。
 *
 * 进度条由**页面自己**推进（`lv_timer`），不靠外部投消息 —— 首帧要 ~3.3s
 * （spectra 建会话 + 整屏清场），期间 ui_task 阻塞在 flush 里，外部投的步骤
 * 消息会堆在一起被一次吞掉，进度条看起来完全不动。
 *
 * **每格节拍 ≥ mono 单帧耗时**，这样每次回调恰好点亮一格、不会出现"一次跳两格"。
 * 注意单帧耗时以实测为准：UI 全屏刷新实测约 **940ms/帧**（不是早期封面菜单那次
 * 410ms 的估算），所以节拍取 900ms。格数与节拍的乘积就是进度条走完的时间，
 * 改这里就等于改开机页时长：16 格 × 900ms ≈ 15s，加上首帧清场那 ~3.3s ≈ 18s。 */
#define APP_BOOT_SEG_NUM        (16)    // 进度条格数
#define APP_BOOT_STEP_MS        (900)   // 每格节拍（≈ 单帧耗时，保证一格一格走）
#define APP_BOOT_DONE_HOLD_TICKS (0)    // 走满后停留几个节拍（让 100% 被看清）

/* 兜底超时：正常路径由**页面**走完进度后上报（APP_UI_REQ_BOOT_DONE），
 * 这个定时器只在页面没能上报时（构建/渲染失败等）保证不把用户卡在开机页。
 * 取值要**明显大于**正常路径（≈18s），否则会在进度条快走完时抢先切页、
 * 把"加载完了再进菜单"变成"进度条没走完就跳走"。 */
#define APP_BOOT_FALLBACK_MS    (35000)

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 取得开机页的页面契约（由 app_manager_boot_show 展示）
 */
const app_ui_ops_t *app_boot_ops(void);

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
