#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_snapshot.h"

bool nas_pool_number(const char *name, size_t *number);
void nas_pool_sort(nas_pool_t *pools, size_t count);
