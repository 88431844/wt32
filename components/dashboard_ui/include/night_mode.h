#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIGHT_MODE_MIN_BRIGHTNESS 5
#define NIGHT_MODE_MAX_BRIGHTNESS 50
#define NIGHT_MODE_BRIGHTNESS_GAP 5

bool night_mode_hour_is_active(uint8_t start_hour, uint8_t end_hour,
                               uint8_t current_hour);
uint8_t night_mode_max_brightness(uint8_t daytime_brightness);
uint8_t night_mode_limit_brightness(uint8_t daytime_brightness,
                                    uint8_t requested_brightness);

#ifdef __cplusplus
}
#endif
