# NAS Footer And Disk Sorting Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved 52 px NAS footer and sortable disk list in WT32-SC01 firmware without changing the already aligned top DSM/NAS button.

**Architecture:** Add a small platform-independent disk sorting module that sorts the complete dynamic `nas_disk_t` collection in place. Keep LVGL responsibilities in `dashboard_ui.c`: header button state, paging, disabled navigation, and exact geometry.

**Tech Stack:** C11, ESP-IDF 4.4.8, LVGL 8.3.11, Python unittest native C harness

---

### Task 1: Add Tested Disk Sorting

**Files:**
- Create: `components/dashboard_ui/nas_disk_sort.h`
- Create: `components/dashboard_ui/nas_disk_sort.c`
- Create: `tests/native/nas_disk_sort_test.c`
- Create: `tests/test_nas_disk_sort_native.py`
- Modify: `components/dashboard_ui/CMakeLists.txt`

- [ ] Write a native test covering ID natural ascending/descending, case-insensitive model sorting, numeric temperature sorting, and invalid temperatures last.
- [ ] Run `python3 -m unittest tests.test_nas_disk_sort_native -v` and confirm it fails because `nas_disk_sort.c` is absent.
- [ ] Implement `nas_disk_sort(nas_disk_t *, size_t, nas_disk_sort_key_t, bool)` with deterministic ID tie-breaking.
- [ ] Add `nas_disk_sort.c` to the dashboard component sources.
- [ ] Re-run the native test and confirm it passes.

### Task 2: Wire Sorting And Stable Pager Controls Into LVGL

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `tests/test_dashboard_ui_contract.py`

- [ ] Update UI contract tests to require 52 px footer geometry, three sort headers, full-collection sorting before paging, title removal, and disabled rather than hidden first/last pager controls.
- [ ] Run `python3 -m unittest tests.test_dashboard_ui_contract -v` and confirm the new assertions fail against the 62 px footer and passive headers.
- [ ] Add sort state and a header callback that resets to page one, toggles the active direction, stops event bubbling, sorts the full collection, and refreshes rows.
- [ ] Reset sort state to disk identifier ascending and page one every time the pool list opens the disk list.
- [ ] Remove the physical-disk title, move the header buttons to the top, and increase four disk rows to 46 px.
- [ ] Place both 36 px pager controls inside a protected 44 px rail; keep them visible whenever disks exist, apply `LV_STATE_DISABLED` to unavailable directions with 30% opacity, and consume rail-gap clicks.
- [ ] Reduce the footer to y=236 and height=52. Center fixed metrics vertically and create two flex-centered 80 x 26 network rows.
- [ ] Re-run the dashboard contract tests and confirm they pass.

### Task 3: Verify, Commit, Build, And Flash

**Files:**
- Verify all changed source and tests.

- [ ] Run `python3 -m unittest discover -s tests -p 'test_*.py' -v` with zero failures.
- [ ] Run `source /Users/luckmiracle/esp/esp-idf/export.sh && idf.py build` and confirm successful size checks.
- [ ] Commit only intended tracked files on `wt32-Nas-pve-monitor`; leave `openhasp/` untouched.
- [ ] Flash with `idf.py -p /dev/cu.usbserial-01E725F0 flash` without erasing NVS.
- [ ] Monitor serial output through repeated successful NAS refreshes showing `pools=4 disks=10`, with no panic or reboot loop.
