#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_MAX_SCAN_RESULTS 12

typedef struct {
    char ssid[33];
    int8_t rssi;
} network_scan_record_t;

typedef struct {
    bool active;
    char ssid[33];
    char password[17];
    uint32_t seconds_remaining;
} network_portal_status_t;

esp_err_t network_manager_start(void);
bool network_manager_is_connected(void);
bool network_manager_is_setup_ap(void);
void network_manager_get_ip(char *out, size_t size);
void network_manager_get_connected_ssid(char *out, size_t size);
esp_err_t network_manager_start_scan(void);
size_t network_manager_get_scan_results(network_scan_record_t *out, size_t capacity,
                                        bool *complete);
esp_err_t network_manager_configure_wifi(const char *ssid, const char *password);
esp_err_t network_manager_start_pve_portal(void);
void network_manager_stop_pve_portal(void);
void network_manager_get_portal_status(network_portal_status_t *status);
void network_manager_notify_settings_changed(void);
bool network_manager_wait_for_settings_change(TickType_t timeout);
bool network_manager_wait_for_connection_or_settings_change(TickType_t timeout);

#ifdef __cplusplus
}
#endif
