#include "board_wt32.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_SPI_HOST SPI2_HOST
#define LCD_PIN_MOSI GPIO_NUM_13
#define LCD_PIN_SCLK GPIO_NUM_14
#define LCD_PIN_CS GPIO_NUM_15
#define LCD_PIN_DC GPIO_NUM_21
#define LCD_PIN_RST GPIO_NUM_22
#define LCD_PIN_BACKLIGHT GPIO_NUM_23
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_PIN_SDA GPIO_NUM_18
#define TOUCH_PIN_SCL GPIO_NUM_19
#define TOUCH_ADDRESS 0x38
#define TOUCH_REGISTER_POINTS 0x02
#define TOUCH_NATIVE_WIDTH 320
#define TOUCH_NATIVE_HEIGHT 480

#define BACKLIGHT_TIMER LEDC_TIMER_0
#define BACKLIGHT_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_SPEED_MODE LEDC_HIGH_SPEED_MODE
#define BACKLIGHT_MAX_DUTY 1023

static const char *TAG = "board_wt32";
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static uint8_t s_brightness = 72;
static bool s_touch_ready;
static bool s_rotation_180 = true;

static esp_err_t init_backlight(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BACKLIGHT_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = LCD_PIN_BACKLIGHT,
        .speed_mode = BACKLIGHT_SPEED_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel");
    return ESP_OK;
}

static esp_err_t init_display(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = WT32_LCD_WIDTH * WT32_LCD_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                 &io_config, &s_panel_io),
        TAG, "LCD panel IO");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_panel_io, &panel_config, &s_panel),
                        TAG, "ST7796 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "LCD reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "LCD init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "LCD swap XY");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, s_rotation_180, s_rotation_180),
                        TAG, "LCD mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "LCD inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "LCD display on");
    return ESP_OK;
}

static esp_err_t init_touch(void)
{
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(TOUCH_I2C_PORT, &config), TAG, "touch I2C config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(TOUCH_I2C_PORT, config.mode, 0, 0, 0),
                        TAG, "touch I2C driver");

    uint8_t chip_register = 0xA3;
    uint8_t chip_id = 0;
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_ADDRESS, &chip_register, 1, &chip_id, 1,
        pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch controller did not acknowledge at 0x%02x", TOUCH_ADDRESS);
        return err;
    }
    ESP_LOGI(TAG, "Touch controller at 0x%02x, chip id 0x%02x", TOUCH_ADDRESS, chip_id);
    s_touch_ready = true;
    return ESP_OK;
}

esp_err_t wt32_board_init(void)
{
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight init");
    ESP_RETURN_ON_ERROR(init_display(), TAG, "display init");
    esp_err_t touch_err = init_touch();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "Touch disabled: %s", esp_err_to_name(touch_err));
    }
    wt32_board_set_brightness(s_brightness);
    ESP_LOGI(TAG, "WT32-SC01 board ready: ST7796, FT5x06 touch %s, 480x320",
             s_touch_ready ? "ready" : "unavailable");
    return ESP_OK;
}

esp_err_t wt32_board_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                 const void *pixels)
{
    return esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, pixels);
}

esp_err_t wt32_board_register_color_done_callback(
    esp_lcd_panel_io_color_trans_done_cb_t callback, void *user_context)
{
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = callback,
    };
    return esp_lcd_panel_io_register_event_callbacks(s_panel_io, &callbacks, user_context);
}

bool wt32_board_read_touch(wt32_touch_point_t *point)
{
    if (point == NULL || !s_touch_ready) {
        return false;
    }

    uint8_t reg = TOUCH_REGISTER_POINTS;
    uint8_t data[5] = {0};
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_ADDRESS, &reg, 1, data, sizeof(data),
        pdMS_TO_TICKS(20));
    if (err != ESP_OK || (data[0] & 0x0f) == 0) {
        point->pressed = false;
        return false;
    }

    const uint16_t raw_x = ((uint16_t)(data[1] & 0x0f) << 8) | data[2];
    const uint16_t raw_y = ((uint16_t)(data[3] & 0x0f) << 8) | data[4];
    if (raw_x >= TOUCH_NATIVE_WIDTH || raw_y >= TOUCH_NATIVE_HEIGHT) {
        point->pressed = false;
        return false;
    }

    int x;
    int y;
    if (s_rotation_180) {
        // Rotate the original WT32-SC01 landscape touch mapping by 180 degrees.
        x = (TOUCH_NATIVE_HEIGHT - 1) - raw_y;
        y = raw_x;
    } else {
        x = raw_y;
        y = (TOUCH_NATIVE_WIDTH - 1) - raw_x;
    }
    if (x < 0) x = 0;
    if (x >= WT32_LCD_WIDTH) x = WT32_LCD_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= WT32_LCD_HEIGHT) y = WT32_LCD_HEIGHT - 1;

    point->pressed = true;
    point->x = (uint16_t)x;
    point->y = (uint16_t)y;
    return true;
}

void wt32_board_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    uint32_t duty = (BACKLIGHT_MAX_DUTY * percent) / 100;
    ledc_set_duty(BACKLIGHT_SPEED_MODE, BACKLIGHT_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_SPEED_MODE, BACKLIGHT_CHANNEL);
}

uint8_t wt32_board_get_brightness(void)
{
    return s_brightness;
}

esp_err_t wt32_board_set_rotation_180(bool enabled)
{
    if (s_panel != NULL) {
        esp_err_t err = esp_lcd_panel_mirror(s_panel, enabled, enabled);
        if (err != ESP_OK) {
            return err;
        }
    }
    s_rotation_180 = enabled;
    return ESP_OK;
}

bool wt32_board_get_rotation_180(void)
{
    return s_rotation_180;
}
