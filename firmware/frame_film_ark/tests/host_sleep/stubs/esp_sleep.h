#ifndef HOST_ESP_SLEEP_H
#define HOST_ESP_SLEEP_H
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER } esp_sleep_wakeup_cause_t;
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
#endif
