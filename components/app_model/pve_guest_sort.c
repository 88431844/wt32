#include "pve_guest_sort.h"

#include <stdlib.h>

static int compare_guests(const void *left_pointer, const void *right_pointer)
{
    const pve_guest_t *left = left_pointer;
    const pve_guest_t *right = right_pointer;
    if (left->running != right->running) return left->running ? -1 : 1;
    if (left->vmid == right->vmid) return 0;
    return left->vmid < right->vmid ? -1 : 1;
}

void pve_guest_sort(pve_guest_t *guests, size_t count)
{
    if (guests == NULL || count < 2) return;
    qsort(guests, count, sizeof(*guests), compare_guests);
}
