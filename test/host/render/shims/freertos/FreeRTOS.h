#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef void*    SemaphoreHandle_t;
typedef void*    QueueHandle_t;
typedef void*    TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE        1
#define pdFALSE       0
#define pdPASS        1
#define pdMS_TO_TICKS(ms) (ms)
