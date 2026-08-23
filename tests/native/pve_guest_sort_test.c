#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pve_guest_sort.h"

static pve_guest_t guest(uint32_t vmid, bool running)
{
    pve_guest_t value = {0};
    value.vmid = vmid;
    value.running = running;
    return value;
}

static void test_running_guests_precede_stopped_guests(void)
{
    pve_guest_t guests[] = {
        guest(102, false), guest(105, true), guest(100, true),
        guest(103, false), guest(101, true),
    };
    pve_guest_sort(guests, 5);
    const uint32_t expected[] = {100, 101, 105, 102, 103};
    for (size_t i = 0; i < 5; ++i) assert(guests[i].vmid == expected[i]);
    for (size_t i = 0; i < 3; ++i) assert(guests[i].running);
    for (size_t i = 3; i < 5; ++i) assert(!guests[i].running);
}

static void test_same_status_guests_use_ascending_vmid(void)
{
    pve_guest_t guests[] = {
        guest(300, false), guest(100, false), guest(200, false),
    };
    pve_guest_sort(guests, 3);
    assert(guests[0].vmid == 100);
    assert(guests[1].vmid == 200);
    assert(guests[2].vmid == 300);
}

static void test_small_collections_are_accepted(void)
{
    pve_guest_t one[] = {guest(100, true)};
    pve_guest_sort(NULL, 0);
    pve_guest_sort(one, 1);
    assert(one[0].vmid == 100 && one[0].running);
}

int main(void)
{
    test_running_guests_precede_stopped_guests();
    test_same_status_guests_use_ascending_vmid();
    test_small_collections_are_accepted();
    puts("PVE guest sort tests passed");
    return 0;
}
