# NAS List Toggle Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the NAS page's pool sub-navigation with direct pool-list and physical-disk-list toggling, vertical disk pagination, and a disk table without capacity.

**Architecture:** Keep the existing bounded NAS snapshot and footer unchanged. Replace the selected-pool detail state with one boolean list-view state and two sibling LVGL containers; both the containers and their rows receive explicit click callbacks, while the vertical paging buttons keep their own non-bubbling callbacks.

**Tech Stack:** C, ESP-IDF 4.4.8, LVGL 8.3.11, Python `unittest` source-contract tests, `idf.py`, `esptool.py`.

---

## File Map

- Modify `tests/test_dashboard_ui_contract.py`: define the new list-toggle, click-target, vertical-pager, and no-capacity UI contracts.
- Modify `components/dashboard_ui/dashboard_ui.c`: remove NAS pool sub-navigation, replace selected-pool detail state, build the two full-width list states, and render disk rows without capacity.
- Create no runtime or data-model files; `components/app_model` and the NAS SNMP collector remain unchanged.

### Task 1: Define The NAS List-Toggle Contract

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py:103-199`
- Test: `tests/test_dashboard_ui_contract.py`

- [ ] **Step 1: Replace the selected-pool navigation contract with the two-list contract**

Replace `test_nas_layout_has_pool_navigation_overview_and_detail` with:

```python
def test_nas_layout_toggles_between_pool_and_disk_lists(self) -> None:
    for field in (
        "nas_overview", "nas_detail", "nas_disks_visible",
        "show_nas_pools", "show_nas_disks", "nas_disk_rows",
        "nas_pool_list_event", "nas_disk_list_event",
    ):
        self.assertIn(field, self.source)
    for removed in (
        "nas_overview_button", "nas_pool_buttons", "nas_pool_button_labels",
        "nas_pool_button_dots", "selected_nas_pool_index",
        "show_nas_overview", "show_nas_detail",
    ):
        self.assertNotIn(removed, self.source)
    self.assertNotIn('make_button(page, "NAS 总览"', self.source)
    self.assertIn('"NAS 物理盘（未按池映射）"', self.source)
```

- [ ] **Step 2: Add click-target and disk-column contracts**

Add:

```python
def test_nas_list_surfaces_and_rows_toggle_views(self) -> None:
    self.assertIn(
        "lv_obj_add_event_cb(s_ui.nas_overview, nas_pool_list_event,",
        self.source,
    )
    self.assertIn(
        "lv_obj_add_event_cb(s_ui.nas_rows[i], nas_pool_list_event,",
        self.source,
    )
    self.assertIn(
        "lv_obj_add_event_cb(s_ui.nas_detail, nas_disk_list_event,",
        self.source,
    )
    self.assertIn(
        "lv_obj_add_event_cb(row, nas_disk_list_event,",
        self.source,
    )

def test_nas_disk_table_omits_capacity(self) -> None:
    self.assertNotIn("nas_disk_capacities", self.source)
    self.assertNotIn("disk->capacity_valid", self.source)
    self.assertNotIn(
        'make_label(s_ui.nas_detail, "容量"',
        self.source,
    )
```

- [ ] **Step 3: Tighten the disk pagination contract for vertical buttons**

Keep the existing bounded-offset assertions, then add:

```python
self.assertIn(
    'make_button(s_ui.nas_detail, "^", 428, 0, 28, 98',
    self.source,
)
self.assertIn(
    'make_button(s_ui.nas_detail, "v", 428, 130, 28, 106',
    self.source,
)
self.assertNotIn(
    'make_button(s_ui.nas_detail, "<"',
    self.source,
)
self.assertNotIn(
    'make_button(s_ui.nas_detail, ">"',
    self.source,
)
```

- [ ] **Step 4: Run focused tests and verify they fail for the intended old UI**

Run:

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_nas_layout_toggles_between_pool_and_disk_lists \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_nas_list_surfaces_and_rows_toggle_views \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_nas_disk_table_omits_capacity \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_nas_disk_table_pages_across_the_snapshot_capacity -v
```

Expected: FAIL because the source still contains selected-pool navigation, horizontal paging buttons, and disk-capacity widgets.

### Task 2: Implement Direct Pool/Disk List Switching

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c:140-177`
- Modify: `components/dashboard_ui/dashboard_ui.c:680-935`
- Modify: `components/dashboard_ui/dashboard_ui.c:1280-1295`
- Modify: `components/dashboard_ui/dashboard_ui.c:1398-1490`
- Test: `tests/test_dashboard_ui_contract.py`

- [ ] **Step 1: Replace selected-pool widgets and state with list-view state**

Remove the NAS overview button, pool button arrays, detail pool summary widgets, disk capacity widgets, and `selected_nas_pool_index`. Retain the pool rows, disk rows, pager, footer, and disk offset, and add:

```c
bool nas_disks_visible;
```

Rename the forward declaration from `update_nas_detail` to:

```c
static void update_nas_disks(void);
```

- [ ] **Step 2: Replace selected-pool navigation callbacks with two view transitions**

Implement:

```c
static void show_nas_pools(void)
{
    s_ui.nas_disks_visible = false;
    set_hidden(s_ui.nas_overview, false);
    set_hidden(s_ui.nas_detail, true);
}

static void show_nas_disks(void)
{
    s_ui.nas_disks_visible = true;
    set_hidden(s_ui.nas_overview, true);
    set_hidden(s_ui.nas_detail, false);
    update_nas_disks();
}

static void nas_pool_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_nas_disks();
}

static void nas_disk_list_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_nas_pools();
}
```

Keep `nas_disk_page_event` responsible only for changing `nas_disk_offset` and refreshing `update_nas_disks()`.

- [ ] **Step 3: Render the global physical-disk list without pool or capacity fields**

Rename `update_nas_detail` to `update_nas_disks` and remove the selected-pool validation, pool title/status, pool-capacity formatting, and capacity cell updates. Start directly from the NAS live state:

```c
static void update_nas_disks(void)
{
    const app_snapshot_t *snapshot = &s_ui.snapshot;
    const bool nas_live = snapshot->nas_online && !snapshot->nas_stale;
    char value[32];
```

Preserve offline/empty handling, offset clamping, page count, vertical button visibility, disk status, ID, model, and temperature rendering. Each visible row reads `snapshot->nas_disks[s_ui.nas_disk_offset + i]`; no `capacity_valid` branch remains.

- [ ] **Step 4: Rebuild the NAS pool surface as the default full content area**

Delete creation of `NAS 总览` and pool buttons. Create the overview at `(0, 0)` with size `480 x 244`, make it explicitly clickable, and bind the container callback:

```c
s_ui.nas_overview = lv_obj_create(page);
lv_obj_remove_style_all(s_ui.nas_overview);
lv_obj_set_pos(s_ui.nas_overview, 0, 0);
lv_obj_set_size(s_ui.nas_overview, WT32_LCD_WIDTH, 244);
lv_obj_clear_flag(s_ui.nas_overview, LV_OBJ_FLAG_SCROLLABLE);
lv_obj_add_flag(s_ui.nas_overview, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_event_cb(s_ui.nas_overview, nas_pool_list_event,
                    LV_EVENT_CLICKED, NULL);
```

Use four 56-pixel pool rows at `x=8`, `y=4 + i * 59`, width `464`. Bind every row to `nas_pool_list_event`; keep the existing status, labels, capacity value, and bar, with the bar widened only within the existing row width.

- [ ] **Step 5: Rebuild the physical-disk surface and vertical pager**

Create the disk surface at `(8, 4)` with size `464 x 236`, make it clickable, bind `nas_disk_list_event`, and add this header and pager geometry:

```c
make_label(s_ui.nas_detail, "NAS 物理盘（未按池映射）", 12, 9, 400,
           &app_font_14, COLOR_MUTED);
s_ui.nas_disk_previous = make_button(s_ui.nas_detail, "^", 428, 0, 28, 98,
                                     nas_disk_page_event, (void *)(intptr_t)-1);
s_ui.nas_disk_page = make_label(s_ui.nas_detail, "1/1", 428, 106, 28,
                                &app_font_14, COLOR_MUTED);
s_ui.nas_disk_next = make_button(s_ui.nas_detail, "v", 428, 130, 28, 106,
                                 nas_disk_page_event, (void *)(intptr_t)1);
```

Use the remaining width for `硬盘`, `型号`, and `温度`. Create four clickable rows at `y=64 + i * 40`, width `412`, bind each row to `nas_disk_list_event`, and give the model column the width released by removing capacity.

- [ ] **Step 6: Update NAS refresh and initialization**

Remove `selected_nas_pool_index` initialization. End `create_nas_page` with `show_nas_pools()`. In `update_nas`, stop updating hidden pool buttons and finish with:

```c
if (s_ui.nas_disks_visible) update_nas_disks();
```

The pool rows, footer values, offline placeholders, and positive-green remaining bars keep their current update behavior.

- [ ] **Step 7: Run the focused tests and make them pass**

Run the four focused tests from Task 1.

Expected: PASS.

- [ ] **Step 8: Run the entire dashboard UI contract suite**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: all dashboard UI contracts pass. Update only obsolete NAS assertions; do not relax unrelated PVE, theme, footer, or font contracts.

- [ ] **Step 9: Commit the tested implementation**

```bash
git add components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git commit -m "feat(ui): toggle NAS pool and disk lists"
```

### Task 3: Verify, Build, Flash, And Monitor

**Files:**
- Verify: `components/dashboard_ui/dashboard_ui.c`
- Verify: `tests/test_dashboard_ui_contract.py`
- Verify: `build/wt32_dashboard.bin`

- [ ] **Step 1: Run all repository Python contract tests**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all tests pass with no failures or errors.

- [ ] **Step 2: Activate ESP-IDF 4.4.8 and build the firmware**

Run in one activated shell:

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py --version
idf.py build
idf.py size
```

Expected: the version is ESP-IDF v4.4.8, build succeeds, and the application remains below the `0x1d0000` OTA slot limit.

- [ ] **Step 3: Confirm the documented WT32 serial device before writing**

Check `/dev/cu.usbserial-01E725F0`, then query it without modifying flash:

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
esptool.py --chip esp32 --port /dev/cu.usbserial-01E725F0 chip_id
```

Expected: an ESP32 chip identity is returned. Do not select either of the other enumerated USB serial ports by guesswork.

- [ ] **Step 4: Flash without erasing settings**

Run:

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbserial-01E725F0 flash
```

Expected: bootloader, partition table, OTA metadata, and application writes report verified hashes. Do not run `erase_flash`; the existing NVS settings and credentials must remain intact.

- [ ] **Step 5: Capture clean boot evidence**

Run:

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbserial-01E725F0 monitor
```

Observe through UI creation and the first provider refresh. Expected: ST7796 and FT5x06 initialize, `Created NAS/PVE/Settings LVGL pages` appears, there is no reset/crash loop, and the active monitor produces either a successful refresh or a specific configuration/network error. Exit the monitor with `Ctrl-]` after collecting evidence.

- [ ] **Step 6: Review the final diff and repository state**

Run:

```bash
git diff HEAD~1 -- components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git status --short --branch
```

Expected: only the planned implementation commit and the pre-existing untracked `openhasp/` remain; no build products or local credentials are staged or committed.
