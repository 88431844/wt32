#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "app_model.h"
#include "board_wt32.h"
#include "dashboard_ui.h"
#include "device_settings.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "network_manager.h"

static const char *TAG = "wt32_dashboard";
static lv_disp_drv_t s_display_driver;
static lv_disp_draw_buf_t s_display_buffer;
static portMUX_TYPE s_flush_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_first_frame_waiter;
static uint32_t s_flush_pending;
static bool s_first_frame_last_queued;
static bool s_first_frame_failed;
static bool color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *event_data,
                                void *user_context)
{
    (void)panel_io;
    (void)event_data;
    TaskHandle_t waiter = NULL;
    BaseType_t higher_priority_woken = pdFALSE;
    portENTER_CRITICAL_ISR(&s_flush_lock);
    if (s_flush_pending > 0) s_flush_pending--;
    if (s_first_frame_last_queued && s_flush_pending == 0) {
        waiter = s_first_frame_waiter;
        s_first_frame_waiter = NULL;
        s_first_frame_last_queued = false;
    }
    portEXIT_CRITICAL_ISR(&s_flush_lock);
    lv_disp_flush_ready((lv_disp_drv_t *)user_context);
    if (waiter != NULL) {
        vTaskNotifyGiveFromISR(waiter, &higher_priority_woken);
    }
    return higher_priority_woken == pdTRUE;
}

static void display_flush(lv_disp_drv_t *driver, const lv_area_t *area,
                          lv_color_t *pixels)
{
    portENTER_CRITICAL(&s_flush_lock);
    s_flush_pending++;
    if (lv_disp_flush_is_last(driver)) s_first_frame_last_queued = true;
    portEXIT_CRITICAL(&s_flush_lock);
    esp_err_t err = wt32_board_draw_bitmap(area->x1, area->y1,
                                            area->x2 + 1, area->y2 + 1, pixels);
    if (err != ESP_OK) {
        TaskHandle_t waiter = NULL;
        portENTER_CRITICAL(&s_flush_lock);
        if (s_first_frame_waiter != NULL) s_first_frame_failed = true;
        if (s_flush_pending > 0) s_flush_pending--;
        if (s_first_frame_last_queued && s_flush_pending == 0) {
            waiter = s_first_frame_waiter;
            s_first_frame_waiter = NULL;
            s_first_frame_last_queued = false;
        }
        portEXIT_CRITICAL(&s_flush_lock);
        ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(err));
        lv_disp_flush_ready(driver);
        if (waiter != NULL) xTaskNotifyGive(waiter);
    }
}

static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static lv_point_t last_point;
    wt32_touch_point_t point = {0};
    if (wt32_board_read_touch(&point)) {
        last_point.x = point.x;
        last_point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point = last_point;
}

static void lvgl_tick(void *argument)
{
    (void)argument;
    lv_tick_inc(2);
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t bootstrap_monitor_settings(void)
{
#ifdef WT32_BOOTSTRAP_SETTINGS
    const struct {
        const char *key;
        const char *value;
    } settings[] = {
        {DEVICE_KEY_PVE_HOST, WT32_BOOTSTRAP_PVE_HOST},
        {DEVICE_KEY_PVE_NODE, WT32_BOOTSTRAP_PVE_NODE},
        {DEVICE_KEY_PVE_TOKEN_ID, WT32_BOOTSTRAP_PVE_TOKEN_ID},
        {DEVICE_KEY_PVE_SECRET, WT32_BOOTSTRAP_PVE_SECRET},
        {DEVICE_KEY_PVE_CA, WT32_BOOTSTRAP_PVE_CA},
        {DEVICE_KEY_NAS_HOST, WT32_BOOTSTRAP_NAS_HOST},
        {DEVICE_KEY_SNMP_COMMUNITY, WT32_BOOTSTRAP_SNMP_COMMUNITY},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        esp_err_t err = device_settings_set_string(settings[i].key, settings[i].value);
        if (err != ESP_OK) return err;
    }
    ESP_LOGI(TAG, "Bootstrap monitor settings stored");
#endif
    return ESP_OK;
}

static esp_err_t init_lvgl(void)
{
    lv_init();

    const size_t buffer_pixels = WT32_LCD_WIDTH * WT32_LCD_DRAW_LINES;
    lv_color_t *buffer_a = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buffer_b = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer_a == NULL || buffer_b == NULL) {
        ESP_LOGE(TAG, "Unable to allocate LVGL DMA buffers");
        free(buffer_a);
        free(buffer_b);
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_display_buffer, buffer_a, buffer_b, buffer_pixels);
    lv_disp_drv_init(&s_display_driver);
    s_display_driver.hor_res = WT32_LCD_WIDTH;
    s_display_driver.ver_res = WT32_LCD_HEIGHT;
    s_display_driver.flush_cb = display_flush;
    s_display_driver.draw_buf = &s_display_buffer;
    lv_disp_drv_register(&s_display_driver);

    ESP_RETURN_ON_ERROR(
        wt32_board_register_color_done_callback(color_transfer_done, &s_display_driver),
        TAG, "LCD callback");

    static lv_indev_drv_t input_driver;
    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = touch_read;
    lv_indev_t *pointer = lv_indev_drv_register(&input_driver);
    if (pointer == NULL) {
        ESP_LOGE(TAG, "Unable to register LVGL touch input");
        return ESP_ERR_NO_MEM;
    }
    lv_timer_set_period(pointer->driver->read_timer, 10);

    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timer), TAG, "LVGL timer create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(timer, 2000), TAG, "LVGL timer start");
    return ESP_OK;
}

static void ui_task(void *argument)
{
    QueueHandle_t snapshot_queue = argument;
    ESP_ERROR_CHECK(init_lvgl());
    ESP_ERROR_CHECK(dashboard_ui_create());
    portENTER_CRITICAL(&s_flush_lock);
    s_first_frame_waiter = xTaskGetCurrentTaskHandle();
    s_flush_pending = 0;
    s_first_frame_last_queued = false;
    s_first_frame_failed = false;
    portEXIT_CRITICAL(&s_flush_lock);
    lv_timer_handler();
    const bool notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0;
    portENTER_CRITICAL(&s_flush_lock);
    const bool first_frame_failed = s_first_frame_failed;
    s_first_frame_waiter = NULL;
    portEXIT_CRITICAL(&s_flush_lock);
    if (notified && !first_frame_failed) {
        wt32_board_set_backlight_enabled(true);
    } else if (first_frame_failed) {
        ESP_LOGE(TAG, "First LCD frame failed; keeping backlight off");
    } else {
        ESP_LOGE(TAG, "First LCD frame did not finish; keeping backlight off");
    }

    app_model_event_t event;
    uint32_t update_count = 0;
    while (true) {
        if (xQueueReceive(snapshot_queue, &event, 0) == pdTRUE) {
            if (event.kind == APP_MODEL_EVENT_SNAPSHOT && event.snapshot != NULL && update_count < 3) {
                ESP_LOGI(TAG, "Applying UI revision=%" PRIu32, event.snapshot->revision);
            }
            const uint32_t revision = event.snapshot != NULL ? event.snapshot->revision : 0;
            dashboard_ui_update(&event);
            app_snapshot_destroy(event.snapshot);
            event.snapshot = NULL;
            update_count++;
            if (revision != 0 && update_count <= 3) {
                ESP_LOGI(TAG, "Applied UI revision=%" PRIu32, revision);
            }
            if (revision != 0 && update_count % 30 == 0) {
                ESP_LOGI(TAG, "UI revision=%" PRIu32 " free_internal=%u free_psram=%u",
                         revision,
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(bootstrap_monitor_settings());
    ESP_ERROR_CHECK(wt32_board_init());
    uint8_t rotate_180 = 1;
    uint8_t brightness = 72;
    (void)device_settings_get_u8(DEVICE_KEY_ROTATE_180, &rotate_180);
    (void)device_settings_get_u8(DEVICE_KEY_BRIGHTNESS, &brightness);
    if (brightness < 10 || brightness > 100) brightness = 72;
    ESP_ERROR_CHECK(wt32_board_set_rotation_180(rotate_180 != 0));
    wt32_board_set_brightness(brightness);
    ESP_ERROR_CHECK(network_manager_start());

    QueueHandle_t snapshot_queue = app_model_start_live_provider();
    ESP_ERROR_CHECK(snapshot_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    BaseType_t created = xTaskCreatePinnedToCore(
        ui_task, "dashboard_ui", 12 * 1024, snapshot_queue, 5, NULL, 1);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
