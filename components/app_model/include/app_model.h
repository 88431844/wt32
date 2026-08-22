#pragma once

#include <stdint.h>

#include "app_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t app_model_start_mock_provider(void);
QueueHandle_t app_model_start_live_provider(void);
void app_model_set_active_monitor(app_monitor_t monitor);
void app_model_set_refresh_seconds(uint8_t seconds);

#ifdef __cplusplus
}
#endif
