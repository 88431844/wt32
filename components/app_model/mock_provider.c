#include "app_model.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "mock_provider";
static QueueHandle_t s_snapshot_queue;

static void mock_provider_task(void *argument)
{
    (void)argument;
    app_snapshot_t snapshot = {
        .revision = 1,
        .hour = 22,
        .minute = 18,
        .second = 36,
        .byd_price = 108.62f,
        .byd_change_percent = 2.20f,
        .weather_temperature = 29,
        .weather_humidity = 80,
        .pve_cpu = 24,
        .pve_memory = 61,
        .pve_storage = 72,
        .nas_temperatures = {36, 38, 39, 37},
        .antigravity_remaining = 68,
        .alert_critical = 1,
        .alert_warning = 1,
        .alert_info = 1,
    };

    ESP_LOGI(TAG, "Local Mock provider started");
    while (true) {
        snapshot.revision++;
        snapshot.uptime_seconds++;
        snapshot.second++;
        if (snapshot.second >= 60) {
            snapshot.second = 0;
            snapshot.minute++;
        }
        if (snapshot.minute >= 60) {
            snapshot.minute = 0;
            snapshot.hour = (snapshot.hour + 1) % 24;
        }

        const int wave = (int)(snapshot.uptime_seconds % 11) - 5;
        snapshot.byd_price = 108.62f + wave * 0.07f;
        snapshot.byd_change_percent = 2.20f + wave * 0.03f;
        snapshot.weather_temperature = 29 + ((snapshot.uptime_seconds / 10) % 3) - 1;
        snapshot.weather_humidity = 80 + ((snapshot.uptime_seconds / 7) % 5) - 2;
        snapshot.pve_cpu = 24 + wave;
        snapshot.pve_memory = 61 + ((snapshot.uptime_seconds / 3) % 5) - 2;
        snapshot.nas_temperatures[2] = 39 + ((snapshot.uptime_seconds / 5) % 3) - 1;
        if (snapshot.uptime_seconds % 15 == 0 && snapshot.antigravity_remaining > 1) {
            snapshot.antigravity_remaining--;
        }

        xQueueOverwrite(s_snapshot_queue, &snapshot);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

QueueHandle_t app_model_start_mock_provider(void)
{
    s_snapshot_queue = xQueueCreate(1, sizeof(app_snapshot_t));
    if (s_snapshot_queue == NULL) {
        ESP_LOGE(TAG, "Unable to allocate snapshot queue");
        return NULL;
    }

    BaseType_t created = xTaskCreatePinnedToCore(
        mock_provider_task, "mock_provider", 4096, NULL, 3, NULL, 0);
    if (created != pdPASS) {
        vQueueDelete(s_snapshot_queue);
        s_snapshot_queue = NULL;
        ESP_LOGE(TAG, "Unable to create Mock provider task");
    }
    return s_snapshot_queue;
}
