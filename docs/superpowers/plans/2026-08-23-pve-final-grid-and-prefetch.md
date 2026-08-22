# PVE Final Grid And Startup Prefetch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match the approved 480 x 320 PVE preview in firmware, preload both monitor caches at startup, and flash the verified build to the connected WT32-SC01 without erasing settings.

**Architecture:** Keep the existing PVE snapshot and three-view navigation state. Refine only the LVGL object geometry and labels in `dashboard_ui.c`, then route the provider's first two connected cycles through the saved homepage and the opposite monitor before returning to active-page polling.

**Tech Stack:** ESP-IDF 4.4.8, C11, LVGL 8.3.11, FreeRTOS, Python `unittest`, `idf.py`, `esptool.py`.

---

## File Map

- `tests/test_dashboard_ui_contract.py`: contracts for the final node geometry, button hit target, list grid, detail grid, and labeled load values.
- `components/dashboard_ui/dashboard_ui.c`: final PVE node, VM list, and VM detail LVGL object tree.
- `tests/test_monitor_migration_contract.py`: contract for saved-homepage-first startup and one-shot opposite-monitor prefetch.
- `components/app_model/live_provider.c`: startup target selection and transition back to active-page-only polling.

### Task 1: Lock The Final PVE UI Contract

- [ ] Update `test_pve_node_view_has_approved_geometry_and_fields` to require `PVE_METRIC_ROW_HEIGHT 64`, `PVE_PROCESSOR_ROW_HEIGHT 42`, `PVE_VM_BUTTON_Y 224`, a 56px hit target, a 40px visible button, the `1分`/`5分`/`15分` labels, and storage-before-memory geometry.
- [ ] Update the list contract to require four fixed columns plus nine theme-aware grid rules while preserving four rows and the right pager.
- [ ] Add a detail contract requiring a 54px title band, a two-column by three-row grid, four grid rules, and separate value labels.
- [ ] Run `python3 -m unittest tests.test_dashboard_ui_contract -v` and confirm the new assertions fail against the current firmware source.

### Task 2: Render The Approved PVE Views

- [ ] Replace the single load string with three aligned load-value labels under static `1分`, `5分`, and `15分` labels.
- [ ] Put storage in the narrow lower-left cell and memory in the wide lower-right cell; expand metric rows to 64px and keep the processor in the final 42px row.
- [ ] Build the VM-list entry as a transparent 56px clickable parent with a centered 40px visible child and a directly owned centered label.
- [ ] Replace the VM list's rounded row cards with a fixed-column grid using boundaries at x=166, 280, 328, and 420 and row boundaries at y=30, 88, 146, 204, and 262.
- [ ] Replace combined VM detail strings with six label/value cells below a compact title/status band; preserve click-to-list behavior.
- [ ] Run `python3 -m unittest tests.test_dashboard_ui_contract -v` and confirm all UI contracts pass.

### Task 3: Prefetch The Inactive Monitor Once At Startup

- [ ] Add a provider contract requiring `DEVICE_KEY_HOME_PAGE`, saved-homepage monitor selection, a one-shot opposite monitor target, and active-page polling after the startup sequence.
- [ ] Run the focused provider test and confirm it fails because the live provider currently defaults to NAS and polls only the active monitor.
- [ ] Initialize the provider control queue from the persisted homepage; in the task, collect that monitor first, collect the opposite monitor immediately after Wi-Fi is available, then use `control.active_monitor` for all later cycles.
- [ ] Publish both successful startup snapshots through the existing immutable snapshot path and retain the existing refresh interval and page-switch wake-up behavior.
- [ ] Run `python3 -m unittest tests.test_monitor_migration_contract -v` and confirm the provider contract passes.

### Task 4: Verify, Build, Flash, And Monitor

- [ ] Run `python3 -m unittest discover -s tests -v` and confirm zero failures.
- [ ] Activate ESP-IDF v4.4.8, run `idf.py build` and `idf.py size`, and confirm the application fits the configured OTA slot.
- [ ] Resolve the connected ESP32 port by chip query; use the verified port rather than list order.
- [ ] Run `idf.py -p <verified-port> flash` without `erase_flash` so NVS settings remain intact.
- [ ] Monitor at 115200 long enough to confirm boot, UI creation, Wi-Fi state, startup primary refresh, opposite-monitor prefetch, and no crash or restart loop.
- [ ] Review `git diff`, exclude `openhasp/` and generated build outputs, then commit the completed source, tests, and plan to `wt32-Nas-pve-monitor`.
