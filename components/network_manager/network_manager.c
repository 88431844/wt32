#include "network_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define MAX_FORM_VALUE 96

static const char *TAG = "network_manager";
static EventGroupHandle_t s_events;
static bool s_setup_ap;
static httpd_handle_t s_http;
static char s_ip[16];

static void load_setting(const char *key, char *out, size_t size)
{
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open("device", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t required = size;
    if (nvs_get_str(nvs, key, out, &required) != ESP_OK) out[0] = '\0';
    nvs_close(nvs);
}

static esp_err_t save_setting(const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("device", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static void url_decode(char *value)
{
    char *src = value;
    char *dst = value;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            unsigned int byte;
            if (sscanf(src + 1, "%2x", &byte) == 1) {
                *dst++ = (char)byte;
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool form_value(const char *body, const char *key, char *out, size_t size)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *start = strstr(body, needle);
    if (!start) return false;
    start += strlen(needle);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= size) len = size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    url_decode(out);
    return true;
}

static esp_err_t portal_root(httpd_req_t *req)
{
    static const char head[] =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>InfoDisplay 配置</title><style>body{font-family:sans-serif;max-width:520px;margin:2em auto;padding:0 1em}"
        "input,select{width:100%;box-sizing:border-box;padding:.65em;margin:.3em 0 1em}button{padding:.7em 1.5em}"
        ".hint{color:#666;font-size:.9em}</style><h2>InfoDisplay 网络配置</h2>"
        "<form method=post action=/save><label>选择 Wi-Fi</label><select name=ssid>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, head);

    wifi_ap_record_t records[20] = {0};
    uint16_t count = 20;
    if (esp_wifi_scan_start(NULL, true) == ESP_OK && esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            if (records[i].ssid[0] == '\0') continue;
            char option[96];
            snprintf(option, sizeof(option), "<option value=\"%s\">%s (%d dBm)</option>",
                     records[i].ssid, records[i].ssid, records[i].rssi);
            httpd_resp_sendstr_chunk(req, option);
        }
    }
    static const char tail[] =
        "</select><div class=hint>如果列表中没有目标网络，也可以手动填写 SSID。</div>"
        "<input name=ssid_manual placeholder='手动输入 SSID'>Wi-Fi 密码<input name=password type=password>"
        "天气地区<input name=city value='深圳'>油价地区<input name=fuel_region value='深圳'>"
        "股票代码<input name=stock_symbol value='002594.SZ'>Gateway 地址<input name=gateway_url>"
        "<button>保存并重启连接</button></form>";
    httpd_resp_sendstr_chunk(req, tail);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t portal_save(httpd_req_t *req)
{
    char body[512];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';
    const char *keys[] = {"ssid_manual", "password", "city", "fuel_region", "stock_symbol", "gateway_url"};
    char value[MAX_FORM_VALUE];
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (form_value(body, keys[i], value, sizeof(value))) {
            save_setting(keys[i], value);
        }
    }
    if (!form_value(body, "ssid_manual", value, sizeof(value)) || value[0] == '\0') {
        if (form_value(body, "ssid", value, sizeof(value))) save_setting("ssid", value);
    } else {
        save_setting("ssid", value);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<meta charset=utf-8><h3>已保存，设备正在连接 Wi-Fi。</h3>");
}

static void start_portal(void)
{
    if (s_setup_ap) return;
    wifi_config_t config = {0};
    memcpy(config.ap.ssid, NETWORK_SETUP_AP_SSID, strlen(NETWORK_SETUP_AP_SSID));
    config.ap.ssid_len = strlen(NETWORK_SETUP_AP_SSID);
    config.ap.channel = 1;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "Unable to start setup AP: %s", esp_err_to_name(start_err));
        return;
    }
    s_setup_ap = true;
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = 80;
    http_config.stack_size = 8192;
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = portal_root};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = portal_save};
    if (httpd_start(&s_http, &http_config) == ESP_OK) {
        httpd_register_uri_handler(s_http, &root);
        httpd_register_uri_handler(s_http, &save);
    }
    ESP_LOGW(TAG, "Wi-Fi unavailable; setup AP %s at http://192.168.4.1", NETWORK_SETUP_AP_SSID);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        if (s_http != NULL) {
            httpd_stop(s_http);
            s_http = NULL;
        }
        if (s_setup_ap) {
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (mode_err != ESP_OK) {
                ESP_LOGW(TAG, "Unable to stop setup AP: %s", esp_err_to_name(mode_err));
            } else {
                ESP_LOGI(TAG, "Setup AP closed after STA acquired IP");
            }
        }
        s_setup_ap = false;
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        ESP_LOGI(TAG, "Wi-Fi connected; SNTP started");
    }
}

static void connection_task(void *arg)
{
    (void)arg;
    char ssid[33];
    char password[65];
    load_setting("ssid", ssid, sizeof(ssid));
    load_setting("password", password, sizeof(password));
    if (ssid[0] == '\0') {
        start_portal();
        vTaskDelete(NULL);
        return;
    }
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if ((bits & WIFI_CONNECTED_BIT) == 0) start_portal();
    vTaskDelete(NULL);
}

esp_err_t network_manager_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "ip events");
    xTaskCreate(connection_task, "wifi_connect", 4096, NULL, 4, NULL);
    return ESP_OK;
}

bool network_manager_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT);
}

bool network_manager_is_setup_ap(void)
{
    return s_setup_ap;
}

void network_manager_get_ip(char *out, size_t size)
{
    if (out == NULL || size == 0) return;
    if (s_setup_ap) {
        snprintf(out, size, "192.168.4.1");
    } else if (network_manager_is_connected() && s_ip[0] != '\0') {
        snprintf(out, size, "%s", s_ip);
    } else {
        snprintf(out, size, "WiFi...");
    }
}
