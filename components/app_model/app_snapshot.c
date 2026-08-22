#include "app_snapshot.h"

#include <stdlib.h>
#include <string.h>

static bool reserve_array(void **items, size_t *current_capacity,
                          size_t requested_capacity, size_t item_size)
{
    if (requested_capacity <= *current_capacity) return true;
    if (requested_capacity > SIZE_MAX / item_size) return false;

    size_t new_capacity = *current_capacity > 0 ? *current_capacity : 4;
    while (new_capacity < requested_capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = requested_capacity;
            break;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / item_size) return false;

    void *replacement = malloc(new_capacity * item_size);
    if (replacement == NULL) return false;
    if (*items != NULL && *current_capacity > 0) {
        memcpy(replacement, *items, *current_capacity * item_size);
    }
    free(*items);
    *items = replacement;
    *current_capacity = new_capacity;
    return true;
}

void app_snapshot_init(app_snapshot_t *snapshot)
{
    if (snapshot != NULL) memset(snapshot, 0, sizeof(*snapshot));
}

void app_snapshot_dispose(app_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    free(snapshot->pve_guests);
    free(snapshot->nas_pools);
    free(snapshot->nas_disks);
    app_snapshot_init(snapshot);
}

app_snapshot_t *app_snapshot_create(void)
{
    app_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (snapshot != NULL) app_snapshot_init(snapshot);
    return snapshot;
}

void app_snapshot_destroy(app_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    app_snapshot_dispose(snapshot);
    free(snapshot);
}

bool app_snapshot_reserve_pve_guests(app_snapshot_t *snapshot, size_t capacity)
{
    return snapshot != NULL && reserve_array((void **)&snapshot->pve_guests,
                                              &snapshot->pve_guest_capacity,
                                              capacity, sizeof(*snapshot->pve_guests));
}

bool app_snapshot_reserve_nas_pools(app_snapshot_t *snapshot, size_t capacity)
{
    return snapshot != NULL && reserve_array((void **)&snapshot->nas_pools,
                                              &snapshot->nas_pool_capacity,
                                              capacity, sizeof(*snapshot->nas_pools));
}

bool app_snapshot_reserve_nas_disks(app_snapshot_t *snapshot, size_t capacity)
{
    return snapshot != NULL && reserve_array((void **)&snapshot->nas_disks,
                                              &snapshot->nas_disk_capacity,
                                              capacity, sizeof(*snapshot->nas_disks));
}

bool app_snapshot_clone(app_snapshot_t *destination, const app_snapshot_t *source)
{
    if (destination == NULL || source == NULL) return false;
    if (destination == source) return true;

    app_snapshot_t candidate = *source;
    candidate.pve_guests = NULL;
    candidate.pve_guest_capacity = 0;
    candidate.nas_pools = NULL;
    candidate.nas_pool_capacity = 0;
    candidate.nas_disks = NULL;
    candidate.nas_disk_capacity = 0;

    if (!app_snapshot_reserve_pve_guests(&candidate, source->pve_guest_count) ||
        !app_snapshot_reserve_nas_pools(&candidate, source->nas_pool_count) ||
        !app_snapshot_reserve_nas_disks(&candidate, source->nas_disk_count)) {
        app_snapshot_dispose(&candidate);
        return false;
    }
    if (source->pve_guest_count > 0) {
        memcpy(candidate.pve_guests, source->pve_guests,
               source->pve_guest_count * sizeof(*source->pve_guests));
    }
    if (source->nas_pool_count > 0) {
        memcpy(candidate.nas_pools, source->nas_pools,
               source->nas_pool_count * sizeof(*source->nas_pools));
    }
    if (source->nas_disk_count > 0) {
        memcpy(candidate.nas_disks, source->nas_disks,
               source->nas_disk_count * sizeof(*source->nas_disks));
    }

    app_snapshot_dispose(destination);
    *destination = candidate;
    return true;
}

void app_snapshot_move(app_snapshot_t *destination, app_snapshot_t *source)
{
    if (destination == NULL || source == NULL || destination == source) return;
    app_snapshot_dispose(destination);
    *destination = *source;
    app_snapshot_init(source);
}
