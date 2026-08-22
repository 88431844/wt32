#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_snapshot.h"

typedef enum {
    NAS_DISK_SORT_ID = 0,
    NAS_DISK_SORT_MODEL,
    NAS_DISK_SORT_TEMPERATURE,
} nas_disk_sort_key_t;

void nas_disk_sort(nas_disk_t *disks, size_t count, nas_disk_sort_key_t key,
                   bool descending);
