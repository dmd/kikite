#pragma once
#include "freertos/FreeRTOS.h"
#define xSemaphoreCreateMutex()        ((SemaphoreHandle_t)1)
#define xSemaphoreTake(handle, ticks)  pdTRUE
#define xSemaphoreGive(handle)         pdTRUE
