# PVE VM Detail And Table Design

## Goal

Make the PVE page's VM navigation interactive, render VM details inside the
page, and present the VM list as a fixed-column table on the 480 x 320 display.

## Interaction

`PVE 总览` shows the node summary and VM table. Each visible `VM <id>` control
opens that guest in a reusable inline detail surface. Clicking a VM row opens
the same detail surface. `PVE 总览` returns from detail to the overview.

The detail state shows only navigation directions that exist. The first guest
has only a right control, the last guest has only a left control, and an
intermediate guest may show both. With VM 100 and VM 101 this produces the
requested right-then-left toggle. Moving to a guest outside the current top-row
window shifts that window so the selected guest remains visible.

## Overview Table

The existing `虚拟机 <running>/<total> 运行` line becomes the table header. Its
remaining columns are `IP`, `CPU`, `内存`, and `磁盘`. Each guest occupies one
fixed-height row with aligned labels. Memory and disk use `used/total` values;
missing values use `--`. Existing vertical list paging remains available.

## VM Detail

The inline detail contains guest ID and name, running state, IPv4 address, CPU
usage and core count, memory used/total, disk used/total, uptime, and Guest Agent
state. It is updated from the latest immutable snapshot without allocating a
new dialog on every click.

## Node Metrics

The node overview remains CPU, memory, storage, and load. No temperature label,
placeholder, model field, or fabricated value is added because the configured
PVE node status source does not provide CPU temperature.

## State And Error Handling

Snapshot updates clamp the selected VM and both VM offsets against the current
guest count. If the selected guest disappears, the UI returns to the overview.
Empty IP and zero capacity values render `--`. All LVGL object mutations remain
in the UI task.

## Verification

Contract tests cover inline detail objects, VM navigation state, top-row click
targets, fixed table headers and columns, and the absence of PVE temperature.
Verification runs the focused UI contract, the full Python suite, and an
ESP-IDF build. Firmware is flashed without erasing NVS, then serial output is
checked for a clean boot and UI/provider initialization.
