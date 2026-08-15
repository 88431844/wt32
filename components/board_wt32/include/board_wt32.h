#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WT32_LCD_WIDTH 480
#define WT32_LCD_HEIGHT 320
#define WT32_LCD_DRAW_LINES 20

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} wt32_touch_point_t;

esp_err_t wt32_board_init(void);
esp_err_t wt32_board_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                 const void *pixels);
esp_err_t wt32_board_register_color_done_callback(
    esp_lcd_panel_io_color_trans_done_cb_t callback, void *user_context);
bool wt32_board_read_touch(wt32_touch_point_t *point);
void wt32_board_set_brightness(uint8_t percent);
uint8_t wt32_board_get_brightness(void);

#ifdef __cplusplus
}
#endif
