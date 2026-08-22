# NAS Metrics And Footer Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match DSM's NAS uptime and memory semantics and keep every footer label at a stable left-aligned position while network values change.

**Architecture:** Keep SNMP collection inside `live_provider.c` and LVGL mutation inside `dashboard_ui.c`. Add small bounded helpers for uptime selection and memory calculation, then split the variable-width network label into fixed-position labels without changing the footer height or other pages.

**Tech Stack:** ESP-IDF C, custom SNMP v2c client, LVGL 8, Python `unittest` source-contract tests, CMake/Ninja.

---

## File Structure

- `components/app_model/live_provider.c`: select system uptime OID and calculate DSM-style memory utilization.
- `components/dashboard_ui/dashboard_ui.c`: create and update fixed-position NAS footer labels.
- `tests/test_monitor_migration_contract.py`: guard the SNMP OIDs, fallback order, and memory calculation.
- `tests/test_dashboard_ui_contract.py`: guard fixed left alignment and split network fields.

The shared worktree already contains related uncommitted changes, so this execution will not create intermediate Git commits or stage unrelated work.

### Task 1: Correct NAS Uptime And Memory Semantics

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/live_provider.c`

- [ ] **Step 1: Write failing provider contracts**

Add tests that require the system uptime OID before the SNMP-agent fallback and require buffer/cache-aware memory calculation:

```python
def test_nas_uptime_prefers_system_uptime_with_agent_fallback(self) -> None:
    source = PROVIDER.read_text(encoding="utf-8")
    system_oid = '"1.3.6.1.2.1.25.1.1.0"'
    agent_oid = '"1.3.6.1.2.1.1.3.0"'
    self.assertIn(system_oid, source)
    self.assertIn(agent_oid, source)
    self.assertLess(source.index(system_oid), source.index(agent_oid))
    self.assertIn("if (!snapshot->nas_uptime_valid)", source)

def test_nas_memory_excludes_reclaimable_buffers_and_cache(self) -> None:
    source = PROVIDER.read_text(encoding="utf-8")
    self.assertIn('"1.3.6.1.4.1.2021.4.14.0"', source)
    self.assertIn('"1.3.6.1.4.1.2021.4.15.0"', source)
    self.assertIn("memory_reclaimable <= memory_total - memory_available", source)
    self.assertIn("memory_used = memory_total - memory_available - memory_reclaimable", source)
    self.assertIn("memory_used = memory_total - memory_available", source)
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
python3 -m unittest \
  tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_nas_uptime_prefers_system_uptime_with_agent_fallback \
  tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_nas_memory_excludes_reclaimable_buffers_and_cache -v
```

Expected: both tests fail because `hrSystemUptime`, buffer/cache collection, and the guarded formula are absent.

- [ ] **Step 3: Implement uptime selection and memory calculation**

In `collect_nas()`, query `hrSystemUptime.0` first and query `sysUpTime.0` only when the first call fails. Convert TimeTicks to seconds after the selected query succeeds.

Read `memBuffer.0` and `memCached.0` separately. When both succeed and their sum is no larger than `total - available`, subtract them from used memory. Otherwise retain the compatible total-minus-available result. Set `nas_memory_valid` only after validating total and available.

- [ ] **Step 4: Run focused and full provider contracts**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: all monitor migration contracts pass.

### Task 2: Stabilize The Two-Row NAS Footer

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Write the failing UI contract**

Add a test that requires three separate network labels and left alignment for all footer values:

```python
def test_nas_footer_uses_fixed_left_aligned_cells(self) -> None:
    for field in ("nas_network_prefix", "nas_upload_value", "nas_download_value"):
        self.assertIn(field, self.source)
    self.assertIn('make_label(footer, "网", 8, 19, 18', self.source)
    self.assertIn('make_label(footer, "↑--", 26, 19, 66', self.source)
    self.assertIn('make_label(footer, "↓--", 94, 19, 66', self.source)
    self.assertNotIn("LV_TEXT_ALIGN_CENTER, 0);\n    lv_obj_set_style_text_align(s_ui.nas_memory_value", self.source)
    self.assertNotIn('lv_label_set_text_fmt(s_ui.nas_network_value, "网 ↑%s ↓%s"', self.source)
```

- [ ] **Step 2: Run the focused UI test and verify RED**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract.DashboardUiContractTest.test_nas_footer_uses_fixed_left_aligned_cells -v
```

Expected: fail because the footer still has one centered `nas_network_value` and centered value columns.

- [ ] **Step 3: Implement fixed footer cells**

Replace `nas_network_value` in the UI state with `nas_network_prefix`, `nas_upload_value`, and `nas_download_value`. Create them at fixed x coordinates inside the existing 160-pixel network cell and remove center alignment from every NAS footer label.

Update `update_nas()` so upload and download are formatted independently:

```c
lv_label_set_text_fmt(s_ui.nas_upload_value, "↑%s", upload);
lv_label_set_text_fmt(s_ui.nas_download_value, "↓%s", download);
```

When rates are invalid, update the two value labels to `↑--` and `↓--`; the fixed `网` label never changes.

- [ ] **Step 4: Run the focused and full UI contracts**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: all dashboard UI contracts pass.

### Task 3: Build, Flash, And Observe

**Files:**
- Verify: entire firmware tree

- [ ] **Step 1: Run all Python contracts**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: zero failures and zero errors.

- [ ] **Step 2: Build the firmware**

Run the repository's configured ESP-IDF build:

```bash
idf.py build
```

Expected: Ninja completes and produces the application binary without compiler errors.

- [ ] **Step 3: Resolve the serial device and flash**

Check the previously documented WT32-SC01 port `/dev/cu.usbserial-01E725F0` first and query chip identity before writing because multiple USB serial devices are connected. After it is confirmed as the ESP32, flash without erasing NVS:

```bash
idf.py -p /dev/cu.usbserial-01E725F0 flash
```

Expected: esptool verifies all written segments and resets the board.

- [ ] **Step 4: Monitor the boot and first NAS refresh**

Run:

```bash
idf.py -p /dev/cu.usbserial-01E725F0 monitor
```

Observe through boot and the first selected NAS refresh. Confirm there are no crash loops and the provider reports a successful NAS snapshot. Exit the monitor cleanly after collecting evidence.

- [ ] **Step 5: Record hardware-only residual validation**

Compare displayed uptime and memory percentage with DSM. Observe at least two network refreshes and confirm `网`, upload, and download positions remain stationary while values change. If physical touch/display inspection is unavailable, report that limitation explicitly rather than claiming visual confirmation.
