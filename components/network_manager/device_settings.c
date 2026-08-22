#include "device_settings.h"

#include <stdbool.h>
#include <string.h>

#include "nvs.h"

static bool valid_key(const char *key)
{
    return key != NULL && key[0] != '\0' && strlen(key) < NVS_KEY_NAME_MAX_SIZE;
}

esp_err_t device_settings_get_string(const char *key, char *out, size_t size)
{
    if (!valid_key(key) || out == NULL || size == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t required = size;
    err = nvs_get_str(handle, key, out, &required);
    nvs_close(handle);
    if (err != ESP_OK) out[0] = '\0';
    return err;
}

esp_err_t device_settings_set_string(const char *key, const char *value)
{
    if (!valid_key(key) || value == NULL) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t device_settings_set_string_if_present(const char *key, const char *value)
{
    if (!valid_key(key) || value == NULL) return ESP_ERR_INVALID_ARG;
    if (value[0] == '\0') return ESP_OK;
    return device_settings_set_string(key, value);
}

esp_err_t device_settings_get_u8(const char *key, uint8_t *value)
{
    if (!valid_key(key) || value == NULL) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_u8(handle, key, value);
    nvs_close(handle);
    return err;
}

esp_err_t device_settings_set_u8(const char *key, uint8_t value)
{
    if (!valid_key(key)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DEVICE_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
