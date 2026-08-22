idf_component_register(
    SRCS "network_manager.c" "device_settings.c" "provisioning_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server nvs_flash lwip)
