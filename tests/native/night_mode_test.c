#include "night_mode.h"

#include <assert.h>
#include <stdio.h>

static void test_cross_midnight_schedule(void)
{
    assert(night_mode_hour_is_active(22, 7, 22));
    assert(night_mode_hour_is_active(22, 7, 23));
    assert(night_mode_hour_is_active(22, 7, 0));
    assert(night_mode_hour_is_active(22, 7, 6));
    assert(!night_mode_hour_is_active(22, 7, 7));
    assert(!night_mode_hour_is_active(22, 7, 21));
}

static void test_same_day_schedule(void)
{
    assert(!night_mode_hour_is_active(8, 18, 7));
    assert(night_mode_hour_is_active(8, 18, 8));
    assert(night_mode_hour_is_active(8, 18, 17));
    assert(!night_mode_hour_is_active(8, 18, 18));
}

static void test_empty_and_invalid_schedules_are_inactive(void)
{
    assert(!night_mode_hour_is_active(7, 7, 7));
    assert(!night_mode_hour_is_active(24, 7, 0));
    assert(!night_mode_hour_is_active(22, 24, 0));
    assert(!night_mode_hour_is_active(22, 7, 24));
}

static void test_night_brightness_stays_below_daytime(void)
{
    assert(night_mode_max_brightness(100) == 50);
    assert(night_mode_max_brightness(72) == 50);
    assert(night_mode_max_brightness(20) == 15);
    assert(night_mode_limit_brightness(72, 20) == 20);
    assert(night_mode_limit_brightness(20, 20) == 15);
    assert(night_mode_limit_brightness(10, 1) == 5);
}

int main(void)
{
    test_cross_midnight_schedule();
    test_same_day_schedule();
    test_empty_and_invalid_schedules_are_inactive();
    test_night_brightness_stays_below_daytime();
    puts("night mode tests passed");
    return 0;
}
