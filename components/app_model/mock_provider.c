#include "app_model.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/task.h"
#include "network_manager.h"

static const char *TAG = "mock_provider";
static QueueHandle_t s_snapshot_queue;

static void mock_provider_task(void *argument)
{
    (void)argument;
    app_snapshot_t snapshot;
    app_snapshot_init(&snapshot);
    snapshot.revision = 1;

    ESP_LOGI(TAG, "Local Mock provider started");
    while (true) {
        time_t now = time(NULL);
        struct tm local_now;
        localtime_r(&now, &local_now);
        snapshot.revision++;
        snapshot.uptime_seconds++;
        snapshot.hour = (uint8_t)local_now.tm_hour;
        snapshot.minute = (uint8_t)local_now.tm_min;
        snapshot.second = (uint8_t)local_now.tm_sec;
        snapshot.wifi_connected = network_manager_is_connected();
        network_manager_get_ip(snapshot.ip_address, sizeof(snapshot.ip_address));

        app_snapshot_t *published = app_snapshot_create();
        if (published != NULL && app_snapshot_clone(published, &snapshot)) {
            app_model_event_t event = {
                .kind = APP_MODEL_EVENT_SNAPSHOT,
                .monitor = APP_MONITOR_NAS,
                .snapshot = published,
            };
            if (xQueueSend(s_snapshot_queue, &event, 0) != pdTRUE) {
                app_model_event_t dropped = {0};
                if (xQueueReceive(s_snapshot_queue, &dropped, 0) == pdTRUE)
                    app_snapshot_destroy(dropped.snapshot);
                if (xQueueSend(s_snapshot_queue, &event, 0) != pdTRUE)
                    app_snapshot_destroy(published);
            }
        } else {
            app_snapshot_destroy(published);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

QueueHandle_t app_model_start_mock_provider(void)
{
    s_snapshot_queue = xQueueCreate(1, sizeof(app_model_event_t));
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
