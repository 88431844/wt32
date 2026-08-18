#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t revision;
    uint32_t uptime_seconds;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool wifi_connected;
    char ip_address[16];
    float byd_price;
    float byd_change_percent;
    int weather_temperature;
    int weather_humidity;
    int pve_cpu;
    int pve_memory;
    int pve_storage;
    int nas_temperatures[4];
    int antigravity_remaining;
    int alert_critical;
    int alert_warning;
    int alert_info;
} app_snapshot_t;

QueueHandle_t app_model_start_mock_provider(void);

#ifdef __cplusplus
}
#endif
