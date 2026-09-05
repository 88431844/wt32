#include "night_mode.h"

bool night_mode_hour_is_active(uint8_t start_hour, uint8_t end_hour,
                               uint8_t current_hour)
{
    if (start_hour >= 24 || end_hour >= 24 || current_hour >= 24 ||
        start_hour == end_hour) {
        return false;
    }
    if (start_hour < end_hour) {
        return current_hour >= start_hour && current_hour < end_hour;
    }
    return current_hour >= start_hour || current_hour < end_hour;
}

uint8_t night_mode_max_brightness(uint8_t daytime_brightness)
{
    if (daytime_brightness <= NIGHT_MODE_BRIGHTNESS_GAP) return 0;
    uint8_t maximum = daytime_brightness - NIGHT_MODE_BRIGHTNESS_GAP;
    return maximum < NIGHT_MODE_MAX_BRIGHTNESS ? maximum : NIGHT_MODE_MAX_BRIGHTNESS;
}

uint8_t night_mode_limit_brightness(uint8_t daytime_brightness,
                                    uint8_t requested_brightness)
{
    const uint8_t maximum = night_mode_max_brightness(daytime_brightness);
    if (requested_brightness > maximum) requested_brightness = maximum;
    if (requested_brightness < NIGHT_MODE_MIN_BRIGHTNESS &&
        maximum >= NIGHT_MODE_MIN_BRIGHTNESS) {
        requested_brightness = NIGHT_MODE_MIN_BRIGHTNESS;
    }
    return requested_brightness;
}
