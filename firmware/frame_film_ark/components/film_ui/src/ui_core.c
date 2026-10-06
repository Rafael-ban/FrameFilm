/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * FileName : /film_ui/src/ui_core.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/17
 * Description: UI 框架层核心：LVGL 宿主任务 / 页面生命周期 / 暂停恢复 / 跨线程消息
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "sys_log.h"
#include "hal_epd.h"

#include "ui_conf.h"
#include "ui_core.h"

#if (SYS_UI_ENABLE == 0)

/*********************************************************************
 * UI 层被裁剪：空实现，且不引用任何 lv_* 符号（LVGL 静态库不会被链接）
 *********************************************************************/

int ui_core_init(void)                                              { return -1; }
int ui_core_is_ready(void)                                          { return 0; }
void ui_core_page_enter(uint8_t app_id, const app_ui_ops_t *ops)    { (void)app_id; (void)ops; }
void ui_core_page_exit(void)                                        { }
void ui_core_pause(void)                                            { }
void ui_core_resume(void)                                           { }

int ui_core_post(uint32_t cmd, const void *data, uint8_t len)
{
    (void)cmd; (void)data; (void)len;
    return -1;
}

void ui_core_post_key(uint8_t key)                                  { (void)key; }

#else /* SYS_UI_ENABLE */

#include "lvgl.h"

#include "ui_ops.h"
#include "ui_assets.h"
#include "ui_display.h"

/*********************************************************************
 * MACROS
 */
#define UI_TAG      "ui_core"

/* 黑白快刷会话的保持时长：最后一次推屏之后超过它就给面板断电。
   取 2s 是为了"连点按键翻页 / 进度条逐格刷新"这类连续刷新的间隙（间隔远小于 2s）
   都落在同一个会话里、省掉每帧那趟 PON/POF；同时保证真正停下来后很快断电。
   ui_task 的等待上限 UI_WAIT_MAX_MS(200ms) 保证了断电判定的及时性。 */
#define UI_MONO_SESSION_HOLD_MS     (2000)

/*********************************************************************
* TYPEDEFS
*/
/**
 * @brief UI 层状态机
 */
typedef enum {
    UI_STATE_OFF = 0,   // bringup 失败 / 未就绪
    UI_STATE_IDLE,      // display 就绪，无页面，不驱动
    UI_STATE_ACTIVE,    // 有页面，驱动 lv_timer_handler
    UI_STATE_PAUSED,    // 有页面，但暂停驱动与输出（面板被直绘占用）
} ui_state_t;

/**
 * @brief 命令 ID
 */
typedef enum {
    UI_CMD_NONE = 0,
    UI_CMD_PAGE_ENTER,
    UI_CMD_PAGE_EXIT,
    UI_CMD_PAUSE,
    UI_CMD_RESUME,
    UI_CMD_APP_MSG,
    UI_CMD_KEY,
} ui_cmd_id_t;

/**
 * @brief 命令对象（队列项，值语义）
 */
typedef struct {
    uint8_t  id;              // ui_cmd_id_t
    uint8_t  len;             // data 有效长度
    uint8_t  key;             // UI_CMD_KEY 时的按键值
    uint8_t  reserved;
    uint32_t arg;             // UI_CMD_APP_MSG 时的 cmd；UI_CMD_PAGE_ENTER 时的 app_id
    const app_ui_ops_t *ops;  // UI_CMD_PAGE_ENTER 时的页面契约
    uint8_t  data[UI_CMD_DATA_MAX];
} ui_cmd_t;

/*********************************************************************
 * LOCAL VARIABLES
 */
static QueueHandle_t m_queue = NULL;
static TaskHandle_t  m_task  = NULL;
static SemaphoreHandle_t m_ack_sem = NULL;   // pause/page_exit 的"已停止输出"应答
static ui_state_t    m_state = UI_STATE_OFF;
static lv_obj_t     *m_root  = NULL;   // 当前页面根对象
static const app_ui_ops_t *m_ops = NULL;
static uint32_t      m_drop_count = 0; // 投递失败计数（仅用于告警节流）

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void ui_task(void *pvParameters);
static void ui_handle_cmd(const ui_cmd_t *c);
static int  ui_cmd_send(const ui_cmd_t *c);

/*********************************************************************
 * LOCAL HELPERS
 */

/**
 * @brief LVGL tick 源：直接取 esp_timer，无需额外定时器
 */
static uint32_t ui_tick_get(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief 建立 LVGL 本体（须在 ui_task 上下文执行）
 *
 * 只做 lv_init 与 tick 源注册：显存与 display 改为按页申请/归还
 * （见 ui_display_acquire），故这里没有失败路径，也不会牵动队列。
 */
static void ui_bringup(void)
{
    lv_init();
    lv_tick_set_cb(ui_tick_get);
}

/**
 * @brief 把面板复位到干净状态，使下一次 mono 刷新先做一次完整清场（消除残影）
 *
 * 直绘内容（各 app 自己的 mono 画面）与 UI 页同属一个 mono（spectra state=2）会话，若不做处理，
 * 差分快刷只驱动变化像素、且快刷波形没有彻底擦除的相位，会把上一幅画面的痕迹留在屏上。
 * 硬复位会让驱动把 mono 会话置为无效（`reset()` 里 `m_mono_inited = false`），
 * 下次 mono 刷新即重建会话并走 `epd_spectra_full_clear()`——这正是"用完整清场波形擦干净"的手段。
 *
 * 代价是一次全清（数秒级 + 一次闪）。从彩色路径进来时本来就要复位，故此时不额外增加开销。
 */
static void ui_clean_panel(void)
{
    hal_epd_display_init();
}

/**
 * @brief 销毁当前页面并归还显示资源（幂等）
 */
static void ui_page_teardown(void)
{
    /* 关掉黑白快刷会话并把面板断电：本函数在 ui_task 里同步跑完（page_exit 之后
       才放行 DIRECT 层绘制），所以不会和随后的直绘抢 SPI，也不会让会话标志残留。 */
    hal_epd_mono_session_end();

    if(m_ops != NULL && m_ops->destroy != NULL)
    {
        m_ops->destroy();
    }
    m_ops = NULL;

    if(m_root != NULL)
    {
        lv_obj_delete(m_root);
        m_root = NULL;
    }

    /* 图像对象已随 root 删除，此时再释放图标资源（L8 展开缓冲约 70KB PSRAM）。
       不常驻：DIRECT 层的大 film 需要尽可能大的连续 PSRAM 块。 */
    ui_assets_release();

    /* 归还显存与 display：UI 层与 DIRECT 层互斥，两块缓冲（共 86KB）不应常驻，
       否则会与图片 app 需要的大块连续 PSRAM（8bpp film 需 345KB）抢内存 */
    ui_display_release();
}

/**
 * @brief 创建并加载新页面
 */
static void ui_page_build(const ui_cmd_t *c)
{
    ui_page_teardown();

    /* 按需申请显存；失败只影响本次页面（UI 层降级），不牵动其它 app */
    if(ui_display_acquire() != 0)
    {
        sys_loge(UI_TAG, "display acquire failed, page enter aborted");
        m_state = UI_STATE_IDLE;
        return;
    }

    /* 先把面板复位到干净状态：首帧会走全清场，避免残留上一幅 mono 画面 */
    ui_clean_panel();

    m_ops = c->ops;

    /* 独立 screen：退出即整棵删除，页面之间互不残留 */
    m_root = lv_obj_create(NULL);
    if(m_root == NULL)
    {
        sys_loge(UI_TAG, "create screen failed");
        m_ops = NULL;
        m_state = UI_STATE_IDLE;
        return;
    }

    /* I1 只有两色，底色显式设为白：主题默认的中间灰经亮度阈值化后不可预期。
       页面不可滚动：EPD 上的滚动动画=每帧整屏刷新，必须杜绝。 */
    lv_obj_remove_style_all(m_root);
    lv_obj_set_style_bg_color(m_root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(m_root, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(m_root, false);

    if(m_ops != NULL && m_ops->create != NULL)
    {
        m_ops->create(m_root);
    }

    lv_screen_load(m_root);

    /* 页面就绪后才开闸门：避免半成品页面被推上屏 */
    ui_display_set_output(1);
    m_state = UI_STATE_ACTIVE;

    sys_logi(UI_TAG, "page enter: app_id=%u", (unsigned)c->arg);
}

/**
 * @brief 投递一条命令到队列
 * @return 0 成功；-1 队列未就绪或已满
 */
static int ui_cmd_send(const ui_cmd_t *c)
{
    if(m_queue == NULL)
    {
        return -1;
    }
    if(xQueueSend(m_queue, c, 0) != pdPASS)
    {
        m_drop_count++;
        if((m_drop_count % 8) == 1)   // 节流告警
        {
            sys_logw(UI_TAG, "cmd queue full, drop id=%u (total %u)",
                     (unsigned)c->id, (unsigned)m_drop_count);
        }
        return -1;
    }
    return 0;
}

/**
 * @brief 等待 ui_task 确认"已停止把帧推给面板"
 *
 * 面板是 app_task（DIRECT 直绘）与 ui_task（UI flush）共享的资源，
 * 两个任务同时调 hal_epd_display_mono() 会让 SPI 事务交错、画面损坏。
 * 闸门只能挡住"尚未开始"的 flush，挡不住"正在进行"的那一次，
 * 因此关闭闸门后必须等 ui_task 给出应答：ui_task 是单线程，一旦它处理完
 * 本条命令，就保证没有任何 flush 在飞行中。
 *
 * @param timeout_ms 等待上限；超时只告警（失败姿态是"可能多刷一帧"，不是卡死）
 */
static void ui_wait_stopped(uint32_t timeout_ms)
{
    if(m_ack_sem == NULL)
    {
        return;
    }

    (void)xSemaphoreTake(m_ack_sem, 0);   // 清掉可能残留的旧应答

    if(xSemaphoreTake(m_ack_sem, pdMS_TO_TICKS(timeout_ms)) != pdPASS)
    {
        sys_logw(UI_TAG, "wait output-stopped ack timeout (%ums)", (unsigned)timeout_ms);
    }
}

/**
 * @brief 处理一条命令（仅 ui_task 上下文）
 */
static void ui_handle_cmd(const ui_cmd_t *c)
{
    switch(c->id)
    {
    case UI_CMD_PAGE_ENTER:
        ui_page_build(c);
        break;

    case UI_CMD_PAGE_EXIT:
        ui_display_set_output(0);
        ui_page_teardown();
        m_state = UI_STATE_IDLE;
        sys_logi(UI_TAG, "page exit");
        if(m_ack_sem != NULL)
        {
            (void)xSemaphoreGive(m_ack_sem);   // 应答：此刻起无 flush 在飞行
        }
        break;

    case UI_CMD_PAUSE:
        ui_display_set_output(0);
        /* 面板马上要被直绘占用：先关会话断电，避免 DC/DC 带着电跨到直绘期间 */
        hal_epd_mono_session_end();
        if(m_state == UI_STATE_ACTIVE)
        {
            m_state = UI_STATE_PAUSED;
            sys_logi(UI_TAG, "output paused");
        }
        if(m_ack_sem != NULL)
        {
            (void)xSemaphoreGive(m_ack_sem);
        }
        break;

    case UI_CMD_RESUME:
        /* 只在确实暂停过时才动作：已 ACTIVE 时重发会白付一次全清场的代价（数秒 + 一次闪）。
           暂停由 app_task 同步关闸门、ui_task 置 PAUSED 两步完成，故这里看状态即可 */
        if(m_state == UI_STATE_PAUSED)
        {
            /* 从直绘占屏回来：直绘内容与页面同属 mono 会话，先强制一次清场消掉残影 */
            ui_clean_panel();

            ui_display_set_output(1);
            /* 直绘内容需要被页面覆盖回来，强制整屏重绘 */
            ui_display_invalidate_all();
            m_state = UI_STATE_ACTIVE;
            sys_logi(UI_TAG, "output resumed");
        }
        break;

    case UI_CMD_APP_MSG:
        if(m_ops != NULL && m_ops->on_msg != NULL)
        {
            m_ops->on_msg(c->arg, (c->len > 0) ? c->data : NULL, c->len);
        }
        break;

    case UI_CMD_KEY:
        if(m_ops != NULL && m_ops->on_key != NULL)
        {
            m_ops->on_key((input_press_type_t)c->key);
        }
        break;

    default:
        break;
    }
}

/**
 * @brief ui_task 主体
 *
 * 先处理队列命令，再驱动 LVGL；空闲时长取 lv_timer_handler 的建议值并夹紧，
 * 用"带超时的队列等待"同时实现休眠与命令唤醒（避免轮询）。
 */
static void ui_task(void *pvParameters)
{
    ui_cmd_t cmd;

    (void)pvParameters;

    /* 只建 LVGL 本体，不申请显存：显存按页申请/归还（见 ui_page_build/teardown），
       因此这里没有失败路径，也就不会再出现"启动时失败把队列回收掉"的连锁反应 */
    ui_bringup();
    m_state = UI_STATE_IDLE;

    for(;;)
    {
        while(xQueueReceive(m_queue, &cmd, 0) == pdPASS)
        {
            ui_handle_cmd(&cmd);
        }

        uint32_t idle = UI_WAIT_MAX_MS;

        if(m_state == UI_STATE_ACTIVE)
        {
            idle = lv_timer_handler();

            /* 黑白快刷会话的空闲收尾：距最后一次推屏超过 HOLD 就断电（幂等）。
               只在这里做（ui_task 上下文），不和直绘抢 SPI：
               UI 与直绘互斥，且 page_exit/pause 的同步握手保证切走前会话已关。 */
            int64_t last = ui_display_last_flush_us();
            if(last != 0 &&
               (esp_timer_get_time() - last) >= (int64_t)UI_MONO_SESSION_HOLD_MS * 1000)
            {
                hal_epd_mono_session_end();
            }
        }

        if(idle < UI_WAIT_MIN_MS)
        {
            idle = UI_WAIT_MIN_MS;
        }
        if(idle > UI_WAIT_MAX_MS)
        {
            idle = UI_WAIT_MAX_MS;
        }

        if(xQueueReceive(m_queue, &cmd, pdMS_TO_TICKS(idle)) == pdPASS)
        {
            ui_handle_cmd(&cmd);
        }
    }
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

int ui_core_is_ready(void)
{
    /* 运行期能力判定：UI 层依赖 mono 快刷（仅 3.7" 屏具备） */
    return (hal_epd_get_capabilities() & EPD_CAP_MONOFAST) ? 1 : 0;
}

int ui_core_init(void)
{
    if(m_queue != NULL)
    {
        return 0;   // 已初始化
    }

    if(!ui_core_is_ready())
    {
        sys_logw(UI_TAG, "panel has no mono capability, ui layer disabled");
        return -1;
    }

    m_queue = xQueueCreate(UI_CMD_QUEUE_LEN, sizeof(ui_cmd_t));
    if(m_queue == NULL)
    {
        sys_loge(UI_TAG, "queue create failed");
        return -1;
    }

    /* 二元信号量：pause / page_exit 与 ui_task 的握手（见 ui_wait_stopped） */
    m_ack_sem = xSemaphoreCreateBinary();
    if(m_ack_sem == NULL)
    {
        sys_loge(UI_TAG, "ack semaphore create failed");
        vQueueDelete(m_queue);
        m_queue = NULL;
        return -1;
    }

    if(xTaskCreate(ui_task, UI_TASK_NAME, UI_TASK_STACK, NULL, UI_TASK_PRIO, &m_task) != pdPASS)
    {
        sys_loge(UI_TAG, "task create failed");
        vSemaphoreDelete(m_ack_sem);
        m_ack_sem = NULL;
        vQueueDelete(m_queue);
        m_queue = NULL;
        return -1;
    }

    sys_logi(UI_TAG, "ui core init done (task prio %d, stack %d)", UI_TASK_PRIO, UI_TASK_STACK);
    return 0;
}

void ui_core_page_enter(uint8_t app_id, const app_ui_ops_t *ops)
{
    ui_cmd_t cmd;

    if(ops == NULL || m_queue == NULL)
    {
        sys_logw(UI_TAG, "page enter ignored: ops=%p queue=%p", (void *)ops, (void *)m_queue);
        return;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = UI_CMD_PAGE_ENTER;
    cmd.arg = app_id;
    cmd.ops = ops;
    (void)ui_cmd_send(&cmd);
}

void ui_core_page_exit(void)
{
    ui_cmd_t cmd;

    /* 同步关闸门并等待 ui_task 停稳：随后 DIRECT 层 app 会立刻绘制面板，
       两个任务同时推 mono 帧会让 SPI 事务交错 */
    ui_display_set_output(0);

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = UI_CMD_PAGE_EXIT;
    if(ui_cmd_send(&cmd) == 0)
    {
        ui_wait_stopped(UI_ACK_TIMEOUT_MS);
    }
}

void ui_core_pause(void)
{
    ui_cmd_t cmd;

    /* 同上：直绘内容要立刻独占面板 */
    ui_display_set_output(0);

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = UI_CMD_PAUSE;
    if(ui_cmd_send(&cmd) == 0)
    {
        ui_wait_stopped(UI_ACK_TIMEOUT_MS);
    }
}

void ui_core_resume(void)
{
    ui_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = UI_CMD_RESUME;
    (void)ui_cmd_send(&cmd);
}

int ui_core_post(uint32_t cmd, const void *data, uint8_t len)
{
    ui_cmd_t c;

    if(m_queue == NULL || cmd == 0)
    {
        return -1;
    }
    if(len > UI_CMD_DATA_MAX)
    {
        sys_logw(UI_TAG, "post cmd=%u len=%u exceeds %u, truncate",
                 (unsigned)cmd, (unsigned)len, (unsigned)UI_CMD_DATA_MAX);
        len = UI_CMD_DATA_MAX;
    }

    memset(&c, 0, sizeof(c));
    c.id = UI_CMD_APP_MSG;
    c.arg = cmd;
    c.len = len;
    if(len > 0 && data != NULL)
    {
        memcpy(c.data, data, len);
    }

    return ui_cmd_send(&c);
}

void ui_core_post_key(uint8_t key)
{
    ui_cmd_t cmd;

    if(m_queue == NULL)
    {
        return;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.id = UI_CMD_KEY;
    cmd.key = key;
    (void)ui_cmd_send(&cmd);
}

#endif /* SYS_UI_ENABLE */
