#include "capacity_format.h"

#include <inttypes.h>
#include <stdio.h>

void dashboard_format_capacity(char *out, size_t size, uint64_t bytes)
{
    if (out == NULL || size == 0) return;
    if (bytes >= (1ULL << 40))
        snprintf(out, size, "%.1fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (1ULL << 30))
        snprintf(out, size, "%.1fG", (double)bytes / (1ULL << 30));
    else if (bytes >= (1ULL << 20))
        snprintf(out, size, "%.1fM", (double)bytes / (1ULL << 20));
    else
        snprintf(out, size, "%" PRIu64 "B", bytes);
}
