#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *TimerHandle_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(x) ((void)(x))
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t auto_reload,
                           void *id, void (*callback)(TimerHandle_t));
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait);
#endif
