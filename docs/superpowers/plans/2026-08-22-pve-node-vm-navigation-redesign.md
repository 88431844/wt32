# PVE Node And VM Navigation Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the crowded PVE subnavigation with a node-first 480 x 320 view, a four-row VM list, and inline VM details while adding node uptime and CPU model data from the existing PVE status API.

**Architecture:** Extend the immutable PVE snapshot with two node fields and parse them in the existing direct HTTPS provider. Keep all LVGL objects and the explicit node/list/detail state machine in `dashboard_ui.c`; reuse the current snapshot delivery, cached-refresh behavior, formatters, themes, and VM detail data.

**Tech Stack:** ESP-IDF 4.4.8, C11, LVGL 8.3.11, cJSON, Python `unittest` contract tests, native C snapshot tests, browser UI preview.

---

## File Map

- `components/app_model/include/app_snapshot.h`: add node uptime and CPU model snapshot fields.
- `components/app_model/live_provider.c`: parse `uptime` and `cpuinfo.model` from `/nodes/{node}/status`.
- `components/dashboard_ui/dashboard_ui.c`: replace PVE subnavigation with the node/list/detail state machine and render the approved geometry.
- `components/dashboard_ui/fonts/app_font_14.c`: regenerate only if the static-text glyph contract identifies missing Chinese glyphs.
- `tests/test_monitor_migration_contract.py`: assert the new snapshot and provider contract.
- `tests/test_dashboard_ui_contract.py`: replace old PVE identity/subnavigation assertions with the approved layout and event contract.
- `docs/superpowers/specs/2026-08-22-pve-node-vm-navigation-redesign.md`: authoritative approved behavior; no edits expected during implementation.

### Task 1: Collect Node Uptime And CPU Model

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/include/app_snapshot.h`
- Modify: `components/app_model/live_provider.c`

- [ ] **Step 1: Write the failing provider contract test**

Add this method to `MonitorMigrationContractTest`:

```python
def test_pve_node_status_collects_uptime_and_cpu_model(self):
    for declaration in (
        "uint32_t pve_uptime_seconds;",
        "char pve_cpu_model[APP_TEXT_LARGE];",
    ):
        self.assertIn(declaration, MODEL)

    parser = PROVIDER[
        PROVIDER.index("static void parse_pve_node"):
        PROVIDER.index("static bool parse_pve_guests")
    ]
    for marker in (
        'json_u64(data, "uptime")',
        'json_string(cpuinfo, "model")',
        "snapshot->pve_uptime_seconds",
        "snapshot->pve_cpu_model",
    ):
        self.assertIn(marker, parser)
    self.assertNotIn("temperature", parser.lower())
```

- [ ] **Step 2: Run the focused test and verify the new contract fails**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_pve_node_status_collects_uptime_and_cpu_model -v
```

Expected: `FAIL` because the snapshot declarations and parser markers do not exist.

- [ ] **Step 3: Add the two snapshot fields**

In the PVE section of `app_snapshot_t`, after `pve_cpu_cores`, add:

```c
uint32_t pve_uptime_seconds;
char pve_cpu_model[APP_TEXT_LARGE];
```

Keep the fields inline in the snapshot so the existing zero-initialization, clone, and move operations remain sufficient.

- [ ] **Step 4: Parse uptime and CPU model from the existing node response**

Replace the one-off `cpuinfo` lookup in `parse_pve_node()` with:

```c
const cJSON *cpuinfo = cJSON_GetObjectItem(data, "cpuinfo");
snapshot->pve_cpu_cores = (uint32_t)json_u64(cpuinfo, "cpus");
snapshot->pve_uptime_seconds = (uint32_t)json_u64(data, "uptime");
copy_text(snapshot->pve_cpu_model, sizeof(snapshot->pve_cpu_model),
          json_string(cpuinfo, "model"));
```

Do not add another HTTP request and do not add a temperature field.

- [ ] **Step 5: Run the focused and snapshot lifecycle tests**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_pve_node_status_collects_uptime_and_cpu_model tests.test_app_snapshot_native -v
```

Expected: both tests pass with no C compiler warnings.

- [ ] **Step 6: Commit the node data change**

```bash
git add tests/test_monitor_migration_contract.py components/app_model/include/app_snapshot.h components/app_model/live_provider.c
git commit -m "feat(pve): collect node identity metrics"
```

### Task 2: Build The Node-First PVE View

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Replace the obsolete identity-row contract with node-view contracts**

Remove `test_pve_identity_row_uses_three_fields_and_two_theme_aware_separators` and add these methods:

```python
def test_pve_node_view_has_approved_geometry_and_fields(self):
    for marker in (
        "#define VM_VISIBLE 4",
        "#define PVE_IDENTITY_HEIGHT 40",
        "#define PVE_METRIC_ROW_HEIGHT 58",
        "#define PVE_PROCESSOR_ROW_HEIGHT 40",
        "#define PVE_NARROW_WIDTH 168",
        "#define PVE_VM_BUTTON_Y 236",
        "lv_obj_t *pve_node_view;",
        "lv_obj_t *pve_uptime;",
        "lv_obj_t *pve_cpu_model;",
        "lv_obj_t *pve_vm_list_button;",
    ):
        self.assertIn(marker, UI)
    for label in ('"CPU"', '"系统负载"', '"内存"', '"存储"',
                  '"处理器"', '"虚拟机列表"'):
        self.assertIn(label, UI)
    self.assertNotIn("temperature", UI[UI.index("static void create_pve_page"):UI.index("static void show_nas_pools")].lower())

def test_pve_has_three_content_views_without_old_subnavigation(self):
    for marker in (
        "PVE_VIEW_NODE",
        "PVE_VIEW_VM_LIST",
        "PVE_VIEW_VM_DETAIL",
        "show_pve_node",
        "show_pve_vm_list",
        "show_vm_detail",
    ):
        self.assertIn(marker, UI)
    for removed in (
        "pve_overview_button",
        "vm_nav_buttons",
        "vm_nav_offset",
        "vm_detail_previous",
        "vm_detail_next",
        '"PVE 总览"',
        "PVE_SUBNAV_LEFT",
        "PVE_SUBNAV_RIGHT",
    ):
        self.assertNotIn(removed, UI)
```

- [ ] **Step 2: Run both UI tests and verify they fail**

Run:

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_node_view_has_approved_geometry_and_fields \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_has_three_content_views_without_old_subnavigation -v
```

Expected: `FAIL` on the old `VM_VISIBLE 5`, old subnavigation objects, and missing node-view fields.

- [ ] **Step 3: Define the PVE constants, state, and object ownership**

Replace the old VM navigation constants with:

```c
#define VM_VISIBLE 4
#define PVE_IDENTITY_HEIGHT 40
#define PVE_METRIC_ROW_HEIGHT 58
#define PVE_PROCESSOR_ROW_HEIGHT 40
#define PVE_NARROW_WIDTH 168
#define PVE_VM_BUTTON_Y 236
#define PVE_RULE_COUNT 6

typedef enum {
    PVE_VIEW_NODE = 0,
    PVE_VIEW_VM_LIST,
    PVE_VIEW_VM_DETAIL,
} pve_view_t;
```

In `dashboard_ui_state_t`, remove the old overview button, VM top-navigation arrays, offsets, detail previous/next buttons, and disk column objects from the list. Add:

```c
pve_view_t pve_view;
lv_obj_t *pve_node_view;
lv_obj_t *pve_vm_list;
lv_obj_t *pve_vm_list_button;
lv_obj_t *pve_uptime;
lv_obj_t *pve_cpu_model;
lv_obj_t *pve_cpu_cores_value;
lv_obj_t *pve_rules[PVE_RULE_COUNT];
lv_obj_t *vm_list_pager;
lv_obj_t *vm_list_previous;
lv_obj_t *vm_list_next;
lv_obj_t *vm_list_page;
lv_obj_t *vm_list_empty;
lv_obj_t *vm_name_buttons[VM_VISIBLE];
```

Retain the existing VM row labels, dots, IP, CPU, memory, detail labels, `vm_list_offset`, and `selected_vm_index`.

- [ ] **Step 4: Add the explicit node/list/detail visibility functions**

Implement one shared view setter and the two top-level transitions before the rendering functions:

```c
static void set_pve_view(pve_view_t view)
{
    s_ui.pve_view = view;
    set_hidden(s_ui.pve_node_view, view != PVE_VIEW_NODE);
    set_hidden(s_ui.pve_vm_list, view != PVE_VIEW_VM_LIST);
    set_hidden(s_ui.vm_detail, view != PVE_VIEW_VM_DETAIL);
}

static void show_pve_node(void)
{
    s_ui.selected_vm_index = -1;
    set_pve_view(PVE_VIEW_NODE);
}

static void show_pve_vm_list(void)
{
    s_ui.selected_vm_index = -1;
    set_pve_view(PVE_VIEW_VM_LIST);
    update_pve();
}
```

In `page_event()`, call `show_pve_node()` when `page == 1` before activating PVE polling. This guarantees that selecting the global PVE navigation always starts on the node view.

- [ ] **Step 5: Rebuild the PVE node view with fixed aligned geometry**

Inside `create_pve_page()` create `pve_node_view` at `(0, 0, 480, 288)`. Within it:

```c
lv_obj_t *identity = make_surface(s_ui.pve_node_view, 8, 4, 464,
                                  PVE_IDENTITY_HEIGHT);
lv_obj_t *metrics = make_surface(s_ui.pve_node_view, 8, 48, 464, 158);
s_ui.pve_vm_list_button = make_button(s_ui.pve_node_view, "虚拟机列表",
                                      8, PVE_VM_BUTTON_Y, 464, 44,
                                      pve_vm_list_button_event, NULL);
```

Place the status/name, version, IP, and uptime on the identity row in that order. In `metrics`, put CPU and memory in the shared `PVE_NARROW_WIDTH` left column, load and storage in the shared right column, then create a full-width 40-pixel processor row. Use three identity dividers, one continuous 116-pixel metric divider, and two horizontal rules; store all six in `pve_rules` so `apply_theme()` recolors them.

Keep CPU, memory, and storage progress bars. Use `app_font_18` only for the primary numeric values; labels, load values, identity values, processor core count, and CPU model use `app_font_14` with fixed one-line widths.

- [ ] **Step 6: Render all node fields and offline fallbacks**

In `update_pve()`:

```c
char uptime[32];
format_uptime(uptime, sizeof(uptime), snapshot->pve_uptime_seconds);
lv_label_set_text(s_ui.pve_uptime,
                  snapshot->pve_uptime_seconds > 0 ? uptime : "--");
lv_label_set_text_fmt(s_ui.pve_cpu_cores_value, "%" PRIu32 " 核",
                      snapshot->pve_cpu_cores);
lv_label_set_text(s_ui.pve_cpu_model,
                  snapshot->pve_cpu_model[0] ? snapshot->pve_cpu_model : "--");
lv_label_set_text_fmt(lv_obj_get_child(s_ui.pve_vm_list_button, 0),
                      "虚拟机列表  %" PRIu32 "/%zu 运行",
                      snapshot->pve_running_count, snapshot->pve_guest_count);
```

Keep the existing online status dot, name/version/host, CPU, load, memory, storage, progress bars, and `--` handling. Update `show_monitor_message()` to clear uptime, processor fields, metrics, bars, and VM rows without referencing removed subnavigation objects.

- [ ] **Step 7: Run the focused UI contract tests**

Run the two tests from Step 2 plus:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: all Dashboard UI contract tests pass; if the static glyph coverage test fails, record the missing characters for Task 4 rather than weakening the test.

- [ ] **Step 8: Commit the node view**

```bash
git add tests/test_dashboard_ui_contract.py components/dashboard_ui/dashboard_ui.c
git commit -m "feat(ui): add PVE node overview"
```

### Task 3: Implement VM List And Detail Navigation

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Add failing contracts for list geometry and event routing**

Add these tests:

```python
def test_pve_vm_list_has_four_aligned_rows_and_vertical_pager(self):
    for marker in (
        "#define VM_VISIBLE 4",
        "vm_name_buttons",
        "vm_list_previous",
        "vm_list_next",
        "vm_list_page",
        "vm_list_empty",
        "420, 0",
        "44, 276",
        "30 + i * 58",
    ):
        self.assertIn(marker, UI)
    for header in ('"虚拟机"', '"IP"', '"CPU"', '"内存"'):
        self.assertIn(header, UI)
    pve_creation = UI[UI.index("static void create_pve_page"):UI.index("static void show_nas_pools")]
    self.assertNotIn("vm_row_disk", pve_creation)

def test_pve_vm_events_route_name_detail_background_node_and_detail_list(self):
    for marker in (
        "pve_vm_list_button_event",
        "vm_name_event",
        "vm_list_return_event",
        "vm_detail_return_event",
        "vm_list_page_event",
        "lv_event_stop_bubbling(event)",
    ):
        self.assertIn(marker, UI)
    self.assertIn("show_vm_detail", UI)
    self.assertIn("show_pve_node", UI)
    self.assertIn("show_pve_vm_list", UI)

def test_pve_snapshot_changes_clamp_list_and_detail_state(self):
    update = UI[UI.index("static void update_pve(void)") : UI.index("static void update_nas(void)")]
    for marker in (
        "last_page_offset",
        "s_ui.vm_list_offset > last_page_offset",
        "s_ui.selected_vm_index >= (int)snapshot->pve_guest_count",
        "show_pve_vm_list()",
    ):
        self.assertIn(marker, update)
```

- [ ] **Step 2: Run the three tests and verify they fail**

Run:

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_vm_list_has_four_aligned_rows_and_vertical_pager \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_vm_events_route_name_detail_background_node_and_detail_list \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_snapshot_changes_clamp_list_and_detail_state -v
```

Expected: `FAIL` because the new VM list objects and callbacks do not exist.

- [ ] **Step 3: Add the event callbacks with explicit bubbling control**

Implement these behaviors:

```c
static void pve_vm_list_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_pve_vm_list();
}

static void vm_name_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    show_vm_detail((size_t)(intptr_t)lv_event_get_user_data(event));
}

static void vm_list_return_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_pve_node();
}

static void vm_detail_return_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    show_pve_vm_list();
}
```

The pager container and page buttons also call `lv_event_stop_bubbling(event)` before updating offsets. Page by `VM_VISIBLE`, then call `update_pve()`.

- [ ] **Step 4: Create the four-row VM list**

Create `pve_vm_list` as a clickable surface at `(8, 4, 464, 276)`. Add a 420-pixel main area and `vm_list_pager` at `(420, 0, 44, 276)`. The header labels are `虚拟机`, `IP`, `CPU`, and `内存`.

For each of four rows, create a non-scrollable clickable row at `y = 30 + i * 58`, width 420 and height 58. Create a separate muted button in the name column containing the status dot, VMID/name label, and `>` label. Place IP, CPU, and memory labels as row siblings on the same baseline. Attach `vm_name_event` only to the name button and `vm_list_return_event` to each row and the list background.

Create the right pager with disabled-state opacity, page label, upper button, and lower button. Hide it when the guest count is at most four. Add a centered `vm_list_empty` label for zero guests or offline first-load state.

- [ ] **Step 5: Expand and simplify the reusable VM detail surface**

Move the detail surface to `(8, 4, 464, 276)`. Remove previous/next controls. Add a 30 x 30 `<` button in the title row and make the entire detail surface clickable. Both use `vm_detail_return_event`; the button stops bubbling.

Keep the existing detail fields and two-column organization. Change VM uptime rendering to use `format_uptime()` instead of raw seconds:

```c
char uptime[32];
format_uptime(uptime, sizeof(uptime), guest->uptime_seconds);
lv_label_set_text_fmt(s_ui.vm_detail_uptime, "运行时间  %s", uptime);
```

- [ ] **Step 6: Update list rendering, pagination, and state convergence**

In `update_pve()` calculate page offsets by four:

```c
const size_t last_page_offset = snapshot->pve_guest_count > VM_VISIBLE ?
    ((snapshot->pve_guest_count - 1) / VM_VISIBLE) * VM_VISIBLE : 0;
if ((size_t)s_ui.vm_list_offset > last_page_offset)
    s_ui.vm_list_offset = (int)last_page_offset;
```

Render name, IP, CPU, and memory only. Update name-button callback user data after each snapshot. Show and disable pager controls consistently with the NAS pager. If `selected_vm_index` is no longer valid while `pve_view == PVE_VIEW_VM_DETAIL`, call `show_pve_vm_list()`.

- [ ] **Step 7: Run the focused and complete UI suites**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract tests.test_monitor_migration_contract -v
```

Expected: all PVE and NAS contracts pass; no old subnavigation assertion remains.

- [ ] **Step 8: Commit list and detail navigation**

```bash
git add tests/test_dashboard_ui_contract.py components/dashboard_ui/dashboard_ui.c
git commit -m "feat(ui): simplify PVE VM navigation"
```

### Task 4: Glyphs, Regression, Build, And Visual Verification

**Files:**
- Modify if required: `components/dashboard_ui/fonts/app_font_14.c`
- Verify: `components/dashboard_ui/dashboard_ui.c`
- Verify: `components/app_model/live_provider.c`
- Verify: `tests/`

- [ ] **Step 1: Run the static glyph coverage test**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract.DashboardUiContractTest.test_14px_font_covers_all_static_chinese_ui_text -v
```

Expected: `PASS`. If it fails, take the exact missing character set from the assertion, append only those characters to the `--symbols` argument recorded at the top of `app_font_14.c`, and regenerate the font with the same size, BPP, range, compression, kerning, include, and output options. Rerun until the test passes.

- [ ] **Step 2: Run every Python and native test**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all tests pass, including snapshot C compilation, NAS disk sorting, board orientation, migration, API, and UI contracts.

- [ ] **Step 3: Build and check firmware size**

Run:

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py build
idf.py size
```

Expected: ESP-IDF 4.4.8 build succeeds with no compiler errors; the application fits the configured OTA partition.

- [ ] **Step 4: Verify the rendered UI at native size**

Use the existing local browser preview approach at a native 480 x 320 device surface. Capture and inspect these states:

1. Node view: name/version/IP/uptime in one row; CPU and memory share the 168-pixel column; load and storage share the wide column; 40-pixel processor row is visible; bottom VM button fits.
2. VM list: four rows; VM name, IP, CPU, and memory share each baseline; vertical pager does not cover text.
3. VM detail: all eight existing fields fit; no previous/next subnavigation remains.
4. Loading, offline, zero-VM, one-page, and multi-page states: no overlap, stale controls, or hidden status message.

Compare the node view against the approved `PVE 节点页 · 第三版` visual companion. Fix every visible clipping, wrapping, alignment, or touch-target mismatch before continuing.

- [ ] **Step 5: Flash without erasing NVS and inspect serial output**

With the known WT32-SC01 connected, run the repository's normal erase-free flash and monitor flow:

```bash
idf.py -p /dev/cu.usbserial-* flash monitor
```

Expected: clean boot, `Created NAS/PVE/Settings LVGL pages`, successful PVE refresh, no reset loop, and no TLS/provider regression. Exit the monitor after one complete PVE refresh.

- [ ] **Step 6: Verify touch behavior on the device**

Confirm these exact paths:

1. Global PVE navigation always opens the node view.
2. The full bottom VM-list button opens the list.
3. A VM name opens only that VM detail.
4. IP, CPU, memory, header, and list background return to the node view.
5. Pager buttons change pages without returning.
6. Detail surface and back button return to the list.
7. NAS and settings navigation remain unchanged.

- [ ] **Step 7: Commit any generated glyph or final verification fixes**

If Task 4 changed tracked files:

```bash
git add components/dashboard_ui/fonts/app_font_14.c components/dashboard_ui/dashboard_ui.c tests
git commit -m "fix(ui): finalize PVE node layout"
```

If no tracked files changed, do not create an empty commit.

- [ ] **Step 8: Confirm repository cleanliness and commit scope**

Run:

```bash
git status --short --branch
git log -5 --oneline --decorate
```

Expected: only the user's unrelated untracked `openhasp/` remains; all project changes are committed on `wt32-Nas-pve-monitor` and no unrelated files appear in any commit.
