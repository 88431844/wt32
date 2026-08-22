idf_component_register(SRCS "live_provider.c" "app_snapshot.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos esp_system network_manager esp_http_client esp-tls lwip json nvs_flash)
