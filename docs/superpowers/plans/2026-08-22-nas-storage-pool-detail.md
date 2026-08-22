# NAS Storage Pool Detail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PVE-style NAS storage-pool navigation and detail views, collect real Synology disk metadata and NAS uptime over SNMP, and render a compact two-row NAS metrics footer without clipping long capacity values.

**Architecture:** Extend the immutable `app_snapshot_t` with bounded pool-health, physical-disk, and NAS-uptime fields. Keep all SNMP work in `live_provider.c` and all LVGL mutation in `dashboard_ui.c`; one reusable NAS detail container renders the selected pool while explicitly treating the disk list as NAS-wide because SNMP does not expose reliable pool membership.

**Tech Stack:** ESP-IDF C, FreeRTOS queues, custom SNMP v2c BER parser, LVGL 8, Python `unittest` contract tests, CMake/Ninja.

---

## File Structure

- `components/app_model/include/app_model.h`: bounded NAS disk model, pool health, NAS uptime, and validity flags.
- `components/app_model/live_provider.c`: Synology Disk MIB traversal, `sysUpTime` conversion, and pool-health population.
- `components/dashboard_ui/dashboard_ui.c`: NAS overview/detail state machine, pool buttons, disk table, compact capacity formatting, and two-row footer.
- `tests/test_monitor_migration_contract.py`: snapshot and SNMP collection contracts.
- `tests/test_dashboard_ui_contract.py`: NAS navigation, detail, status-dot, footer, and capacity-layout contracts.

### Task 1: Add Bounded NAS Disk And Uptime Models

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/include/app_model.h`

- [ ] **Step 1: Write the failing model contract**

Add this test to `MonitorMigrationContractTest`:

```python
def test_snapshot_has_bounded_nas_disk_and_uptime_models(self) -> None:
    self.assertIn("#define APP_MAX_NAS_DISKS 8", APP_MODEL)
    self.assertIn("bool healthy", APP_MODEL)
    self.assertIn("char id[APP_TEXT_SMALL]", APP_MODEL)
    self.assertIn("char model[APP_TEXT_MEDIUM]", APP_MODEL)
    self.assertIn("bool temperature_valid", APP_MODEL)
    self.assertIn("bool capacity_valid", APP_MODEL)
    self.assertIn("nas_disk_t nas_disks[APP_MAX_NAS_DISKS]", APP_MODEL)
    self.assertIn("uint32_t nas_disk_count", APP_MODEL)
    self.assertIn("uint32_t nas_uptime_seconds", APP_MODEL)
    self.assertIn("bool nas_uptime_valid", APP_MODEL)
```

- [ ] **Step 2: Run the model contract and verify it fails**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_snapshot_has_bounded_nas_disk_and_uptime_models -v
```

Expected: `FAIL` because the NAS disk type and uptime fields do not exist.

- [ ] **Step 3: Add the bounded model**

Add `APP_MAX_NAS_DISKS`, pool health, and the disk type in `app_model.h`:

```c
#define APP_MAX_NAS_DISKS 8

typedef struct {
    char name[APP_TEXT_MEDIUM];
    char description[APP_TEXT_LARGE];
    char filesystem[APP_TEXT_SMALL];
    char raid_type[APP_TEXT_SMALL];
    uint64_t used_bytes;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool healthy;
} nas_pool_t;

typedef struct {
    char id[APP_TEXT_SMALL];
    char model[APP_TEXT_MEDIUM];
    bool healthy;
    int temperature_c;
    bool temperature_valid;
    uint64_t capacity_bytes;
    bool capacity_valid;
} nas_disk_t;
```

Add these fields to the NAS section of `app_snapshot_t`:

```c
uint32_t nas_uptime_seconds;
bool nas_uptime_valid;
uint32_t nas_disk_count;
nas_disk_t nas_disks[APP_MAX_NAS_DISKS];
```

- [ ] **Step 4: Run the model contract and verify it passes**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_snapshot_has_bounded_nas_disk_and_uptime_models -v
```

Expected: `OK`.

- [ ] **Step 5: Commit the model change**

```bash
git add components/app_model/include/app_model.h tests/test_monitor_migration_contract.py
git commit -m "feat: add NAS disk snapshot model"
```

### Task 2: Collect Synology Disk Metadata And NAS Uptime

**Files:**
- Modify: `tests/test_monitor_migration_contract.py`
- Modify: `components/app_model/live_provider.c`

- [ ] **Step 1: Write the failing SNMP collection contract**

Add this test:

```python
def test_provider_collects_synology_disks_and_nas_uptime(self) -> None:
    source = PROVIDER.read_text(encoding="utf-8")
    self.assertIn('"1.3.6.1.2.1.1.3.0"', source)
    self.assertIn('"1.3.6.1.4.1.6574.2.1.1.2"', source)
    self.assertIn('"1.3.6.1.4.1.6574.2.1.1.3"', source)
    self.assertIn('"1.3.6.1.4.1.6574.2.1.1.5"', source)
    self.assertIn('"1.3.6.1.4.1.6574.2.1.1.6"', source)
    self.assertIn("collect_nas_disks", source)
    self.assertIn("ticks / 100ULL", source)
    self.assertIn("pool->healthy = raid_status == 1", source)
    self.assertIn("disk->capacity_valid = false", source)
```

- [ ] **Step 2: Run the provider contract and verify it fails**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_provider_collects_synology_disks_and_nas_uptime -v
```

Expected: `FAIL` because disk traversal and NAS uptime collection are absent.

- [ ] **Step 3: Add disk status mapping and Disk MIB traversal**

Add the following helper next to `synology_raid_status()`:

```c
static bool synology_disk_healthy(uint64_t status)
{
    return status == 1;
}

static void collect_nas_disks(const char *host, const char *community,
                              app_snapshot_t *snapshot)
{
    static const char *disk_id_oid = "1.3.6.1.4.1.6574.2.1.1.2";
    char cursor[96];
    copy_text(cursor, sizeof(cursor), disk_id_oid);
    snapshot->nas_disk_count = 0;

    for (size_t i = 0; i < APP_MAX_NAS_DISKS * 2 &&
                       snapshot->nas_disk_count < APP_MAX_NAS_DISKS; ++i) {
        snmp_value_t value;
        if (!snmp_exchange(host, community, cursor, true, &value) ||
            !oid_in_subtree(value.oid, disk_id_oid) ||
            value.type != SNMP_VALUE_STRING) break;
        copy_text(cursor, sizeof(cursor), value.oid);

        nas_disk_t *disk = &snapshot->nas_disks[snapshot->nas_disk_count++];
        memset(disk, 0, sizeof(*disk));
        copy_text(disk->id, sizeof(disk->id), value.text);
        disk->capacity_valid = false;

        char oid[112];
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.3", value.last_index);
        (void)snmp_get_text(host, community, oid, disk->model, sizeof(disk->model));

        uint64_t status = 0;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.5", value.last_index);
        disk->healthy = snmp_get_number(host, community, oid, &status) &&
                        synology_disk_healthy(status);

        uint64_t temperature = 0;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.6", value.last_index);
        disk->temperature_valid = snmp_get_number(host, community, oid, &temperature) &&
                                  temperature <= 150;
        if (disk->temperature_valid) disk->temperature_c = (int)temperature;
    }
}
```

Do not query `.6574.2.1.1.4` as capacity: in Synology Disk MIB it is the disk type string. Leave `capacity_valid` false until a verified physical-capacity OID exists.

- [ ] **Step 4: Collect uptime and pool health inside `collect_nas()`**

After setting `nas_configured`, collect `sysUpTime` ticks:

```c
uint64_t ticks = 0;
snapshot->nas_uptime_valid = snmp_get_number(
    host, community, "1.3.6.1.2.1.1.3.0", &ticks);
snapshot->nas_uptime_seconds = snapshot->nas_uptime_valid ?
                               (uint32_t)(ticks / 100ULL) : 0;
```

When RAID status is returned, populate health before formatting the description:

```c
pool->healthy = raid_status == 1;
```

After the storage-pool walk and before network collection, call:

```c
collect_nas_disks(host, community, snapshot);
```

Extend the success log to expose real probe results without credentials:

```c
ESP_LOGI(TAG, "NAS refresh succeeded: pools=%" PRIu32 " disks=%" PRIu32
              " uptime=%s",
         snapshot.nas_pool_count, snapshot.nas_disk_count,
         snapshot.nas_uptime_valid ? "yes" : "no");
```

- [ ] **Step 5: Run the provider contract and all monitor contracts**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: all tests report `OK`.

- [ ] **Step 6: Commit the provider change**

```bash
git add components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "feat: collect NAS disk metadata and uptime"
```

### Task 3: Build PVE-Style NAS Overview And Pool Detail Navigation

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Replace the old one-line-footer contract with NAS navigation contracts**

Replace `test_nas_footer_is_one_text_height_and_includes_network_rates` and add detail assertions:

```python
def test_nas_layout_has_pool_navigation_overview_and_detail(self) -> None:
    for field in (
        "nas_overview_button", "nas_pool_buttons", "nas_pool_button_dots",
        "nas_overview", "nas_detail", "selected_nas_pool_index",
        "show_nas_overview", "show_nas_detail", "nas_disk_rows",
    ):
        self.assertIn(field, self.source)
    self.assertIn('make_button(page, "NAS 总览"', self.source)
    self.assertIn('"存储池%d"', self.source)
    self.assertIn('"NAS 物理盘（未按池映射）"', self.source)

def test_nas_footer_is_two_rows_and_includes_ip_uptime_and_network(self) -> None:
    self.assertIn("#define NAS_METRICS_HEIGHT 36", self.source)
    self.assertIn("nas_network_value", self.source)
    self.assertIn("nas_ip_value", self.source)
    self.assertIn("nas_uptime_value", self.source)
    self.assertIn("format_rate", self.source)
    self.assertIn("format_uptime", self.source)

def test_nas_capacity_layout_uses_compact_values_and_status_dots(self) -> None:
    self.assertIn("format_capacity", self.source)
    self.assertIn('"%s/%s 剩%s"', self.source)
    self.assertIn("nas_row_status_dots", self.source)
    self.assertIn("pool->healthy ? COLOR_GREEN : COLOR_RED", self.source)
    self.assertNotIn('"池 %d  %s"', self.source)
```

- [ ] **Step 2: Run the UI contracts and verify they fail**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: the new NAS tests fail because the current page has only a title and four static rows.

- [ ] **Step 3: Add NAS UI state and formatter helpers**

Change the footer constant and add the bounded visible disk count:

```c
#define NAS_VISIBLE 4
#define NAS_DISK_VISIBLE 4
#define NAS_METRICS_HEIGHT 36
```

Add these members to `dashboard_context_t`:

```c
lv_obj_t *nas_overview_button;
lv_obj_t *nas_pool_buttons[NAS_VISIBLE];
lv_obj_t *nas_pool_button_labels[NAS_VISIBLE];
lv_obj_t *nas_pool_button_dots[NAS_VISIBLE];
lv_obj_t *nas_overview;
lv_obj_t *nas_detail;
lv_obj_t *nas_detail_title;
lv_obj_t *nas_detail_status_dot;
lv_obj_t *nas_detail_capacity;
lv_obj_t *nas_disk_rows[NAS_DISK_VISIBLE];
lv_obj_t *nas_disk_dots[NAS_DISK_VISIBLE];
lv_obj_t *nas_disk_ids[NAS_DISK_VISIBLE];
lv_obj_t *nas_disk_models[NAS_DISK_VISIBLE];
lv_obj_t *nas_disk_temperatures[NAS_DISK_VISIBLE];
lv_obj_t *nas_disk_capacities[NAS_DISK_VISIBLE];
lv_obj_t *nas_row_status_dots[NAS_VISIBLE];
lv_obj_t *nas_ip_value;
lv_obj_t *nas_uptime_value;
int selected_nas_pool_index;
```

Add formatters that preserve the full capacity suffix and keep the footer compact:

```c
static void format_capacity(char *out, size_t size, uint64_t bytes)
{
    if (bytes >= (100ULL << 40)) snprintf(out, size, "%.0fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (1ULL << 40)) snprintf(out, size, "%.1fT", (double)bytes / (1ULL << 40));
    else if (bytes >= (100ULL << 30)) snprintf(out, size, "%.0fG", (double)bytes / (1ULL << 30));
    else if (bytes >= (1ULL << 30)) snprintf(out, size, "%.1fG", (double)bytes / (1ULL << 30));
    else format_bytes(out, size, bytes);
}

static void format_uptime(char *out, size_t size, uint32_t seconds)
{
    const uint32_t days = seconds / 86400U;
    const uint32_t hours = seconds / 3600U % 24U;
    const uint32_t minutes = seconds / 60U % 60U;
    if (days > 0) snprintf(out, size, "%" PRIu32 "天 %" PRIu32 "时", days, hours);
    else snprintf(out, size, "%" PRIu32 "时 %" PRIu32 "分", hours, minutes);
}
```

- [ ] **Step 4: Create one NAS overview and one reusable detail surface**

Implement `create_nas_page()` with these exact geometry rules:

- `NAS 总览` at `(8, 4, 94, 24)`.
- Four pool buttons start at `x=106`, width `88`, height `24`, with a status dot and `存储池N` label.
- Overview container at `(0, 30, 480, 214)`; four rows at `x=8`, `y=4+i*51`, width `464`, height `47`.
- Row dot at `x=10`; pool name column width `92`; description width `86`; capacity value right-aligned in a `252`-pixel column; progress bar width `440`.
- Detail surface at `(8, 34, 464, 206)` with one pool summary and a four-row disk table. Table columns reserve `58`, `210`, `70`, and `90` pixels for disk ID, model, temperature, and capacity.
- Footer surface at `(8, 248, 464, 36)`. First row contains CPU, memory, and temperature; second row contains network, NAS IP, and uptime. Each label gets an explicit column width and `LV_LABEL_LONG_CLIP` only within its own column.

Build the pool buttons using LVGL button objects plus `make_status_dot()` so status never depends on a missing glyph.

- [ ] **Step 5: Add NAS navigation and detail rendering**

Implement these state transitions:

```c
static void show_nas_overview(void)
{
    s_ui.selected_nas_pool_index = -1;
    set_hidden(s_ui.nas_overview, false);
    set_hidden(s_ui.nas_detail, true);
    set_button_selected(s_ui.nas_overview_button, true);
    for (int i = 0; i < NAS_VISIBLE; ++i)
        set_button_selected(s_ui.nas_pool_buttons[i], false);
}

static void show_nas_detail(size_t index)
{
    if (index >= s_ui.snapshot.nas_pool_count || index >= NAS_VISIBLE) return;
    s_ui.selected_nas_pool_index = (int)index;
    set_hidden(s_ui.nas_overview, true);
    set_hidden(s_ui.nas_detail, false);
    set_button_selected(s_ui.nas_overview_button, false);
    for (int i = 0; i < NAS_VISIBLE; ++i)
        set_button_selected(s_ui.nas_pool_buttons[i], i == (int)index);
    update_nas_detail();
}
```

Wire `LV_EVENT_CLICKED` callbacks for the overview button, all pool buttons, and overview rows. In `update_nas_detail()`, render the selected pool summary, set the pool dot from `pool->healthy`, label the table `NAS 物理盘（未按池映射）`, and render all available disks. Use `--` when `temperature_valid` or `capacity_valid` is false.

- [ ] **Step 6: Update the NAS overview and two-row footer**

In `update_nas()`:

- Hide absent pool buttons and rows.
- Set each visible button label to `存储池%d`.
- Set button and row dots with `pool->healthy ? COLOR_GREEN : COLOR_RED`.
- Set overview capacity with `"%s/%s 剩%s"` after `format_capacity()`.
- Use red text only for non-healthy description; omit the `正常` word when healthy.
- Set `nas_ip_value` from `snapshot->nas_host` when configured, otherwise `IP --`.
- Set `nas_uptime_value` from `nas_uptime_seconds` only when `nas_uptime_valid`, otherwise `运行 --`.
- If the selected pool no longer exists, call `show_nas_overview()`; otherwise refresh the detail.

Initialize `selected_nas_pool_index = -1` in `dashboard_ui_create()`.

- [ ] **Step 7: Run UI contracts and all Python tests**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
python3 -m unittest discover -s tests -v
```

Expected: both commands report `OK` with no failures.

- [ ] **Step 8: Commit the UI change**

```bash
git add components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git commit -m "feat: add NAS pool detail navigation"
```

### Task 4: Build And Verify The Firmware

**Files:**
- Verify: `components/app_model/include/app_model.h`
- Verify: `components/app_model/live_provider.c`
- Verify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Run whitespace and contract checks**

Run:

```bash
git diff --check
python3 -m unittest discover -s tests -v
```

Expected: no whitespace errors and all tests report `OK`.

- [ ] **Step 2: Build the ESP-IDF firmware**

Use the repository's configured ESP-IDF environment and run:

```bash
idf.py build
```

Expected: Ninja finishes successfully and produces the application binary without C compiler warnings introduced by these changes.

- [ ] **Step 3: Verify model size remains bounded**

Inspect the build size output:

```bash
idf.py size-components
```

Expected: `app_model` and `dashboard_ui` fit the current application partition and no dynamic per-disk allocation was added.

- [ ] **Step 4: Probe live NAS data on hardware without erasing settings**

Flash only the application image using the existing non-erasing workflow, then monitor logs. Expected log shape:

```text
NAS refresh succeeded: pools=4 disks=4 uptime=yes
```

Confirm on screen:

- `NAS 总览` and four pool buttons fit one row and respond to touch.
- Healthy pools and disks use green solid dots; abnormal or unknown states use red solid dots.
- A long value such as `887G/1.8T 剩977G` remains complete.
- Pool detail shows real disk model and temperature values.
- Physical capacity shows `--` unless a verified capacity source was added.
- CPU, memory, temperature, network, NAS IP, and NAS uptime occupy two non-overlapping rows.

- [ ] **Step 5: Record the verified API result**

Update the task handoff with the observed disk count, whether model and temperature were returned, whether any trustworthy capacity OID was found, and any fields that remained `--`. Do not add credentials, SNMP community strings, or NAS secrets to logs or repository files.

