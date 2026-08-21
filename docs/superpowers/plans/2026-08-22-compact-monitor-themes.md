# Compact Monitor Themes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver and flash a compact five-theme WT32-SC01 monitor that polls only the visible NAS or PVE page and adds real NAS network rates plus best-effort guest IPv4 addresses.

**Architecture:** Keep LVGL mutations in the UI task and all HTTPS/SNMP work in the live-provider task. Add a small nonblocking provider-control API for active page and refresh interval, extend the immutable snapshot with optional network/IP fields, and persist theme/interval through the existing checked NVS layer. Optional per-field failures degrade to `--` without invalidating a successful node or NAS snapshot.

**Tech Stack:** ESP-IDF 4.4.8, FreeRTOS, LVGL 8.3.11, cJSON, ESP-TLS, SNMP v2c/IF-MIB, NVS, Python `unittest`, `idf.py`, `esptool.py`.

---

## File Structure

- `components/app_model/include/app_model.h`: snapshot fields and provider-control API.
- `components/app_model/live_provider.c`: active-page scheduler, PVE guest IPv4 parsing, Synology descriptions, and IF-MIB rate calculation.
- `components/network_manager/include/device_settings.h`: persistent theme and interval keys.
- `components/dashboard_ui/dashboard_ui.c`: compact geometry, theme application, settings controls, local clock, and safe dynamic text.
- `components/dashboard_ui/fonts/app_font_14.c`: regenerated static Chinese subset.
- `components/dashboard_ui/fonts/app_font_18.c`: regenerated static Chinese subset.
- `tests/test_dashboard_ui_contract.py`: layout, theme, settings, and glyph contracts.
- `tests/test_monitor_migration_contract.py`: polling, NVS, provider, and API contracts.

The existing monitor migration is uncommitted and overlaps every implementation file. Do not create implementation commits that would silently include pre-existing user changes. The design and plan documents may be committed independently; code remains as a reviewable working-tree diff unless the user later requests a commit.

### Task 1: Lock Model, Settings, And UI Contracts

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Add failing persistent-setting and provider-control contracts**

Assert exact keys and public API:

```python
self.assertIn('#define DEVICE_KEY_THEME "theme_id"', settings_header)
self.assertIn('#define DEVICE_KEY_REFRESH "refresh_s"', settings_header)
self.assertIn("APP_MONITOR_NONE", model_header)
self.assertIn("APP_MONITOR_NAS", model_header)
self.assertIn("APP_MONITOR_PVE", model_header)
self.assertIn("app_model_set_active_monitor", model_header)
self.assertIn("app_model_set_refresh_seconds", model_header)
self.assertIn("network_manager_wait_for_settings_change", provider_source)
```

- [ ] **Step 2: Add failing snapshot and data-source contracts**

```python
self.assertIn("char ipv4_address[16]", model_header)
self.assertIn("uint64_t nas_rx_bytes_per_second", model_header)
self.assertIn("uint64_t nas_tx_bytes_per_second", model_header)
self.assertIn("network-get-interfaces", provider_source)
self.assertIn("1.3.6.1.2.1.31.1.1.1.6", provider_source)
self.assertIn("1.3.6.1.2.1.31.1.1.1.10", provider_source)
self.assertNotIn("pve_cpu_temperature", model_header)
```

- [ ] **Step 3: Add failing geometry, theme, and glyph contracts**

```python
self.assertIn("THEME_GRAPHITE", ui_source)
self.assertIn("THEME_COUNT 5", ui_source)
self.assertIn('"10"', ui_source)
self.assertIn('"30"', ui_source)
self.assertIn('"60"', ui_source)
self.assertIn('"120"', ui_source)
self.assertIn("NAS_METRICS_HEIGHT 24", ui_source)
self.assertIn("nas_network_value", ui_source)
self.assertNotIn('"●"', ui_source)
self.assertNotIn("%-12s", ui_source)
```

- [ ] **Step 4: Run focused tests and verify RED**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract tests.test_monitor_migration_contract -v
```

Expected: new contracts fail while existing monitor migration contracts continue to pass.

### Task 2: Add Snapshot Fields And Active-Monitor Control

**Files:**
- Modify: `components/app_model/include/app_model.h`
- Modify: `components/app_model/live_provider.c`
- Modify: `components/network_manager/include/device_settings.h`

- [ ] **Step 1: Add model fields and control enum**

Define:

```c
typedef enum {
    APP_MONITOR_NONE = 0,
    APP_MONITOR_NAS = 1,
    APP_MONITOR_PVE = 2,
} app_monitor_t;

typedef struct {
    uint32_t vmid;
    char name[APP_TEXT_MEDIUM];
    char kind[8];
    char ipv4_address[16];
    /* existing fields */
} pve_guest_t;

void app_model_set_active_monitor(app_monitor_t monitor);
void app_model_set_refresh_seconds(uint8_t seconds);
```

Add `nas_rx_bytes_per_second`, `nas_tx_bytes_per_second`, and `nas_network_rate_valid` to `app_snapshot_t`.

- [ ] **Step 2: Add persistent keys**

```c
#define DEVICE_KEY_THEME "theme_id"
#define DEVICE_KEY_REFRESH "refresh_s"
```

Both keys fit the NVS 15-character limit.

- [ ] **Step 3: Implement a one-item provider command queue**

Store `{active_monitor, refresh_seconds, refresh_now}` in a one-item FreeRTOS queue. `app_model_set_active_monitor()` clamps the enum and marks `refresh_now`; `app_model_set_refresh_seconds()` accepts only 10, 30, 60, or 120 and overwrites the command queue to wake the provider task.

- [ ] **Step 4: Replace unconditional dual collection**

In `live_provider_task`, collect only `APP_MONITOR_PVE` or `APP_MONITOR_NAS`. For `APP_MONITOR_NONE`, update clock/network status and wait without issuing HTTPS or SNMP. Page changes break the wait immediately. Interval waiting uses the selected seconds and does not add request duration to the next scheduled deadline.

- [ ] **Step 5: Run focused contracts**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: provider-control, keys, and active-page contracts pass.

### Task 3: Add Guest IPv4 And NAS Network Rates

**Files:**
- Modify: `components/app_model/live_provider.c`
- Modify: `components/app_model/include/app_model.h`
- Test: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Parse QEMU Guest Agent interfaces**

Replace per-guest `agent/ping` with:

```text
/api2/json/nodes/{node}/qemu/{vmid}/agent/network-get-interfaces
```

Walk `data.result[]` or `data[]` according to the returned PVE envelope, then each `ip-addresses[]`. Select the first IPv4 that is not `127.0.0.0/8`, `169.254.0.0/16`, or `0.0.0.0`. Copy at most 15 characters and set `guest_agent` only after a valid agent response. A response without a usable address leaves `ipv4_address` empty.

- [ ] **Step 2: Parse LXC interfaces best-effort**

Query:

```text
/api2/json/nodes/{node}/lxc/{vmid}/interfaces
```

Accept `inet` values such as `192.168.31.20/24`, strip the CIDR suffix, and apply the same IPv4 exclusions. HTTP 4xx or malformed optional responses must not set the node offline.

- [ ] **Step 3: Select an active NAS interface**

Walk IF-MIB `ifName` (`1.3.6.1.2.1.31.1.1.1.1`) and `ifOperStatus` (`1.3.6.1.2.1.2.2.1.8`). Prefer an `up(1)` non-loopback physical interface; reject `lo`, bridge-only, tunnel, and interface names with no usable counters. Use its table index for Counter64 queries.

- [ ] **Step 4: Calculate Counter64 rates**

Read `ifHCInOctets` (`1.3.6.1.2.1.31.1.1.1.6`) and `ifHCOutOctets` (`1.3.6.1.2.1.31.1.1.1.10`). Keep prior counters, interface index, and `esp_timer_get_time()` timestamp. Set rate valid only when interface matches, counters are monotonic, and elapsed time is at least 250 ms. Compute:

```c
rate = (current - previous) * 1000000ULL / elapsed_us;
```

Update the baseline after every valid sample. Counter resets and interface changes clear validity for one sample.

- [ ] **Step 5: Map SNMP-native volume status text**

Map Synology RAID status values to bounded text such as `正常`, `降级`, `故障`, or `状态 N`. Use the volume name/path already exposed by SNMP as description; do not label it as a DSM custom description or claim a RAID level.

- [ ] **Step 6: Run focused tests**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: Counter64, guest IPv4, optional-failure, and volume-status contracts pass.

### Task 4: Implement Themes, Compact Layout, And Refresh Controls

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `main/app_main.c`
- Test: `tests/test_dashboard_ui_contract.py`

- [ ] **Step 1: Define five palettes and default Graphite Green**

Create a `dashboard_palette_t` array with background, surface, alternate surface, line, text, muted, primary, selected-text, positive, warning, used, and free colors. Define `THEME_GRAPHITE` as the default index. Load and validate `theme_id` before `dashboard_ui_create()`.

- [ ] **Step 2: Convert themeable colors to shared styles**

Use text-role styles for normal, muted, positive, warning, and selected text instead of direct hard-coded values. `apply_theme()` updates root, surface, button, and text-role styles, refreshes active navigation/button states, calls `update_pve()` and `update_nas()`, and invalidates the screen.

- [ ] **Step 3: Flatten the top bar**

Keep the fixed 32-pixel bar and place `NAS`, `HH:MM`, IP, `PVE`, and `设置` in one row. Add an LVGL timer that reads local time and updates `HH:MM` independently of provider snapshots.

- [ ] **Step 4: Compact NAS metrics to 24 pixels**

Move the fourth storage row upward as needed and create one 24-pixel footer. Render CPU, memory, temperature, upload, and download as inline labels. Use `format_rate()` for B/s, K/s, M/s, and G/s and `--` when the rate is invalid.

- [ ] **Step 5: Widen and format VM rows**

Shrink vertical arrow controls to the minimum practical touch width while retaining 40-pixel height. Give the list content columns for ID/name, IPv4, CPU, memory used/total, and disk used/total. Clip only the guest name; retain complete IP and numeric columns. Show `--` for missing IPv4.

- [ ] **Step 6: Add settings controls**

Add one theme row with previous/next buttons and the current palette name. Add four fixed refresh buttons. Theme and interval changes persist through `device_settings_set_u8`; refresh changes call `app_model_set_refresh_seconds()` immediately. Navigation calls `app_model_set_active_monitor()` with NAS, PVE, or NONE.

- [ ] **Step 7: Replace unsupported status glyphs**

Use small colored LVGL circle objects for node and VM state instead of `●`. Sanitize scanned dynamic SSIDs by replacing unsupported non-ASCII code points with `?` while preserving the original record index used for connection.

- [ ] **Step 8: Run UI contracts**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: all layout, theme, persistence, and glyph contracts pass.

### Task 5: Regenerate Fonts And Verify Firmware

**Files:**
- Modify: `components/dashboard_ui/fonts/app_font_14.c`
- Modify: `components/dashboard_ui/fonts/app_font_18.c`
- Test: all `tests/`

- [ ] **Step 1: Build the exact static glyph set**

Extract non-ASCII characters from all static `dashboard_ui.c` string literals, add `°`, `·`, `↑`, and `↓`, and regenerate both fonts from the locally licensed CJK font. Keep ASCII `0x20-0x7E`, 2-bit depth, no kerning, and the existing LVGL names.

- [ ] **Step 2: Verify source and font glyph coverage**

Run a script that compares every static non-ASCII UI character to the generated font comment/unicode map. Expected: no missing static code point and no literal bullet glyph in UI source.

- [ ] **Step 3: Run the complete test suite**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all tests pass.

- [ ] **Step 4: Build with ESP-IDF 4.4.8**

Activate the installed ESP-IDF environment, then run:

```bash
idf.py build
idf.py size
```

Expected: successful build; application remains below the `0x1d0000` OTA slot.

### Task 6: Erase-Free Flash And Serial Verification

**Files:**
- Use generated `build/` artifacts only.

- [ ] **Step 1: Resolve the exact board port**

Check `/dev/cu.usbserial-01E725F0` first because it is the previously documented WT32 port. If multiple ports exist, query chip identity before writing and do not guess from enumeration order.

- [ ] **Step 2: Flash without erasing NVS**

Run:

```bash
idf.py -p /dev/cu.usbserial-01E725F0 flash
```

Do not run `erase_flash`. Expected: bootloader/partition/app writes verify while the NVS partition remains untouched.

- [ ] **Step 3: Monitor boot and first requests**

Run the serial monitor long enough to confirm ST7796, FT5x06, LVGL, network manager, provider control, and first visible-page request. Switch NAS/PVE/Settings on hardware if touch access is available and confirm logs show immediate active-page changes without hidden-page requests.

- [ ] **Step 4: Report hardware-observable limits**

Record whether QEMU Guest Agent returned usable IPv4 values and whether Synology returned usable IF-MIB Counter64 data. Missing optional data must render `--`, not boxes or stale invented values.
