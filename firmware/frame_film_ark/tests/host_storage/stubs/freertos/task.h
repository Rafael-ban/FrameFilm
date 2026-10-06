#ifndef HOST_TASK_H
#define HOST_TASK_H
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
#endif
