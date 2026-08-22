# Dynamic Monitor Data And Icon UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display every PVE guest, NAS pool, and NAS disk returned by the configured services, preserve the last complete snapshot across refresh failures, add the approved icon/footer UI, and persist a NAS/PVE default homepage selection.

**Architecture:** Replace fixed arrays and value-copy queues with heap-owned immutable snapshots carried in typed model events. Providers build complete candidates and publish only success/failure/loading/offline events; the UI owns the latest successful snapshot, paginates its dynamic collections, and presents transient centered failure feedback. Homepage selection is committed to NVS before UI state changes, while top navigation remains transient.

**Tech Stack:** ESP-IDF 4.4.8, C11, FreeRTOS queues/tasks, cJSON, ESP-TLS, SNMP/UDP, LVGL, NVS, Python `unittest`, host C compiler.

---

## File Map

- Create `components/app_model/include/app_snapshot.h`: dynamic snapshot and model-event public types plus ownership API.
- Create `components/app_model/app_snapshot.c`: host-buildable allocation, reserve, clone, move, and destruction helpers.
- Modify `components/app_model/include/app_model.h`: retain model field types and expose pointer/event provider API without business count caps.
- Modify `components/app_model/CMakeLists.txt`: compile the snapshot implementation.
- Modify `components/app_model/live_provider.c`: dynamically receive PVE JSON, collect complete SNMP subtrees, build candidates, and publish ownership-safe events.
- Modify `components/app_model/mock_provider.c`: publish heap-owned snapshots/events using the same contract.
- Modify `main/app_main.c`: consume model events and transfer/destroy snapshot ownership correctly.
- Create `components/dashboard_ui/include/dashboard_icons.h`: declarations for embedded LVGL icon images.
- Create `components/dashboard_ui/assets/dashboard_icons.c`: offline brand and monochrome hardware/status image descriptors.
- Modify `components/dashboard_ui/CMakeLists.txt`: compile embedded icon resources.
- Modify `components/dashboard_ui/dashboard_ui.c`: dynamic pagination, list switching, first-load/offline/toast states, icons, final 464x42 footer, and homepage selector.
- Modify `components/dashboard_ui/include/dashboard_ui.h`: accept model events or explicit status updates with clear ownership.
- Modify `components/network_manager/include/device_settings.h`: define `DEVICE_KEY_HOME_PAGE` and valid values.
- Modify `components/network_manager/device_settings.c`: use the existing checked NVS helpers for homepage persistence.
- Create `tests/native/app_snapshot_test.c`: executable lifecycle and large-count model tests.
- Create `tests/test_app_snapshot_native.py`: build and execute host snapshot tests.
- Modify `tests/test_dashboard_ui_contract.py`: assert the approved UI geometry, interactions, icons, toast, and dynamic paging.
- Modify `tests/test_monitor_migration_contract.py`: assert dynamic provider/event/ownership and homepage persistence contracts.

### Task 1: Dynamic Snapshot Ownership

**Files:**
- Create: `components/app_model/include/app_snapshot.h`
- Create: `components/app_model/app_snapshot.c`
- Create: `tests/native/app_snapshot_test.c`
- Create: `tests/test_app_snapshot_native.py`
- Modify: `components/app_model/include/app_model.h`
- Modify: `components/app_model/CMakeLists.txt`

- [ ] **Step 1: Write the failing host lifecycle tests**

Create a host runner that compiles `app_snapshot.c` with `cc -std=c11 -Wall -Wextra -Werror`. The C test must reserve and populate 25 guests, 12 pools, and 10 disks; clone them; move them; dispose both sides; reject `SIZE_MAX` capacity overflow; and assert a failed reserve preserves the old pointer/count/capacity.

```c
app_snapshot_t source;
app_snapshot_init(&source);
assert(app_snapshot_reserve_pve_guests(&source, 25));
assert(app_snapshot_reserve_nas_pools(&source, 12));
assert(app_snapshot_reserve_nas_disks(&source, 10));
source.pve_guest_count = 25;
source.nas_pool_count = 12;
source.nas_disk_count = 10;
app_snapshot_t clone;
app_snapshot_init(&clone);
assert(app_snapshot_clone(&clone, &source));
assert(clone.pve_guest_count == 25 && clone.pve_guests != source.pve_guests);
app_snapshot_t moved;
app_snapshot_init(&moved);
app_snapshot_move(&moved, &clone);
assert(moved.nas_pool_count == 12 && clone.nas_pools == NULL);
app_snapshot_dispose(&moved);
app_snapshot_dispose(&source);
```

- [ ] **Step 2: Run the host test and verify RED**

Run: `python3 -m unittest tests.test_app_snapshot_native -v`

Expected: FAIL because `app_snapshot.h` and `app_snapshot.c` do not exist.

- [ ] **Step 3: Implement the dynamic type and lifecycle API**

Define count and capacity as `size_t`, use pointers for all three collections, and expose these exact functions:

```c
void app_snapshot_init(app_snapshot_t *snapshot);
void app_snapshot_dispose(app_snapshot_t *snapshot);
app_snapshot_t *app_snapshot_create(void);
void app_snapshot_destroy(app_snapshot_t *snapshot);
bool app_snapshot_reserve_pve_guests(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_reserve_nas_pools(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_reserve_nas_disks(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_clone(app_snapshot_t *destination, const app_snapshot_t *source);
void app_snapshot_move(app_snapshot_t *destination, app_snapshot_t *source);
```

The reserve helper must check `capacity > SIZE_MAX / sizeof(element)`, allocate before freeing the old block, preserve state on failure, and use geometric growth without a business maximum. Add event kinds `APP_MODEL_EVENT_LOADING`, `APP_MODEL_EVENT_SNAPSHOT`, `APP_MODEL_EVENT_REFRESH_FAILED`, and `APP_MODEL_EVENT_OFFLINE`; the snapshot pointer is valid only for `APP_MODEL_EVENT_SNAPSHOT`.

- [ ] **Step 4: Run lifecycle tests and verify GREEN**

Run: `python3 -m unittest tests.test_app_snapshot_native -v`

Expected: PASS with the host test process exiting 0 and no compiler warnings.

- [ ] **Step 5: Commit the snapshot foundation**

```bash
git add components/app_model/include/app_model.h components/app_model/include/app_snapshot.h components/app_model/app_snapshot.c components/app_model/CMakeLists.txt tests/native/app_snapshot_test.c tests/test_app_snapshot_native.py
git commit -m "refactor: add dynamic monitor snapshots"
```

### Task 2: Event Queue And Complete-Candidate Publishing

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/live_provider.c`
- Modify: `components/app_model/mock_provider.c`
- Modify: `main/app_main.c`
- Modify: `components/dashboard_ui/include/dashboard_ui.h`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Rewrite the queue contract tests**

Assert `xQueueCreate(1, sizeof(app_model_event_t))`, pointer-only snapshot transfer, explicit destruction of replaced queued snapshots, UI replacement of its previous owned snapshot, and absence of fixed snapshot value copies. Assert initial loading, later failure, and no-cache offline are distinct events.

```python
self.assertIn("sizeof(app_model_event_t)", provider)
self.assertIn("app_snapshot_destroy(dropped.snapshot)", provider)
self.assertIn("APP_MODEL_EVENT_LOADING", provider)
self.assertIn("APP_MODEL_EVENT_REFRESH_FAILED", provider)
self.assertIn("APP_MODEL_EVENT_OFFLINE", provider)
self.assertNotIn("candidate = snapshot", provider)
```

- [ ] **Step 2: Run the event tests and verify RED**

Run: `python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest -v`

Expected: FAIL on fixed value queues and stale-copy behavior.

- [ ] **Step 3: Implement ownership-safe event publishing and consumption**

Add `publish_event(app_model_event_t event)` that first tries `xQueueSend`; when full it receives one old event, destroys `old.snapshot` when present, then sends the new event. The live provider must publish loading before a first request, publish a candidate only after full success, publish offline only when no successful snapshot exists and the request timeout has elapsed, and otherwise publish refresh-failed without embedding a partial snapshot. `ui_task` transfers a snapshot event pointer into `dashboard_ui_update()` and destroys any unconsumed pointer on shutdown/error paths.

- [ ] **Step 4: Run the event tests and verify GREEN**

Run: `python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest -v`

Expected: queue/event ownership assertions PASS.

- [ ] **Step 5: Commit event migration**

```bash
git add components/app_model/live_provider.c components/app_model/mock_provider.c main/app_main.c components/dashboard_ui/include/dashboard_ui.h components/dashboard_ui/dashboard_ui.c tests/test_monitor_migration_contract.py
git commit -m "refactor: publish owned monitor events"
```

### Task 3: Unlimited PVE Response And Guest Parsing

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/live_provider.c`

- [ ] **Step 1: Add failing PVE dynamic-response tests**

Assert `HTTP_RESPONSE_MAX` is absent; reception uses a checked geometric `realloc`; EOF is required before parsing; guest storage reserves the actual `cJSON_GetArraySize(data)`; and any allocation/element failure returns false without publishing.

```python
self.assertNotIn("HTTP_RESPONSE_MAX", source)
self.assertIn("grow_http_buffer", source)
self.assertIn("cJSON_GetArraySize(data)", source)
self.assertIn("app_snapshot_reserve_pve_guests", source)
self.assertNotIn("APP_MAX_PVE_GUESTS", source)
```

- [ ] **Step 2: Run the PVE tests and verify RED**

Run: `python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_pve_response_and_guest_collection_are_dynamic -v`

Expected: FAIL because the provider still allocates a fixed 24 KiB body and caps guest parsing.

- [ ] **Step 3: Implement checked geometric HTTP growth and exact guest allocation**

Start the body at 4096 bytes, double after checking `capacity > SIZE_MAX / 2`, and retain one byte for NUL. Treat negative TLS reads as errors, accept only a clean connection close, parse the full payload, reserve exactly the JSON array length, then increment count only after each element is completely populated.

- [ ] **Step 4: Run the PVE tests and full model tests**

Run: `python3 -m unittest tests.test_monitor_migration_contract tests.test_app_snapshot_native -v`

Expected: PASS.

- [ ] **Step 5: Commit dynamic PVE collection**

```bash
git add components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "fix: collect every PVE guest"
```

### Task 4: Unlimited Complete SNMP Walks

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/live_provider.c`

- [ ] **Step 1: Add failing SNMP completion tests**

Assert fixed pool/disk caps and the total collection budget are absent; each GETNEXT keeps a previous OID, requires strict lexical OID progress, ends only outside its subtree, dynamically reserves by returned count, and returns failure on timeout/protocol/allocation errors.

```python
self.assertNotIn("SNMP_COLLECT_BUDGET_MS", source)
self.assertNotIn("SNMP_MAX_POOLS", source)
self.assertNotIn("APP_MAX_NAS_DISKS", source)
self.assertIn("snmp_oid_compare", source)
self.assertIn("app_snapshot_reserve_nas_pools", source)
self.assertIn("app_snapshot_reserve_nas_disks", source)
```

- [ ] **Step 2: Run the SNMP test and verify RED**

Run: `python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_snmp_walks_complete_dynamic_subtrees -v`

Expected: FAIL on fixed walk loops and aggregate deadline.

- [ ] **Step 3: Implement complete subtree walks**

Keep `SNMP_TIMEOUT_MS` per UDP exchange. For each response, parse the full OID, require `current > previous`, return success when `current` leaves the requested prefix, merge indexed columns into dynamically grown pool/disk entries, and return false on any incomplete column walk. Remove all count-based and wall-budget stop conditions.

- [ ] **Step 4: Run SNMP and full contract tests**

Run: `python3 -m unittest tests.test_monitor_migration_contract -v`

Expected: PASS with dynamic walk, strict progress, and existing SNMP validation coverage intact.

- [ ] **Step 5: Commit dynamic NAS collection**

```bash
git add components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "fix: collect complete NAS inventories"
```

### Task 5: Dynamic Pagination, Loading, Offline, And Toast UI

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `components/dashboard_ui/include/dashboard_ui.h`

- [ ] **Step 1: Add failing UI state and pagination contracts**

Assert page counts derive only from `size_t` collection counts; PVE, pools, and disks clamp offsets after smaller snapshots; NAS pages remain four rows; list-body click toggles pool/disk mode; pager button callbacks stop bubbling; disk capacity is absent; loading text is `数据加载中`; offline appears only on an offline event without a cached snapshot; refresh failure keeps rows and starts a centered non-clickable 2500 ms fade toast.

- [ ] **Step 2: Run dashboard contracts and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: FAIL on fixed caps, stale/offline banners, and missing toast lifecycle.

- [ ] **Step 3: Implement dynamic view offsets and transient feedback**

Use a helper equivalent to:

```c
static size_t clamp_page_offset(size_t offset, size_t count, size_t page_size)
{
    if (count == 0) return 0;
    const size_t last = ((count - 1) / page_size) * page_size;
    return offset > last ? last : offset;
}
```

Create a toast layer centered in the active content area, clear `LV_OBJ_FLAG_CLICKABLE`, animate opacity in/out, and delete/hide after 2500 ms. Preserve the existing snapshot on refresh-failed events.

- [ ] **Step 4: Run dashboard and model contracts**

Run: `python3 -m unittest tests.test_dashboard_ui_contract tests.test_monitor_migration_contract -v`

Expected: PASS.

- [ ] **Step 5: Commit dynamic UI behavior**

```bash
git add components/dashboard_ui/dashboard_ui.c components/dashboard_ui/include/dashboard_ui.h tests/test_dashboard_ui_contract.py
git commit -m "feat: paginate complete monitor data"
```

### Task 6: Offline Icons And Final NAS Footer

**Files:**
- Create: `components/dashboard_ui/include/dashboard_icons.h`
- Create: `components/dashboard_ui/assets/dashboard_icons.c`
- Modify: `components/dashboard_ui/CMakeLists.txt`
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `tests/test_dashboard_ui_contract.py`

- [ ] **Step 1: Add failing icon and footer geometry contracts**

Assert official-source DSM and Proxmox brand assets are embedded; processor, DIMM, temperature, HDD, pool, IP, uptime, upload, and download descriptors exist; the footer is exactly 464x42; the right side has four 68 px cells; the network value uses two lines; values use fixed-width alignment; and rate formatting supports `B/K/M/G/T` with at most one decimal.

- [ ] **Step 2: Run footer contracts and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: FAIL on text-only controls and the old 36 px footer.

- [ ] **Step 3: Add embedded assets and implement the final layout**

Use offline LVGL image descriptors. Preserve official brand colors for DSM/Proxmox; mark monochrome images recolorable. Place full IPv4 and compact uptime on the left. Place CPU, memory, temperature, and network in equal 68 px cells on the right. Format network rates as `999B`, `1.0K`, `999.9G`, or `1.0T` without clipping and use `--` for unavailable values.

- [ ] **Step 4: Run UI tests and build-check the resources**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: PASS including long-value stress contracts.

- [ ] **Step 5: Commit icon/footer UI**

```bash
git add components/dashboard_ui/include/dashboard_icons.h components/dashboard_ui/assets/dashboard_icons.c components/dashboard_ui/CMakeLists.txt components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git commit -m "feat: add monitor icons and balanced NAS footer"
```

### Task 7: Persisted Homepage Selector

**Files:**
- Modify: `components/network_manager/include/device_settings.h`
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Add failing persistence and settings-layout tests**

Assert `DEVICE_KEY_HOME_PAGE` is `home_page`; missing/invalid reads map to NAS; settings has three rows with homepage NAS/PVE segmented buttons first; selection calls checked NVS write before changing active page; write failure returns without UI change; startup selects saved homepage; top `page_event()` does not write the key.

- [ ] **Step 2: Run homepage tests and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract tests.test_monitor_migration_contract -v`

Expected: FAIL because startup is hardcoded and no homepage key/buttons exist.

- [ ] **Step 3: Implement commit-before-navigate homepage behavior**

Use values `0` for NAS and `1` for PVE. Read and validate once during `dashboard_ui_create()`. In the settings callback, call `device_settings_set_u8(DEVICE_KEY_HOME_PAGE, requested)` first; only on `ESP_OK` update button states, `active_page`, active monitor scheduling, and page visibility. Do not add persistence to top navigation callbacks.

- [ ] **Step 4: Run homepage and full Python tests**

Run: `python3 -m unittest discover -s tests -p 'test_*.py' -v`

Expected: all tests PASS.

- [ ] **Step 5: Commit homepage selection**

```bash
git add components/network_manager/include/device_settings.h components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py tests/test_monitor_migration_contract.py
git commit -m "feat: persist the selected homepage"
```

### Task 8: Browser QA And Firmware Delivery

**Files:**
- Modify only if verification finds a defect in the files above.

- [ ] **Step 1: Start the local visual preview and inspect 480x320**

Open the current implementation preview in the in-app browser without asking. Verify settings at 480x320, normal NAS values, pools with 12 entries, disks with 10 entries, and the footer with longest IPv4, `100%`, three-digit temperature, and `999.9G` network values. Capture screenshots and verify no overlap, clipping, blank assets, or horizontal pager controls.

- [ ] **Step 2: Run the complete automated suite**

Run: `python3 -m unittest discover -s tests -p 'test_*.py' -v`

Expected: all tests PASS.

- [ ] **Step 3: Build the ESP-IDF firmware**

Run: `source /Users/luckmiracle/esp/esp-idf/export.sh && idf.py build`

Expected: `Project build complete` with no compiler or linker errors.

- [ ] **Step 4: Review and commit any verification fixes**

Run: `git diff --check && git status --short`

Expected: no whitespace errors; only intended source/test files plus the untouched untracked `openhasp/`. Commit any fixes with a focused message, then confirm `git log -1 --oneline` describes the completed implementation.

- [ ] **Step 5: Flash without erasing NVS**

Run: `source /Users/luckmiracle/esp/esp-idf/export.sh && idf.py -p /dev/cu.usbserial-01E725F0 flash`

Expected: all application images write and verify successfully; do not run `erase-flash`.

- [ ] **Step 6: Verify boot over serial**

Run: `source /Users/luckmiracle/esp/esp-idf/export.sh && idf.py -p /dev/cu.usbserial-01E725F0 monitor`

Expected: device boots without a panic/reboot loop, initializes the configured homepage, and logs successful NAS/PVE refresh counts from the actual interfaces. Exit monitor after observing a complete refresh.

## Self-Review Record

- Spec coverage: dynamic collections, full PVE body, complete SNMP walks, snapshot ownership, cached refresh behavior, first-load timeout, centered toast, all pagination/list interactions, official/offline icons, final footer, homepage persistence, browser QA, build, flash, and serial verification each map to a task.
- Placeholder scan: no `TBD`, deferred implementation, or unspecified error-handling steps remain.
- Type consistency: all dynamic counts use `size_t`; all snapshot transfers use `app_snapshot_t *` inside `app_model_event_t`; reserve/clone/move/dispose names are consistent across model, provider, UI, and tests.
