#include "network_manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_settings.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define PROVISIONING_TIMEOUT_MS (5 * 60 * 1000)
#define PORTAL_BODY_MAX 12288
#define PORTAL_CA_MAX 6144

typedef struct {
    char pve_host[64];
    char pve_node[33];
    char token_id[96];
    char token_secret[128];
    char ca[PORTAL_CA_MAX];
    char nas_host[64];
    char community[128];
} portal_form_t;

static const char *TAG = "provisioning";
static SemaphoreHandle_t s_lock;
static network_portal_status_t s_status;
static TickType_t s_started_at;
static httpd_handle_t s_http;
static TaskHandle_t s_dns_task;
static TaskHandle_t s_timeout_task;
static int s_dns_socket = -1;

static bool ensure_lock(void)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

static bool request_from_ap(httpd_req_t *request)
{
    struct sockaddr_storage address = {0};
    socklen_t length = sizeof(address);
    int socket_fd = httpd_req_to_sockfd(request);
    if (getpeername(socket_fd, (struct sockaddr *)&address, &length) != 0 ||
        address.ss_family != AF_INET) return false;
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)&address;
    return (ntohl(ipv4->sin_addr.s_addr) >> 8) == 0xC0A804UL;
}

static void url_decode(char *value)
{
    char *read = value;
    char *write = value;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            unsigned int byte = 0;
            if (sscanf(read + 1, "%2x", &byte) == 1) {
                *write++ = (char)byte;
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_value(const char *body, const char *key, char *out, size_t size)
{
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *start = body;
    while ((start = strstr(start, needle)) != NULL) {
        if (start == body || start[-1] == '&') break;
        start += strlen(needle);
    }
    if (start == NULL) return false;
    start += strlen(needle);
    const char *end = strchr(start, '&');
    size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
    if (length >= size) return false;
    memcpy(out, start, length);
    out[length] = '\0';
    url_decode(out);
    return true;
}

static esp_err_t reject_non_ap(httpd_req_t *request)
{
    if (request_from_ap(request)) return ESP_OK;
    httpd_resp_set_status(request, "403 Forbidden");
    httpd_resp_sendstr(request, "Forbidden");
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t portal_root(httpd_req_t *request)
{
    if (reject_non_ap(request) != ESP_OK) return ESP_OK;
    static const char page[] =
        "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>WT32 监控配置</title><style>body{font-family:sans-serif;max-width:560px;"
        "margin:24px auto;padding:0 16px;color:#182026}label{display:block;margin-top:14px}"
        "input,textarea{width:100%;box-sizing:border-box;padding:10px;margin-top:5px}"
        "button{margin-top:18px;padding:12px 20px;background:#1677ff;color:white;border:0}"
        ".hint{color:#65727d;font-size:13px}</style></head><body><h2>WT32 监控配置</h2>"
        "<p class=hint>空白的密码、Token Secret 与 CA 不会覆盖设备中已有内容。</p>"
        "<form method=post action=/save>"
        "<label>PVE 地址<input name=pve_host value='192.168.31.34' maxlength=63 required></label>"
        "<label>PVE 节点<input name=pve_node value='p330' maxlength=32 required></label>"
        "<label>PVE Token ID<input name=token_id maxlength=95 required></label>"
        "<label>PVE Token Secret<input name=token_secret type=password maxlength=127></label>"
        "<label>PVE CA PEM<textarea name=ca rows=9 maxlength=6143></textarea></label>"
        "<label>群晖地址<input name=nas_host value='192.168.31.105' maxlength=63 required></label>"
        "<label>只读 SNMP Community<input name=community type=password maxlength=127></label>"
        "<button type=submit>保存并刷新</button></form></body></html>";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(request, page);
}

static esp_err_t save_form(const portal_form_t *form)
{
    esp_err_t err = device_settings_set_string(DEVICE_KEY_PVE_HOST, form->pve_host);
    if (err == ESP_OK) err = device_settings_set_string(DEVICE_KEY_PVE_NODE, form->pve_node);
    if (err == ESP_OK) err = device_settings_set_string(DEVICE_KEY_PVE_TOKEN_ID, form->token_id);
    if (err == ESP_OK) err = device_settings_set_string_if_present(DEVICE_KEY_PVE_SECRET,
                                                                    form->token_secret);
    if (err == ESP_OK) err = device_settings_set_string_if_present(DEVICE_KEY_PVE_CA, form->ca);
    if (err == ESP_OK) err = device_settings_set_string(DEVICE_KEY_NAS_HOST, form->nas_host);
    if (err == ESP_OK) err = device_settings_set_string_if_present(DEVICE_KEY_SNMP_COMMUNITY,
                                                                    form->community);
    return err;
}

static void delayed_stop_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(350));
    network_manager_stop_pve_portal();
    vTaskDelete(NULL);
}

static esp_err_t portal_save(httpd_req_t *request)
{
    if (reject_non_ap(request) != ESP_OK) return ESP_OK;
    if (request->content_len == 0 || request->content_len >= PORTAL_BODY_MAX) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Invalid form size");
    }
    char *body = calloc(1, request->content_len + 1);
    portal_form_t *form = calloc(1, sizeof(*form));
    if (body == NULL || form == NULL) {
        free(body);
        free(form);
        return httpd_resp_send_500(request);
    }
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            free(body);
            free(form);
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    bool valid = form_value(body, "pve_host", form->pve_host, sizeof(form->pve_host)) &&
                 form_value(body, "pve_node", form->pve_node, sizeof(form->pve_node)) &&
                 form_value(body, "token_id", form->token_id, sizeof(form->token_id)) &&
                 form_value(body, "token_secret", form->token_secret, sizeof(form->token_secret)) &&
                 form_value(body, "ca", form->ca, sizeof(form->ca)) &&
                 form_value(body, "nas_host", form->nas_host, sizeof(form->nas_host)) &&
                 form_value(body, "community", form->community, sizeof(form->community)) &&
                 form->pve_host[0] != '\0' && form->pve_node[0] != '\0' &&
                 form->token_id[0] != '\0' && form->nas_host[0] != '\0';
    esp_err_t err = valid ? save_form(form) : ESP_ERR_INVALID_ARG;
    memset(form, 0, sizeof(*form));
    memset(body, 0, received);
    free(form);
    free(body);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Monitor settings save failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "<meta charset=utf-8><h3>保存失败，请检查字段后重试。</h3>");
    }
    network_manager_notify_settings_changed();
    esp_err_t response_err = httpd_resp_sendstr(
        request, "<meta charset=utf-8><h3>配置已保存，设备正在刷新数据。</h3>");
    xTaskCreate(delayed_stop_task, "portal_stop", 2048, NULL, 4, NULL);
    return response_err;
}

static esp_err_t captive_probe(httpd_req_t *request)
{
    if (reject_non_ap(request) != ESP_OK) return ESP_OK;
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t packet[512];
    struct sockaddr_in bind_address = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0 || bind(socket_fd, (struct sockaddr *)&bind_address,
                              sizeof(bind_address)) != 0) {
        if (socket_fd >= 0) close(socket_fd);
        ESP_LOGE(TAG, "Captive DNS start failed");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    s_dns_socket = socket_fd;
    while (true) {
        struct sockaddr_in client = {0};
        socklen_t client_length = sizeof(client);
        int length = recvfrom(socket_fd, packet, sizeof(packet) - 16, 0,
                              (struct sockaddr *)&client, &client_length);
        if (length < 12) {
            if (errno == EBADF) break;
            continue;
        }
        size_t question_end = 12;
        while (question_end < (size_t)length && packet[question_end] != 0) {
            question_end += (size_t)packet[question_end] + 1;
        }
        question_end += 5;
        if (question_end > (size_t)length || question_end + 16 > sizeof(packet)) continue;
        packet[2] = 0x81;
        packet[3] = 0x80;
        packet[6] = 0;
        packet[7] = 1;
        size_t offset = question_end;
        const uint8_t answer[] = {
            0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x04, 192, 168, 4, 1,
        };
        memcpy(packet + offset, answer, sizeof(answer));
        offset += sizeof(answer);
        sendto(socket_fd, packet, offset, 0, (struct sockaddr *)&client, client_length);
    }
    close(socket_fd);
    s_dns_socket = -1;
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void timeout_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(PROVISIONING_TIMEOUT_MS));
    s_timeout_task = NULL;
    network_manager_stop_pve_portal();
    vTaskDelete(NULL);
}

static esp_err_t register_http_handlers(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 10240;
    esp_err_t err = httpd_start(&s_http, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = portal_root},
        {.uri = "/save", .method = HTTP_POST, .handler = portal_save},
        {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_probe},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_probe},
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_probe},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_probe},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        err = httpd_register_uri_handler(s_http, &handlers[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t network_manager_start_pve_portal(void)
{
    if (!ensure_lock()) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_status.active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_status.ssid, sizeof(s_status.ssid), "WT32-Setup-%02X%02X", mac[4], mac[5]);
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    uint8_t random_bytes[12];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        s_status.password[i] = alphabet[random_bytes[i] % (sizeof(alphabet) - 1)];
    }
    s_status.password[sizeof(random_bytes)] = '\0';
    s_status.active = true;
    s_status.seconds_remaining = PROVISIONING_TIMEOUT_MS / 1000;
    s_started_at = xTaskGetTickCount();
    network_portal_status_t status = s_status;
    xSemaphoreGive(s_lock);

    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, status.ssid, strlen(status.ssid));
    memcpy(ap.ap.password, status.password, strlen(status.password));
    ap.ap.ssid_len = strlen(status.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK && xTaskCreate(dns_task, "captive_dns", 4096, NULL, 4,
                                      &s_dns_task) != pdPASS) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = register_http_handlers();
    if (err == ESP_OK && xTaskCreate(timeout_task, "portal_timeout", 2048, NULL, 3,
                                      &s_timeout_task) != pdPASS) err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        network_manager_stop_pve_portal();
        return err;
    }
    ESP_LOGI(TAG, "Temporary monitor setup AP started");
    return ESP_OK;
}

void network_manager_stop_pve_portal(void)
{
    if (!ensure_lock()) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) return;
    if (!s_status.active) {
        xSemaphoreGive(s_lock);
        return;
    }
    s_status.active = false;
    s_status.seconds_remaining = 0;
    memset(s_status.password, 0, sizeof(s_status.password));
    httpd_handle_t http = s_http;
    s_http = NULL;
    int dns_socket = s_dns_socket;
    TaskHandle_t timeout_handle = s_timeout_task;
    s_timeout_task = NULL;
    xSemaphoreGive(s_lock);

    if (http != NULL) httpd_stop(http);
    if (dns_socket >= 0) shutdown(dns_socket, SHUT_RDWR);
    if (timeout_handle != NULL && timeout_handle != xTaskGetCurrentTaskHandle()) {
        vTaskDelete(timeout_handle);
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Temporary monitor setup AP stopped");
}

void network_manager_get_portal_status(network_portal_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (!ensure_lock() || xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    *status = s_status;
    if (status->active) {
        TickType_t elapsed = xTaskGetTickCount() - s_started_at;
        uint32_t elapsed_seconds = elapsed / configTICK_RATE_HZ;
        const uint32_t total_seconds = PROVISIONING_TIMEOUT_MS / 1000;
        status->seconds_remaining = elapsed_seconds < total_seconds ?
                                    total_seconds - elapsed_seconds : 0;
    }
    xSemaphoreGive(s_lock);
}
