#ifndef HOST_HAL_API_H
#define HOST_HAL_API_H
#include <stdbool.h>
#include <stdint.h>
#define LED_COLOR_RED 0xff0000u
#define LED_COLOR_GREEN 0x00ff00u
#define LED_COLOR_WHITE 0xffffffu
#define LED_COLOR_BLACK 0u
enum { INPUT_PRESS_SHORT, INPUT_PRESS_LONG, INPUT_PRESS_UP, INPUT_PRESS_DOWN };
void hal_input_register_cb(int press, void (*cb)(void));
void hal_led_breath_stop(void);
void hal_led_set_color(uint32_t color);
void hal_led_breath_start(uint32_t color, int min, int max, int period);
int hal_bat_get_level(void);
void hal_pwr_set_timer_wakeup(uint16_t minutes);
bool hal_pwr_wake_condition_met(void);
void hal_led_deinit(void);
void hal_bat_deinit(void);
void hal_sd_deinit(void);
void hal_epd_deinit(void);
void hal_input_deinit(void);
void hal_pwr_enter_sleep(void);
#endif
