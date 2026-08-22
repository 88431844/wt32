idf_component_register(SRCS "dashboard_ui.c"
                            "assets/dashboard_icons.c"
                            "fonts/app_font_11.c"
                            "fonts/app_font_14.c"
                            "fonts/app_font_18.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl app_model board_wt32 network_manager nvs_flash heap)
