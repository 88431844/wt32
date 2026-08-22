#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nas_disk_sort.h"

static nas_disk_t disk(const char *id, const char *model, int temperature,
                       bool temperature_valid)
{
    nas_disk_t value = {0};
    snprintf(value.id, sizeof(value.id), "%s", id);
    snprintf(value.model, sizeof(value.model), "%s", model);
    value.temperature_c = temperature;
    value.temperature_valid = temperature_valid;
    return value;
}

static void test_disk_ids_use_natural_numeric_order(void)
{
    nas_disk_t disks[] = {
        disk("disk 10", "D", 40, true),
        disk("disk 2", "B", 38, true),
        disk("disk 1", "A", 37, true),
        disk("disk 9", "C", 39, true),
    };

    nas_disk_sort(disks, 4, NAS_DISK_SORT_ID, false);
    assert(strcmp(disks[0].id, "disk 1") == 0);
    assert(strcmp(disks[1].id, "disk 2") == 0);
    assert(strcmp(disks[2].id, "disk 9") == 0);
    assert(strcmp(disks[3].id, "disk 10") == 0);

    nas_disk_sort(disks, 4, NAS_DISK_SORT_ID, true);
    assert(strcmp(disks[0].id, "disk 10") == 0);
    assert(strcmp(disks[1].id, "disk 9") == 0);
    assert(strcmp(disks[2].id, "disk 2") == 0);
    assert(strcmp(disks[3].id, "disk 1") == 0);
}

static void test_models_are_case_insensitive_and_missing_values_are_last(void)
{
    nas_disk_t disks[] = {
        disk("disk 4", "", 40, true),
        disk("disk 3", "zeta", 39, true),
        disk("disk 2", "alpha", 38, true),
        disk("disk 1", "Alpha", 37, true),
    };

    nas_disk_sort(disks, 4, NAS_DISK_SORT_MODEL, false);
    assert(strcmp(disks[0].id, "disk 1") == 0);
    assert(strcmp(disks[1].id, "disk 2") == 0);
    assert(strcmp(disks[2].id, "disk 3") == 0);
    assert(strcmp(disks[3].id, "disk 4") == 0);

    nas_disk_sort(disks, 4, NAS_DISK_SORT_MODEL, true);
    assert(strcmp(disks[0].id, "disk 3") == 0);
    assert(strcmp(disks[1].id, "disk 1") == 0);
    assert(strcmp(disks[2].id, "disk 2") == 0);
    assert(strcmp(disks[3].id, "disk 4") == 0);
}

static void test_invalid_temperatures_stay_last_in_both_directions(void)
{
    nas_disk_t disks[] = {
        disk("disk 4", "D", 42, true),
        disk("disk 2", "B", 0, false),
        disk("disk 1", "A", 36, true),
        disk("disk 3", "C", 42, true),
    };

    nas_disk_sort(disks, 4, NAS_DISK_SORT_TEMPERATURE, false);
    assert(strcmp(disks[0].id, "disk 1") == 0);
    assert(strcmp(disks[1].id, "disk 3") == 0);
    assert(strcmp(disks[2].id, "disk 4") == 0);
    assert(strcmp(disks[3].id, "disk 2") == 0);

    nas_disk_sort(disks, 4, NAS_DISK_SORT_TEMPERATURE, true);
    assert(strcmp(disks[0].id, "disk 3") == 0);
    assert(strcmp(disks[1].id, "disk 4") == 0);
    assert(strcmp(disks[2].id, "disk 1") == 0);
    assert(strcmp(disks[3].id, "disk 2") == 0);
}

int main(void)
{
    test_disk_ids_use_natural_numeric_order();
    test_models_are_case_insensitive_and_missing_values_are_last();
    test_invalid_temperatures_stay_last_in_both_directions();
    puts("nas disk sort tests passed");
    return 0;
}
