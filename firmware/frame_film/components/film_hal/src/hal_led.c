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
 * FileName : /film_hal/src/hal_led.c
 * Author: Kiritro  Version: v0.1  Date: 2026/3/31
 * Description: Function introduction
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "led_strip.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "hal_led.h"
#include "sys_log.h"


/*********************************************************************
 * MACROS
 */
#define LED_TAG                        "HAL_LED"

#define RGB_LED_WS2812_PIN             (GPIO_NUM_17)
#define RGB_LED_NUMBERS                (2)
#define RGB_LED_RMT_RES_HZ             (10 * 1000 * 1000)

#define LED_STRIP_USE_DMA              (0)
#if LED_STRIP_USE_DMA
#define LED_STRIP_MEMORY_BLOCK_WORDS   (1024)
#else
#define LED_STRIP_MEMORY_BLOCK_WORDS   (0)
#endif

/* 呼吸灯：亮度在 min~max 之间沿"升余弦"曲线往复，周期由调用方给定。

   两个节拍**必须分开**（这是暗端丝滑的关键）：
     · TICK  ：抖动刷新节拍。抖动靠"两相邻档位快速交替 + 人眼时间积分"补出亚级精度，
               交替频率必须远高于闪烁融合频率（~60Hz），否则看到的就是低频闪烁（毛糙）。
               它是**固定的**，不随呼吸周期变 —— 调周期只能动 PHASE 分频，别动这里。
     · PHASE ：曲线推进节拍 = TICK x 分频，分频由 hal_led_breath_start() 的 period_ms 反推。
   若把两者合成一个节拍（分频=1 且 TICK 调大），抖动会掉到几 Hz，暗端必然一格一格闪。 */
#define LED_BREATH_TICK_MS             (5)
#define LED_BREATH_STEPS               (64)
/* 分频=1 时的周期（最快档）：64 x 5ms = 320ms */
#define LED_BREATH_BASE_MS             (LED_BREATH_STEPS * LED_BREATH_TICK_MS)

/*********************************************************************
* TYPEDEFS
*/
typedef struct
{
    uint32_t brightness;  // LED亮度值，范围通常为0到255
    uint32_t color;       // LED颜色值，通常是一个32位的颜色编码
    bool initialized;     // LED初始化标志
} led_t;

/*********************************************************************
 * CONSTANTS
 */
/* 呼吸曲线：(1 - cos(2*pi*i/64)) / 2 放大到 0~100，i 为相位 */
static const uint8_t m_breath_curve[LED_BREATH_STEPS] =
{
      0,   0,   1,   2,   4,   6,   8,  11,
     15,  18,  22,  26,  31,  35,  40,  45,
     50,  55,  60,  65,  69,  74,  78,  82,
     85,  89,  92,  94,  96,  98,  99, 100,
    100, 100,  99,  98,  96,  94,  92,  89,
     85,  82,  78,  74,  69,  65,  60,  55,
     50,  45,  40,  35,  31,  26,  22,  18,
     15,  11,   8,   6,   4,   2,   1,   0
};


/*********************************************************************
 * LOCAL VARIABLES
 */
static led_strip_handle_t m_rgb;
static led_t m_led =
{
    .brightness = 10,
    .color = LED_COLOR_WHITE,
    .initialized = false
};

static esp_timer_handle_t m_breath_timer;   // 呼吸节拍（惰性创建，只创建一次）
/* 呼吸亮度两端，单位是 **8 位通道刻度 0~255**，不是百分比。
   百分比是整数，MIN~MAX=1~10 这种小范围只剩 10 档可分辨，暗端会一格一格跳。 */
static uint32_t m_breath_min_scale;         // 最暗（0~255）
static uint32_t m_breath_max_scale;         // 最亮（0~255）
static uint32_t m_breath_phase;             // 当前相位（0 ~ LED_BREATH_STEPS-1）
static uint32_t m_breath_div;               // 曲线推进分频（每 N 拍推进一档相位）
static uint32_t m_breath_tick;              // 抖动拍计数（每 m_breath_div 拍推进一档相位）
static uint32_t m_breath_acc;               // 时间抖动累加器（8.8 定点，只存小数部分）

/*********************************************************************
 * GLOBAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */
static led_strip_handle_t configure_led(void);
static void led_breath_timer_cb(void *arg);
static void led_apply_scale(uint32_t scale);

/*********************************************************************
 * GLOBAL FUNCTIONS
 */



/**
 * @brief 初始化LED硬件
 *
 * 此函数用于初始化LED硬件，使其处于可用状态。
 * 在使用其他LED相关函数之前，必须先调用此函数。
 */
void hal_led_init(void)
{
#if FRAMEFILM_MAX == 1
    // Max版本无LED，跳过初始化
    return;
#else
    m_rgb = configure_led();
    m_led.initialized = true;

    hal_led_set_color(m_led.color);
    hal_led_set_brightness(m_led.brightness);
#endif
}

/**
 * @brief 获取LED的当前亮度
 *
 * 此函数用于获取LED的当前亮度值。
 *
 * @return 当前LED的亮度值，范围通常为0到255。
 */
uint32_t hal_led_get_brightness(void)
{
    if (!m_led.initialized)
    {
        return 0;
    }

    return m_led.brightness;
}

/**
 * @brief 设置LED的亮度
 *
 * 此函数用于设置LED的亮度。
 *
 * @param brightness 要设置的亮度值，范围通常为0到255。
 */
void hal_led_set_brightness(uint32_t brightness)
{
    if (!m_led.initialized)
    {
        return;
    }

    m_led.brightness = brightness;
    hal_led_set_color(m_led.color);
}

/**
 * @brief 设置LED的颜色
 *
 * 此函数用于设置LED的颜色。
 *
 * @param color 要设置的颜色值，通常是一个32位的颜色编码。
 */
void hal_led_set_color(uint32_t color)
{
    uint32_t r = 0, g = 0, b = 0;

    if (!m_led.initialized)
    {
        return;
    }

    m_led.color = color;
    if (m_led.brightness > 0 && m_led.brightness <= 100)
    {
        r = (((color >> 16) & 0xff) * m_led.brightness / 100);
        g = (((color >> 8) & 0xff) * m_led.brightness / 100);
        b = ((color & 0xff) * m_led.brightness / 100);
    }

    for(int i = 0; i < RGB_LED_NUMBERS; i++)
    {
        led_strip_set_pixel(m_rgb, i, r, g, b);
    }
    led_strip_refresh(m_rgb);
}

/**
 * @brief 获取LED的当前颜色
 *
 * 此函数用于获取LED的当前颜色值。
 *
 * @return 当前LED的颜色值，通常是一个32位的颜色编码。
 */
uint32_t hal_led_get_color(void)
{
    if (!m_led.initialized)
    {
        return 0;
    }

    return m_led.color;
}

/**
 * @brief 按 8 位通道刻度（0~255）直接写像素
 *
 * 呼吸走这条，而不是 hal_led_set_brightness()：那条的入参是**整数百分比**，
 * 小范围呼吸（如 1%~10%）只剩十来个可分辨档位，暗端会一格一格跳。
 */
static void led_apply_scale(uint32_t scale)
{
    uint32_t r = 0, g = 0, b = 0;

    if (scale > 255u)
    {
        scale = 255u;
    }

    if (scale > 0u)
    {
        r = (((m_led.color >> 16) & 0xffu) * scale) / 255u;
        g = (((m_led.color >> 8) & 0xffu) * scale) / 255u;
        b = ((m_led.color & 0xffu) * scale) / 255u;
    }

    for (int i = 0; i < RGB_LED_NUMBERS; i++)
    {
        led_strip_set_pixel(m_rgb, i, r, g, b);
    }
    led_strip_refresh(m_rgb);

    /* 同步百分比字段（向上取整：scale>0 时不能记成 0，否则会被 set_color 判成熄灭） */
    m_led.brightness = (scale > 0u) ? ((scale * 100u + 254u) / 255u) : 0u;
}

/**
 * @brief 呼吸节拍：按曲线取下一档亮度重画
 *
 * 两个节拍分开跑（见文件头的说明）：
 *   · 每拍（5ms）做一次"时间上的四舍五入"：目标亮度按 8.8 定点累加，
 *     只输出整数部分、余数留到下一拍补，于是两相邻档位在 200Hz 上交替，
 *     时间平均恰好等于目标值 —— 等效精度远高于 8 位通道。
 *   · 每 m_breath_div 拍才推进一档曲线，呼吸快慢由它决定，和抖动互不牵连。
 */
static void led_breath_timer_cb(void *arg)
{
    uint32_t span_fx;
    uint32_t target_fx;
    uint32_t scale;

    (void)arg;

    if (++m_breath_tick >= m_breath_div)
    {
        m_breath_tick  = 0;
        m_breath_phase = (m_breath_phase + 1u) % LED_BREATH_STEPS;
    }

    span_fx   = (m_breath_max_scale - m_breath_min_scale) << 8;
    target_fx = (m_breath_min_scale << 8)
              + span_fx * m_breath_curve[m_breath_phase] / 100u;

    m_breath_acc += target_fx;
    scale = m_breath_acc >> 8;
    m_breath_acc -= (scale << 8);

    led_apply_scale(scale);
}

void hal_led_breath_start(uint32_t color, uint32_t min_brightness, uint32_t max_brightness,
                          uint32_t period_ms)
{
    if (!m_led.initialized)
    {
        return;   // Max 版无 LED
    }

    if (min_brightness > 100u)
    {
        min_brightness = 100u;
    }
    if (max_brightness > 100u)
    {
        max_brightness = 100u;
    }
    if (min_brightness > max_brightness)
    {
        uint32_t tmp = min_brightness;

        min_brightness = max_brightness;
        max_brightness = tmp;
    }

    /* 周期 → 曲线推进分频（四舍五入到最接近的可达周期）。
       下限 1 拍一档 = LED_BREATH_BASE_MS，再快抖动就来不及做时间平均了 */
    m_breath_div = (period_ms + (LED_BREATH_BASE_MS / 2u)) / LED_BREATH_BASE_MS;
    if (m_breath_div == 0u)
    {
        m_breath_div = 1u;
    }

    /* 百分比 → 8 位刻度（对外仍是 0~100，保持接口语义不变） */
    m_breath_min_scale = min_brightness * 255u / 100u;
    m_breath_max_scale = max_brightness * 255u / 100u;
    m_breath_phase     = 0;
    m_breath_tick      = 0;
    m_breath_acc       = 0;
    hal_led_set_color(color);

    if (m_breath_timer == NULL)
    {
        const esp_timer_create_args_t args =
        {
            .callback = led_breath_timer_cb,
            .name     = "led_breath",
        };

        if (esp_timer_create(&args, &m_breath_timer) != ESP_OK)
        {
            sys_loge(LED_TAG, "create breath timer failed");
            return;
        }
    }

    (void)esp_timer_stop(m_breath_timer);   // 未启动时返回错误码，忽略
    (void)esp_timer_start_periodic(m_breath_timer, (uint64_t)LED_BREATH_TICK_MS * 1000u);
}

void hal_led_breath_stop(void)
{
    if (m_breath_timer != NULL)
    {
        (void)esp_timer_stop(m_breath_timer);
    }
}

void hal_led_deinit(void)
{
    if (!m_led.initialized)
    {
        return;
    }

    /* 先停呼吸：定时器不在对象树里，不停的话它会继续往已经删掉的 strip 写 */
    hal_led_breath_stop();

    hal_led_set_color(LED_COLOR_BLACK);

    if (m_rgb != NULL)
    {
        led_strip_del(m_rgb);
        m_rgb = NULL;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RGB_LED_WS2812_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    m_led.initialized = false;

    sys_logi(LED_TAG, "LED deinitialized");
}

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config =
    {
        .strip_gpio_num = RGB_LED_WS2812_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = RGB_LED_NUMBERS,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config =
    {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = RGB_LED_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    SYS_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    sys_logi(LED_TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

