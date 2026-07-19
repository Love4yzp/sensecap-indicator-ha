#pragma once
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;

#define portMAX_DELAY    ((TickType_t)0xFFFFFFFFU)
/* Host stub: 1 tick == 1 ms is sufficient; esp_event_stub dispatches
 * synchronously and ignores the timeout. Matches real FreeRTOS's macro name. */
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE           ((BaseType_t)1)
#define pdFALSE          ((BaseType_t)0)
#define pdPASS           pdTRUE
#define pdFAIL           pdFALSE
#define configMAX_TASK_NAME_LEN 16
