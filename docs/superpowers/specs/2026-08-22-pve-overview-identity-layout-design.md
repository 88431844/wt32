# PVE Overview Identity Layout Design

## Goal

Replace the crowded PVE overview identity sentence with three horizontally
aligned fields for node name, version, and IP address. Remove the redundant
online text because the existing green status dot already communicates that
state.

## Layout

The first row of the existing 464-pixel-wide PVE summary surface keeps the
status dot at the left. The remaining width is divided into three fixed fields
with two thin vertical separators:

```text
o  name p330  |  version 8.4.1  |  IP 192.168.31.34
```

Each field is a separate LVGL label with a stable width. Text stays on one line
and uses the normal or muted text role; online state color is applied only to
the status dot. The row must fit the 480 x 320 display without wrapping or
overlapping the CPU, memory, storage, and load labels below it.

## States

- Online: show the current PVE node name, PVE version, and configured PVE host.
- Missing name or version: use the existing `p330` and `--` fallbacks.
- Loading, offline, refresh failure without cached data, or unconfigured: show
  the status message in the first field and `--` in the version and IP fields.
- The status dot remains green online and gray otherwise.
- The identity row never renders the word `在线`.

## Scope

This change is limited to the PVE overview identity row. It does not change the
PVE provider, node metrics, VM table, VM detail view, navigation, themes, or
refresh behavior.

## Verification

Add a Dashboard UI contract test covering the three labels, two separators,
fixed horizontal geometry, online field formatting, offline clearing, and the
absence of the old combined online sentence. Run the focused contract test,
the full Python test suite, and an ESP-IDF 4.4.8 firmware build with size check.
