#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_SDA_GPIO GPIO_NUM_18
#define TOUCH_SCL_GPIO GPIO_NUM_19
#define TOUCH_I2C_HZ 100000
#define LCD_BACKLIGHT_GPIO GPIO_NUM_23

static const char *TAG = "wt32_probe";

static esp_err_t read_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(
        TOUCH_I2C_PORT, address, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static bool probe_address(uint8_t address)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd, pdMS_TO_TICKS(40));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

static void configure_touch_bus(void)
{
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_HZ,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &config));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, config.mode, 0, 0, 0));
}

void app_main(void)
{
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    gpio_set_level(LCD_BACKLIGHT_GPIO, 1);

    esp_chip_info_t chip_info = {0};
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "WT32-SC01 hardware probe");
    ESP_LOGI(TAG, "ESP32 revision=%d cores=%d flash=%" PRIu32 " bytes",
             chip_info.revision, chip_info.cores, spi_flash_get_chip_size());
    ESP_LOGI(TAG, "PSRAM heap=%u bytes", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Touch bus: SDA=%d SCL=%d at %d Hz",
             TOUCH_SDA_GPIO, TOUCH_SCL_GPIO, TOUCH_I2C_HZ);

    configure_touch_bus();

    int found = 0;
    for (uint8_t address = 1; address < 0x7f; ++address) {
        if (!probe_address(address)) {
            continue;
        }
        ++found;
        ESP_LOGI(TAG, "I2C device found at 0x%02X", address);

        if (address == 0x38) {
            uint8_t chip_id = 0;
            uint8_t vendor_id = 0;
            uint8_t firmware_id = 0;
            esp_err_t chip_err = read_register(address, 0xA3, &chip_id);
            esp_err_t vendor_err = read_register(address, 0xA8, &vendor_id);
            esp_err_t firmware_err = read_register(address, 0xA6, &firmware_id);
            ESP_LOGI(TAG,
                     "FT5x06 candidate: chip=0x%02X (%s), vendor=0x%02X (%s), fw=0x%02X (%s)",
                     chip_id, esp_err_to_name(chip_err),
                     vendor_id, esp_err_to_name(vendor_err),
                     firmware_id, esp_err_to_name(firmware_err));
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "No I2C device acknowledged; check touch controller batch and reset line");
    }

    ESP_LOGI(TAG, "Probe complete; repeating scan every 5 seconds");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Touch 0x38=%s, GSL candidate 0x40=%s",
                 probe_address(0x38) ? "ACK" : "none",
                 probe_address(0x40) ? "ACK" : "none");
    }
}
