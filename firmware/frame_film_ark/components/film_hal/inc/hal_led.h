#ifndef __HAL_LED_H__
#define __HAL_LED_H__


/*********************************************************************
 * INCLUDES
 */
#include "led_strip.h"


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
// 定义常用颜色
#define LED_COLOR_RED           (0xFF0000)    // 红色
#define LED_COLOR_GREEN         (0x00FF00)    // 绿色
#define LED_COLOR_BLUE          (0x0000FF)    // 蓝色
#define LED_COLOR_WHITE         (0xFFFFFF)    // 白色
#define LED_COLOR_BLACK         (0x000000)    // 黑色
#define LED_COLOR_LIGHT_BLUE    (0x87CEEB)    // 浅蓝色
#define LED_COLOR_LIGHT_RED     (0xFFB6C1)    // 浅红色
#define LED_COLOR_EMERALD       (0x00FF7F)    // 翠绿色
#define LED_COLOR_YELLOW        (0xFFFF00)    // 黄色
#define LED_COLOR_PURPLE        (0x800080)    // 紫色
#define LED_COLOR_ORANGE        (0xFFA500)    // 橙色


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
/**
 * @brief 初始化LED硬件
 *
 * 此函数用于初始化LED硬件，使其处于可用状态。
 * 在使用其他LED相关函数之前，必须先调用此函数。
 */
extern void hal_led_init(void);

/**
 * @brief 获取LED的当前亮度
 *
 * 此函数用于获取LED的当前亮度值。
 *
 * @return 当前LED的亮度值，范围通常为0到255。
 */
extern uint32_t hal_led_get_brightness(void);

/**
 * @brief 设置LED的亮度
 *
 * 此函数用于设置LED的亮度。
 *
 * @param brightness 要设置的亮度值，范围通常为0到255。
 */
extern void hal_led_set_brightness(uint32_t brightness);

/**
 * @brief 设置LED的颜色
 *
 * 此函数用于设置LED的颜色。
 *
 * @param color 要设置的颜色值，通常是一个32位的颜色编码。
 */
extern void hal_led_set_color(uint32_t color);

/**
 * @brief 获取LED的当前颜色
 *
 * 此函数用于获取LED的当前颜色值。
 *
 * @return 当前LED的颜色值，通常是一个32位的颜色编码。
 */
extern uint32_t hal_led_get_color(void);

/**
 * @brief 开始呼吸灯效果（颜色固定，亮度沿升余弦曲线在 min ~ max 之间往复）
 *
 * 只做"显示效果"，不做"什么时候该亮什么色"的策略判断（那属于 service 层）。
 * 重复调用会以新颜色/亮度区间/周期重新开始（相位归零）。
 *
 * 平滑度由模块内部的**固定抖动节拍**（5ms/200Hz）保证，与 period_ms 无关 ——
 * 周期只决定快慢，可以放心调长（别指望用周期去换平滑，那正是暗端发毛的原因）。
 * period_ms 会就近取整到内部可达值（64 档 x 5ms 的整数倍）。
 *
 * @param color          呼吸的颜色（LED_COLOR_xxx）
 * @param min_brightness 最暗亮度（0~100，0 即呼吸到熄灭）
 * @param max_brightness 最亮亮度（0~100）
 * @param period_ms      一个完整呼吸的周期（毫秒）；下限 320ms，实际取最近的可达值
 */
extern void hal_led_breath_start(uint32_t color, uint32_t min_brightness,
                                 uint32_t max_brightness, uint32_t period_ms);

/**
 * @brief 停止呼吸灯（保持当前亮度，不再往复）
 */
extern void hal_led_breath_stop(void);

/**
 * @brief 反初始化LED硬件
 *
 * 此函数用于释放LED硬件资源，将LED设置为关闭状态。
 * 释放RMT设备并将LED数据引脚配置为低功耗输入模式。
 * 通常在进入低功耗模式前调用此函数。
 */
extern void hal_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_LED_H__ */
