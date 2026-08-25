#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "capacity_format.h"

static uint64_t gibibytes(double value)
{
    return (uint64_t)(value * (double)(1ULL << 30));
}

static uint64_t tebibytes(double value)
{
    return (uint64_t)(value * (double)(1ULL << 40));
}

static void expect_capacity(uint64_t bytes, const char *expected)
{
    char actual[24];
    dashboard_format_capacity(actual, sizeof(actual), bytes);
    assert(strcmp(actual, expected) == 0);
}

static void test_synology_available_capacity_keeps_one_decimal_place(void)
{
    expect_capacity(tebibytes(4.9), "4.9T");
    expect_capacity(tebibytes(1.2), "1.2T");
    expect_capacity(gibibytes(962.3), "962.3G");
    expect_capacity(tebibytes(7.1), "7.1T");
}

static void test_synology_used_capacity_keeps_one_decimal_place(void)
{
    expect_capacity(tebibytes(9.1), "9.1T");
    expect_capacity(gibibytes(500.3), "500.3G");
    expect_capacity(gibibytes(750.2), "750.2G");
    expect_capacity(tebibytes(34.8), "34.8T");
}

int main(void)
{
    test_synology_available_capacity_keeps_one_decimal_place();
    test_synology_used_capacity_keeps_one_decimal_place();
    puts("capacity format tests passed");
    return 0;
}
