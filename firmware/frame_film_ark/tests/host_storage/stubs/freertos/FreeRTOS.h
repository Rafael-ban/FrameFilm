#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *TimerHandle_t;
typedef void *SemaphoreHandle_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR(x) ((void)(x))
#define pvPortMalloc malloc
#define vPortFree free
#endif
