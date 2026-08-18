#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_SETUP_AP_SSID "InfoDisplay-Setup"

esp_err_t network_manager_start(void);
bool network_manager_is_connected(void);
bool network_manager_is_setup_ap(void);
void network_manager_get_ip(char *out, size_t size);

#ifdef __cplusplus
}
#endif
