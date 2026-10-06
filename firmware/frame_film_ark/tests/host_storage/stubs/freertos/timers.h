#ifndef HOST_TIMERS_H
#define HOST_TIMERS_H
#include "FreeRTOS.h"
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t reload,
                           void *id, void (*callback)(TimerHandle_t));
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait);
#endif
