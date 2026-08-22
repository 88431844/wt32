#include "network_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "device_settings.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"

#define WIFI_CONNECTED_BIT BIT0
#define SETTINGS_CHANGED_BIT BIT1
#define WIFI_CONNECT_TIMEOUT_MS 15000

typedef struct {
    char ssid[33];
    char password[65];
} wifi_request_t;

static const char *TAG = "network_manager";
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_lock;
static bool s_scan_running;
static bool s_scan_complete;
static bool s_configure_running;
static bool s_sntp_started;
static network_scan_record_t s_scan_results[NETWORK_MAX_SCAN_RESULTS];
static size_t s_scan_count;
static char s_ip[16];
static char s_connected_ssid[33];

static void start_sntp_once(void)
{
    if (s_sntp_started) return;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    s_sntp_started = true;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            s_ip[0] = '\0';
            s_connected_ssid[0] = '\0';
            xSemaphoreGive(s_lock);
        }
        esp_wifi_connect();
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        wifi_ap_record_t ap = {0};
        if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                snprintf(s_connected_ssid, sizeof(s_connected_ssid), "%s", (const char *)ap.ssid);
            }
            xSemaphoreGive(s_lock);
        }
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        start_sntp_once();
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static void scan_task(void *arg)
{
    (void)arg;
    wifi_ap_record_t records[NETWORK_MAX_SCAN_RESULTS * 2] = {0};
    uint16_t count = sizeof(records) / sizeof(records[0]);
    size_t stored = 0;
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);

    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        memset(s_scan_results, 0, sizeof(s_scan_results));
        if (err == ESP_OK) {
            for (uint16_t i = 0; i < count && stored < NETWORK_MAX_SCAN_RESULTS; ++i) {
                if (records[i].ssid[0] == '\0') continue;
                bool duplicate = false;
                for (size_t j = 0; j < stored; ++j) {
                    if (strcmp(s_scan_results[j].ssid, (const char *)records[i].ssid) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                snprintf(s_scan_results[stored].ssid, sizeof(s_scan_results[stored].ssid),
                         "%s", (const char *)records[i].ssid);
                s_scan_results[stored].rssi = records[i].rssi;
                stored++;
            }
        }
        s_scan_count = stored;
        s_scan_complete = true;
        s_scan_running = false;
        xSemaphoreGive(s_lock);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}

static void configure_wifi_task(void *arg)
{
    wifi_request_t *request = (wifi_request_t *)arg;
    wifi_config_t previous = {0};
    wifi_config_t next = {0};
    esp_wifi_get_config(WIFI_IF_STA, &previous);
    memcpy(next.sta.ssid, request->ssid, strlen(request->ssid));
    memcpy(next.sta.password, request->password, strlen(request->password));

    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &next);
    if (err == ESP_OK) err = esp_wifi_connect();
    EventBits_t bits = 0;
    if (err == ESP_OK) {
        bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    }
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        err = device_settings_set_string(DEVICE_KEY_SSID, request->ssid);
        if (err == ESP_OK) err = device_settings_set_string(DEVICE_KEY_PASSWORD, request->password);
        if (err == ESP_OK) ESP_LOGI(TAG, "Wi-Fi configuration saved");
        else ESP_LOGE(TAG, "Unable to persist Wi-Fi configuration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "New Wi-Fi connection failed; restoring previous network");
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &previous);
        esp_wifi_connect();
    }

    memset(request, 0, sizeof(*request));
    free(request);
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_configure_running = false;
        xSemaphoreGive(s_lock);
    }
    vTaskDelete(NULL);
}

esp_err_t network_manager_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    s_events = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();
    if (s_events == NULL || s_lock == NULL) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) return event_err;
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    event_handler, NULL), TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    event_handler, NULL), TAG, "ip events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi station mode");

    char ssid[33] = {0};
    char password[65] = {0};
    if (device_settings_get_string(DEVICE_KEY_SSID, ssid, sizeof(ssid)) == ESP_OK &&
        ssid[0] != '\0') {
        device_settings_get_string(DEVICE_KEY_PASSWORD, password, sizeof(password));
        wifi_config_t config = {0};
        memcpy(config.sta.ssid, ssid, strlen(ssid));
        memcpy(config.sta.password, password, strlen(password));
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "wifi config");
    }
    memset(password, 0, sizeof(password));
    return esp_wifi_start();
}

bool network_manager_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

bool network_manager_is_setup_ap(void)
{
    network_portal_status_t status = {0};
    network_manager_get_portal_status(&status);
    return status.active;
}

void network_manager_get_ip(char *out, size_t size)
{
    if (out == NULL || size == 0) return;
    out[0] = '\0';
    if (network_manager_is_connected() && s_lock != NULL &&
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(out, size, "%s", s_ip);
        xSemaphoreGive(s_lock);
    }
}

void network_manager_get_connected_ssid(char *out, size_t size)
{
    if (out == NULL || size == 0) return;
    out[0] = '\0';
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        snprintf(out, size, "%s", s_connected_ssid);
        xSemaphoreGive(s_lock);
    }
}

esp_err_t network_manager_start_scan(void)
{
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_scan_running) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_running = true;
    s_scan_complete = false;
    s_scan_count = 0;
    xSemaphoreGive(s_lock);
    if (xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 4, NULL) != pdPASS) {
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_scan_running = false;
            xSemaphoreGive(s_lock);
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

size_t network_manager_get_scan_results(network_scan_record_t *out, size_t capacity,
                                        bool *complete)
{
    if (complete != NULL) *complete = false;
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    size_t count = s_scan_count < capacity ? s_scan_count : capacity;
    if (out != NULL && count > 0) memcpy(out, s_scan_results, count * sizeof(*out));
    if (complete != NULL) *complete = s_scan_complete;
    xSemaphoreGive(s_lock);
    return count;
}

esp_err_t network_manager_configure_wifi(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) return ESP_ERR_INVALID_ARG;
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 64 ||
        (password_length > 0 && password_length < 8)) return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_configure_running) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_configure_running = true;
    xSemaphoreGive(s_lock);

    wifi_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_configure_running = false;
            xSemaphoreGive(s_lock);
        }
        return ESP_ERR_NO_MEM;
    }
    snprintf(request->ssid, sizeof(request->ssid), "%s", ssid);
    snprintf(request->password, sizeof(request->password), "%s", password);
    if (xTaskCreate(configure_wifi_task, "wifi_config", 4096, request, 4, NULL) != pdPASS) {
        memset(request, 0, sizeof(*request));
        free(request);
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            s_configure_running = false;
            xSemaphoreGive(s_lock);
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void network_manager_notify_settings_changed(void)
{
    if (s_events != NULL) xEventGroupSetBits(s_events, SETTINGS_CHANGED_BIT);
}

bool network_manager_wait_for_settings_change(TickType_t timeout)
{
    if (s_events == NULL) {
        vTaskDelay(timeout);
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, SETTINGS_CHANGED_BIT, pdTRUE,
                                           pdFALSE, timeout);
    return (bits & SETTINGS_CHANGED_BIT) != 0;
}
