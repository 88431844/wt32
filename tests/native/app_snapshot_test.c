#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_snapshot.h"

static void populate_snapshot(app_snapshot_t *snapshot)
{
    assert(app_snapshot_reserve_pve_guests(snapshot, 25));
    assert(app_snapshot_reserve_nas_pools(snapshot, 12));
    assert(app_snapshot_reserve_nas_disks(snapshot, 10));

    snapshot->pve_guest_count = 25;
    snapshot->nas_pool_count = 12;
    snapshot->nas_disk_count = 10;
    for (size_t i = 0; i < snapshot->pve_guest_count; ++i) {
        snapshot->pve_guests[i].vmid = (uint32_t)(100 + i);
        snprintf(snapshot->pve_guests[i].name,
                 sizeof(snapshot->pve_guests[i].name), "guest-%zu", i);
    }
    for (size_t i = 0; i < snapshot->nas_pool_count; ++i) {
        snprintf(snapshot->nas_pools[i].name,
                 sizeof(snapshot->nas_pools[i].name), "pool-%zu", i);
    }
    for (size_t i = 0; i < snapshot->nas_disk_count; ++i) {
        snprintf(snapshot->nas_disks[i].id,
                 sizeof(snapshot->nas_disks[i].id), "disk-%zu", i);
    }
}

static void test_large_dynamic_collections_clone_and_move(void)
{
    app_snapshot_t source;
    app_snapshot_init(&source);
    populate_snapshot(&source);

    app_snapshot_t clone;
    app_snapshot_init(&clone);
    assert(app_snapshot_clone(&clone, &source));
    assert(clone.pve_guest_count == 25);
    assert(clone.nas_pool_count == 12);
    assert(clone.nas_disk_count == 10);
    assert(clone.pve_guests != source.pve_guests);
    assert(clone.nas_pools != source.nas_pools);
    assert(clone.nas_disks != source.nas_disks);
    assert(strcmp(clone.pve_guests[24].name, "guest-24") == 0);
    assert(strcmp(clone.nas_pools[11].name, "pool-11") == 0);
    assert(strcmp(clone.nas_disks[9].id, "disk-9") == 0);

    app_snapshot_t moved;
    app_snapshot_init(&moved);
    app_snapshot_move(&moved, &clone);
    assert(moved.pve_guest_count == 25);
    assert(moved.nas_pool_count == 12);
    assert(moved.nas_disk_count == 10);
    assert(clone.pve_guests == NULL && clone.pve_guest_count == 0);
    assert(clone.nas_pools == NULL && clone.nas_pool_count == 0);
    assert(clone.nas_disks == NULL && clone.nas_disk_count == 0);

    app_snapshot_dispose(&clone);
    app_snapshot_dispose(&moved);
    app_snapshot_dispose(&source);
}

static void test_failed_reserve_preserves_existing_collection(void)
{
    app_snapshot_t snapshot;
    app_snapshot_init(&snapshot);
    assert(app_snapshot_reserve_pve_guests(&snapshot, 4));
    snapshot.pve_guest_count = 4;

    pve_guest_t *old_pointer = snapshot.pve_guests;
    const size_t old_capacity = snapshot.pve_guest_capacity;
    assert(!app_snapshot_reserve_pve_guests(&snapshot, SIZE_MAX));
    assert(snapshot.pve_guests == old_pointer);
    assert(snapshot.pve_guest_capacity == old_capacity);
    assert(snapshot.pve_guest_count == 4);

    app_snapshot_dispose(&snapshot);
}

static void test_heap_snapshot_destroy_accepts_null(void)
{
    app_snapshot_t *snapshot = app_snapshot_create();
    assert(snapshot != NULL);
    assert(app_snapshot_reserve_nas_disks(snapshot, 10));
    app_snapshot_destroy(snapshot);
    app_snapshot_destroy(NULL);
}

int main(void)
{
    test_large_dynamic_collections_clone_and_move();
    test_failed_reserve_preserves_existing_collection();
    test_heap_snapshot_destroy_accepts_null();
    puts("app_snapshot tests passed");
    return 0;
}
