# PVE Running-First Guest Sort Design

## Goal

Make the PVE virtual machine list show running guests before stopped guests by
default while preserving ascending VMID order inside each status group.

## Ordering Contract

The complete guest collection uses these keys in order:

1. Running state, with `running == true` first.
2. VMID in ascending numeric order.

For example, an input containing stopped VM 102, running VM 105, running VM
100, stopped VM 103, and running VM 101 becomes 100, 101, 105, 102, 103.

The ordering is deterministic and does not depend on the order returned by the
PVE API.

## Architecture

Add a small `pve_guest_sort` module to the app model component. It accepts the
existing `pve_guest_t` array and count, sorts the array in place, and returns
without action for null arrays or collections smaller than two entries.

Call the sorter once after `parse_pve_guests()` has populated the complete
collection. The existing per-guest network lookup then follows the sorted
array, so list pagination, guest detail selection, and refreshed snapshots all
share the same order.

The snapshot model, PVE request endpoints, dashboard layout, page size, and
manual UI controls remain unchanged.

## Testing

Add a native C test that passes deliberately unordered running and stopped
guests to the real sorting module and asserts the exact running-first, VMID-
ascending result. It also verifies empty, single-entry, and all-same-status
collections.

Add a provider contract assertion that the parsed guest collection is sorted
before the per-guest network lookup loop begins. Run the focused native test,
the full Python test suite, and an ESP-IDF firmware build before committing and
flashing the device.

