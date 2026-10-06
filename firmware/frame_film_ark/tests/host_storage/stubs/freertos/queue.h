#ifndef HOST_QUEUE_H
#define HOST_QUEUE_H
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(unsigned count, unsigned item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
#endif
