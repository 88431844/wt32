idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board_wt32 app_model dashboard_ui nvs_flash)
