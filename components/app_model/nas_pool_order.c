#include "nas_pool_order.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool nas_pool_number(const char *name, size_t *number)
{
    static const char prefix[] = "Volume ";
    if (name == NULL || number == NULL || strncmp(name, prefix, sizeof(prefix) - 1) != 0)
        return false;

    const char *cursor = name + sizeof(prefix) - 1;
    if (*cursor < '0' || *cursor > '9') return false;

    size_t value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        const size_t digit = (size_t)(*cursor - '0');
        if (value > (SIZE_MAX - digit) / 10) return false;
        value = value * 10 + digit;
        cursor++;
    }
    if (*cursor != '\0' || value == 0) return false;
    *number = value;
    return true;
}

static int compare_pools(const void *left_pointer, const void *right_pointer)
{
    const nas_pool_t *left = left_pointer;
    const nas_pool_t *right = right_pointer;
    size_t left_number = 0;
    size_t right_number = 0;
    const bool left_valid = nas_pool_number(left->name, &left_number);
    const bool right_valid = nas_pool_number(right->name, &right_number);

    if (left_valid != right_valid) return left_valid ? -1 : 1;
    if (left_valid && left_number != right_number)
        return left_number < right_number ? -1 : 1;
    return strcmp(left->name, right->name);
}

void nas_pool_sort(nas_pool_t *pools, size_t count)
{
    if (pools == NULL || count < 2) return;
    qsort(pools, count, sizeof(*pools), compare_pools);
}
