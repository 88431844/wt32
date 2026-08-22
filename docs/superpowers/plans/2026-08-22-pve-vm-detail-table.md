# PVE VM Detail And Table Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add inline PVE guest detail navigation and a fixed-column VM table, then build and flash the firmware without erasing settings.

**Architecture:** Keep the existing snapshot and provider interfaces unchanged. Add one reusable overview container and one reusable detail surface inside `dashboard_ui.c`; VM labels, rows, and direction controls select an index and redraw from the current snapshot.

**Tech Stack:** ESP-IDF 4.4.8, LVGL 8.3.11, C, Python `unittest`, `idf.py`, `esptool.py`.

---

## File Structure

- `tests/test_dashboard_ui_contract.py`: source contracts for inline detail navigation and table columns.
- `components/dashboard_ui/dashboard_ui.c`: PVE overview/detail state, click targets, arrow visibility, and table geometry.
- `docs/superpowers/specs/2026-08-22-pve-vm-detail-table-design.md`: approved behavior and scope.

The repository already has overlapping uncommitted monitor work. Do not create
code commits that mix those existing changes with this increment.

### Task 1: Lock The UI Contract

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`

- [x] **Step 1: Add failing inline-detail contracts**

Assert that the UI stores overview/detail surfaces, selected VM state, clickable
VM navigation buttons, and previous/next detail controls:

```python
self.assertIn("pve_overview", self.source)
self.assertIn("vm_detail", self.source)
self.assertIn("selected_vm_index", self.source)
self.assertIn("vm_nav_buttons", self.source)
self.assertIn("show_vm_detail", self.source)
self.assertIn("show_pve_overview", self.source)
self.assertIn("vm_detail_previous", self.source)
self.assertIn("vm_detail_next", self.source)
```

- [x] **Step 2: Add failing table contracts**

```python
for field in ("vm_header_ip", "vm_header_cpu", "vm_header_memory", "vm_header_disk"):
    self.assertIn(field, self.source)
for field in ("vm_row_cpu", "vm_row_memory", "vm_row_disk"):
    self.assertIn(field, self.source)
self.assertNotIn("vm_row_metrics", self.source)
self.assertNotIn("pve_cpu_temperature", self.source)
```

- [x] **Step 3: Run the focused contract and verify RED**

Run `python3 -m unittest tests.test_dashboard_ui_contract -v`.
Expected: the new inline-detail and table-column assertions fail because the
corresponding LVGL objects do not exist yet.

### Task 2: Implement Inline Detail And Table Columns

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Test: `tests/test_dashboard_ui_contract.py`

- [x] **Step 1: Add reusable UI state**

Add overview/detail objects, top VM buttons, selected index, detail labels,
direction controls, table header labels, and separate row labels for CPU,
memory, and disk. Use `-1` as the no-selected-guest value.

- [x] **Step 2: Replace modal allocation with state switching**

`show_vm_detail(index)` clamps the index, keeps it in the top navigation window,
hides the overview, shows the detail surface, and refreshes all detail labels.
`show_pve_overview()` performs the inverse. The first guest hides previous, the
last guest hides next, and an intermediate guest exposes both available paths.

- [x] **Step 3: Make VM navigation and rows select the real guest index**

Top controls use their snapshot index as event data. VM table rows continue to
replace their event callback after paging, so the selected guest always matches
the displayed row. Arrow events decrement or increment the selected index.

- [x] **Step 4: Split the list into fixed columns**

Create header labels `IP`, `CPU`, `内存`, and `磁盘` beside the dynamic first
header. Replace the combined metrics label with individual aligned labels and
reuse `format_percent()` and `format_bytes()` for each value.

- [x] **Step 5: Run the focused contract and verify GREEN**

Run `python3 -m unittest tests.test_dashboard_ui_contract -v`.
Expected: all dashboard UI contracts pass.

### Task 3: Verify, Build, Flash, And Monitor

**Files:**
- Use generated `build/` artifacts only.

- [x] **Step 1: Run the complete Python suite**

Run `python3 -m unittest discover -s tests -v`.
Expected: every contract and provider test passes.

- [x] **Step 2: Build and check size with the installed ESP-IDF environment**

Run `idf.py build` and `idf.py size` through the repository's documented ESP-IDF
4.4.8 environment. Expected: exit status 0 and the application fits the
configured OTA slot.

- [x] **Step 3: Resolve the exact WT32 serial device**

Check `/dev/cu.usbserial-01E725F0` first, then query chip identity if enumeration
has changed. Do not select a port by list order.

- [x] **Step 4: Flash without erasing NVS**

Run `idf.py -p /dev/cu.usbserial-01E725F0 flash`. Do not run `erase_flash`.
Expected: all written segments verify and existing Wi-Fi/PVE/NAS settings remain.

- [x] **Step 5: Check boot output**

Monitor long enough to confirm display, touch, LVGL page creation, Wi-Fi, active
PVE monitor selection, and either a successful PVE refresh or a specific
configuration/network error. Stop the monitor cleanly after collecting evidence.
