#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_SETTINGS_NAMESPACE "device"
#define DEVICE_KEY_SSID "ssid"
#define DEVICE_KEY_PASSWORD "password"
#define DEVICE_KEY_PVE_HOST "pve_host"
#define DEVICE_KEY_PVE_NODE "pve_node"
#define DEVICE_KEY_PVE_TOKEN_ID "pve_token_id"
#define DEVICE_KEY_PVE_SECRET "pve_secret"
#define DEVICE_KEY_PVE_CA "pve_ca"
#define DEVICE_KEY_NAS_HOST "nas_host"
#define DEVICE_KEY_SNMP_COMMUNITY "snmp_community"
#define DEVICE_KEY_ROTATE_180 "rotate180"
#define DEVICE_KEY_BRIGHTNESS "brightness"
#define DEVICE_KEY_NIGHT_ENABLED "night_on"
#define DEVICE_KEY_NIGHT_START "night_start"
#define DEVICE_KEY_NIGHT_END "night_end"
#define DEVICE_KEY_NIGHT_BRIGHTNESS "night_bright"
#define DEVICE_KEY_THEME "theme_id"
#define DEVICE_KEY_REFRESH "refresh_s"
#define DEVICE_KEY_HOME_PAGE "home_page"

esp_err_t device_settings_get_string(const char *key, char *out, size_t size);
esp_err_t device_settings_set_string(const char *key, const char *value);
esp_err_t device_settings_set_string_if_present(const char *key, const char *value);
esp_err_t device_settings_get_u8(const char *key, uint8_t *value);
esp_err_t device_settings_set_u8(const char *key, uint8_t value);

#ifdef __cplusplus
}
#endif
