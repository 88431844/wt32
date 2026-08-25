#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nas_pool_order.h"

static nas_pool_t pool(const char *name, uint64_t free_bytes)
{
    nas_pool_t value = {0};
    snprintf(value.name, sizeof(value.name), "%s", name);
    value.free_bytes = free_bytes;
    return value;
}

static uint64_t gibibytes(double value)
{
    return (uint64_t)(value * (double)(1ULL << 30));
}

static uint64_t tebibytes(double value)
{
    return (uint64_t)(value * (double)(1ULL << 40));
}

static void test_synology_pool_order_preserves_dsm_capacity_mapping(void)
{
    nas_pool_t pools[] = {
        pool("Volume 3", gibibytes(962.3)),
        pool("Volume 1", tebibytes(4.9)),
        pool("Volume 4", tebibytes(7.1)),
        pool("Volume 2", tebibytes(1.2)),
    };

    nas_pool_sort(pools, 4);

    const char *expected_names[] = {"Volume 1", "Volume 2", "Volume 3", "Volume 4"};
    const uint64_t expected_free[] = {
        tebibytes(4.9), tebibytes(1.2), gibibytes(962.3), tebibytes(7.1),
    };
    for (size_t i = 0; i < 4; ++i) {
        assert(strcmp(pools[i].name, expected_names[i]) == 0);
        assert(pools[i].free_bytes == expected_free[i]);
    }
}

static void test_synology_pool_number_comes_from_volume_name(void)
{
    size_t number = 0;
    assert(nas_pool_number("Volume 3", &number));
    assert(number == 3);
    assert(nas_pool_number("Volume 12", &number));
    assert(number == 12);
    assert(!nas_pool_number("Volume three", &number));
    assert(!nas_pool_number("Storage Pool 1", &number));
}

int main(void)
{
    test_synology_pool_order_preserves_dsm_capacity_mapping();
    test_synology_pool_number_comes_from_volume_name();
    puts("NAS pool order tests passed");
    return 0;
}
