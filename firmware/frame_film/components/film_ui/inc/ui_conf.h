#ifndef __UI_CONF_H__
#define __UI_CONF_H__

/*********************************************************************
 * INCLUDES
 */
#include "hal_epd.h"    /* EPD_WIDTH / EPD_HEIGHT */
#include "sys_cfg.h"    /* SYS_UI_ENABLE */

/*********************************************************************
 * MACROS
 */

/* ---- 逻辑画布 ----
 * 面板物理分辨率 EPD_WIDTH x EPD_HEIGHT 为横向；UI 层按"物理旋转 90°"的竖屏布局，
 * 故逻辑宽高互换。I1 缓冲字节数 = 像素数 / 8，与 .film Format 0x01 主体大小一致。 */
#define UI_LOGICAL_W            (EPD_HEIGHT)                            /* 480 */
#define UI_LOGICAL_H            (EPD_WIDTH)                             /* 720 */
#define UI_FB_BYTES             ((UI_LOGICAL_W * UI_LOGICAL_H) / 8)     /* 43200 */

/* 送驱动的 mono 位图尺寸（物理横向） */
#define UI_MONO_W               (EPD_WIDTH)
#define UI_MONO_H               (EPD_HEIGHT)
#define UI_MONO_BYTES           ((UI_MONO_W * UI_MONO_H) / 8)           /* 43200 */

/* ---- 首次点亮校准项（三项都可一行翻转） ---- */

/* 旋转方向：1 = 面板顺时针 90°，0 = 逆时针 90°。按实际装配方向二选一 */
#define UI_ROTATE_90_CW         (1)

/* LVGL I1 缓冲的位语义：1 = 亮（白），0 = 暗（黑）。
 * 依据 lv_draw_sw_blend_to_i1.c：src_color = lv_color_luminance(颜色) / (I1_LUM_THRESHOLD + 1)，
 * 亮度高者置位，故白 = 1。若首次点亮发现整体反色，改成 1 即可。 */
#define UI_I1_BIT_BLACK         (0)

/* mono 位图中"黑"对应的位值，须与 .film Format 0x01 约定一致（1 = 黑）。
 * 与驱动的 MONO_CODE_BIT_WHITE 职责不重叠：这里只管"产出符合 .film 约定的位图"。 */
#define UI_MONO_BIT_BLACK       (1)

/* 两块缓冲清"白底"时的填充字节（由上面的位语义推导，勿手改） */
#define UI_I1_WHITE_BYTE        ((UI_I1_BIT_BLACK) ? 0x00 : 0xFF)
#define UI_MONO_WHITE_BYTE      ((UI_MONO_BIT_BLACK) ? 0x00 : 0xFF)

/* ---- 刷新节拍 ----
 * 实际节拍由 Kconfig 的 CONFIG_LV_DEF_REFR_PERIOD 决定（当前 100ms）；
 * 上屏由阻塞式 flush 自然限速，这里只约束 ui_task 的等待上下界。 */
#define UI_WAIT_MIN_MS          (10)
#define UI_WAIT_MAX_MS          (200)

/* ---- ui_task ---- */
#define UI_TASK_NAME            "ui_task"
#define UI_TASK_PRIO            (4)     /* 低于 app_task(5)：输入/事件优先于 UI 渲染 */
#define UI_TASK_STACK           (8192)  /* LVGL 控件树 + 绘图，4096 不够 */
#define UI_CMD_QUEUE_LEN        (8)
#define UI_CMD_DATA_MAX         (16)

/* pause / page_exit 的应答等待上限（毫秒）
 * ui_task 可能正阻塞在一次 flush 里（35ms~200ms+），等它处理完命令即可返回；
 * 超时只告警不阻塞：界面操作失败姿态是"可能多刷一帧"，不是卡死。 */
#define UI_ACK_TIMEOUT_MS       (1000)

#endif /* __UI_CONF_H__ */
