# Monitor Settings And Captive Portal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver and flash a three-page NAS/PVE/Settings firmware with real PVE HTTPS and Synology SNMP data, persistent Wi-Fi/display settings, and a button-triggered WPA2 captive portal for PVE credentials.

**Architecture:** Keep LVGL calls in the UI task and keep all Wi-Fi, HTTP, DNS, HTTPS, and SNMP work outside it. Split temporary AP/HTTP/DNS provisioning from station networking, expose bounded status APIs to the Settings page, and use the existing one-item snapshot queue for live data. Store all credentials in the `device` NVS namespace with valid key lengths and trigger provider refresh through a network-manager event bit.

**Tech Stack:** ESP-IDF 4.4.8, FreeRTOS, LVGL 8.3.11, ESP HTTP server/client, ESP Wi-Fi AP+STA, lwIP UDP/DNS, NVS, cJSON, ST7796, FT5x06, SNMP v2c.

---

## File Structure

- `components/network_manager/network_manager.c`: station connection, asynchronous scan/connect, IP/status, settings-change event.
- `components/network_manager/provisioning_portal.c`: on-demand WPA2 AP, DNS wildcard responder, captive HTTP form, timeout and cleanup.
- `components/network_manager/include/network_manager.h`: bounded Wi-Fi and portal APIs consumed by UI and provider.
- `components/network_manager/include/device_settings.h`: shared NVS key constants and checked typed settings API.
- `components/network_manager/device_settings.c`: NVS string/u8 persistence with secret-preserving updates.
- `components/dashboard_ui/dashboard_ui.c`: fixed top navigation, Settings page, keyboard modal, display controls, portal status modal, redacted data errors.
- `components/app_model/live_provider.c`: corrected NVS keys, immediate refresh, TLS expected-name handling, stale snapshot preservation, live request diagnostics.
- `components/app_model/include/app_model.h`: snapshot freshness/error fields needed by UI.
- `main/app_main.c`: startup order and persisted display settings.
- `tests/test_dashboard_ui_contract.py`: top bar and Settings UI contracts.
- `tests/test_monitor_migration_contract.py`: NVS, portal, live provider, and security contracts.
- `tests/test_board_orientation_contract.py`: persisted rotation/brightness startup contracts.

### Task 1: Lock Top Navigation And Settings Contracts

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Write failing top-bar and page tests**

Add assertions equivalent to:

```python
def test_top_bar_orders_nas_status_pve_settings_without_power(self) -> None:
    self.assertIn('#define PAGE_COUNT 3', self.source)
    self.assertIn('make_button(bar, "NAS", 4, 3, 92, 26', self.source)
    self.assertIn('make_button(bar, "PVE", 292, 3, 92, 26', self.source)
    self.assertIn('make_button(bar, "设置", 388, 3, 88, 26', self.source)
    self.assertIn('"WiFi未连接，请到设置里面设置"', self.source)
    self.assertNotIn('"PWR"', self.source)

def test_settings_page_has_wifi_rotation_brightness_and_token_controls(self) -> None:
    self.assertIn('create_settings_page', self.source)
    self.assertIn('lv_dropdown_create', self.source)
    self.assertIn('lv_keyboard_create', self.source)
    self.assertIn('lv_slider_create', self.source)
    self.assertIn('PVE Token 设置', self.source)
```

- [ ] **Step 2: Write failing configuration/security tests**

Read `network_manager.c`, `provisioning_portal.c`, `device_settings.c`, and
`live_provider.c` in the test and assert:

```python
self.assertIn('"pve_secret"', settings_source)
self.assertNotIn('nvs_set_str(handle, "pve_token_secret"', settings_source)
self.assertIn('WIFI_AUTH_WPA2_PSK', portal_source)
self.assertIn('PROVISIONING_TIMEOUT_MS', portal_source)
self.assertIn('network_manager_wait_for_settings_change', provider_source)
self.assertNotIn('skip_cert_common_name_check = true', provider_source)
```

- [ ] **Step 3: Run the focused tests and verify RED**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract tests.test_monitor_migration_contract -v
```

Expected: failures for three pages, missing Settings controls, missing portal/settings files, and the old invalid PVE secret key.

- [ ] **Step 4: Commit tests**

```bash
git add tests/test_dashboard_ui_contract.py tests/test_monitor_migration_contract.py
git commit -m "test: define monitor settings and portal contracts"
```

### Task 2: Add Checked Persistent Device Settings

**Files:**
- Create: `components/network_manager/include/device_settings.h`
- Create: `components/network_manager/device_settings.c`
- Modify: `components/network_manager/CMakeLists.txt`
- Modify: `components/app_model/live_provider.c`
- Test: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Define bounded keys and public API**

Create constants whose key strings are all at most 15 characters:

```c
#define DEVICE_KEY_SSID "ssid"
#define DEVICE_KEY_PASSWORD "password"
#define DEVICE_KEY_PVE_HOST "pve_host"
#define DEVICE_KEY_PVE_NODE "pve_node"
#define DEVICE_KEY_PVE_TOKEN_ID "pve_token_id"
#define DEVICE_KEY_PVE_SECRET "pve_secret"
#define DEVICE_KEY_PVE_CA "pve_ca"
#define DEVICE_KEY_NAS_HOST "nas_host"
#define DEVICE_KEY_SNMP_COMMUNITY "snmp_community"
#define DEVICE_KEY_ROTATE_180 "rotate180"
#define DEVICE_KEY_BRIGHTNESS "brightness"

esp_err_t device_settings_get_string(const char *key, char *out, size_t size);
esp_err_t device_settings_set_string(const char *key, const char *value);
esp_err_t device_settings_set_string_if_present(const char *key, const char *value);
esp_err_t device_settings_get_u8(const char *key, uint8_t *value);
esp_err_t device_settings_set_u8(const char *key, uint8_t value);
```

`set_string_if_present` returns `ESP_OK` without writing when the submitted value
is empty, so reopening the portal cannot erase an existing secret.

- [ ] **Step 2: Implement checked NVS access**

Use namespace `device`, check every `nvs_open`, `nvs_get_*`, `nvs_set_*`, and
`nvs_commit` result, and close handles on all paths. Reject a null key/value and
keys longer than `NVS_KEY_NAME_MAX_SIZE - 1` with `ESP_ERR_INVALID_ARG`.

- [ ] **Step 3: Migrate live provider reads**

Replace local `load_string()` calls with `device_settings_get_string()` and read
the Token Secret from `DEVICE_KEY_PVE_SECRET`. Keep host/node defaults but never
default a secret or community.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: NVS key and secret-preservation contracts pass; portal-related tests
remain failing because Task 3 has not created the module yet.

- [ ] **Step 5: Commit settings layer**

```bash
git add components/network_manager/include/device_settings.h components/network_manager/device_settings.c components/network_manager/CMakeLists.txt components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "fix: persist monitor settings with valid NVS keys"
```

### Task 3: Implement On-Demand WPA2 Captive Portal

**Files:**
- Create: `components/network_manager/provisioning_portal.c`
- Modify: `components/network_manager/network_manager.c`
- Modify: `components/network_manager/include/network_manager.h`
- Modify: `components/network_manager/CMakeLists.txt`
- Test: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Define bounded portal status API**

Add:

```c
typedef struct {
    bool active;
    char ssid[33];
    char password[17];
    uint32_t seconds_remaining;
} network_portal_status_t;

esp_err_t network_manager_start_pve_portal(void);
void network_manager_stop_pve_portal(void);
void network_manager_get_portal_status(network_portal_status_t *status);
bool network_manager_wait_for_settings_change(TickType_t timeout);
```

The status copy is protected by a mutex. The password is available only to the
local LVGL task and is never logged.

- [ ] **Step 2: Implement AP lifecycle**

Generate an AP SSID from the MAC suffix and a per-session password from
`esp_fill_random()`. Configure AP+STA with `WIFI_AUTH_WPA2_PSK`, one channel, and
four clients. Start a five-minute FreeRTOS timeout task and make cleanup
idempotent so save, cancel, timeout, and reboot cannot double-stop services.

- [ ] **Step 3: Implement captive DNS and HTTP endpoints**

Run a UDP DNS task on port 53 that answers A queries with `192.168.4.1`. Register
common captive probes plus `/` to the PVE form and `/save` to the POST handler.
The form contains host, node, Token ID, blank Token Secret, and blank CA PEM.

Validate lengths before an atomic logical save. Save host/node/token ID normally,
save secret/CA only when non-empty, return a redacted error on any NVS failure,
set the settings-change event only after all writes succeed, then stop the AP.

- [ ] **Step 4: Remove the boot-time open portal behavior**

When Wi-Fi credentials are missing or a connection fails, keep the UI usable and
report disconnected state. Do not start the PVE portal automatically and do not
expose an HTTP server on the normal station interface.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: WPA2, timeout, valid NVS keys, no normal-LAN portal, and refresh-event
contracts pass.

- [ ] **Step 6: Commit portal**

```bash
git add components/network_manager/provisioning_portal.c components/network_manager/network_manager.c components/network_manager/include/network_manager.h components/network_manager/CMakeLists.txt tests/test_monitor_migration_contract.py
git commit -m "feat: add on-demand PVE captive portal"
```

### Task 4: Add Nonblocking Wi-Fi Settings APIs

**Files:**
- Modify: `components/network_manager/network_manager.c`
- Modify: `components/network_manager/include/network_manager.h`
- Test: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Add failing async Wi-Fi contracts**

Assert that the header exposes fixed-capacity scan records and these APIs:

```c
#define NETWORK_MAX_SCAN_RESULTS 12
typedef struct { char ssid[33]; int8_t rssi; } network_scan_record_t;
esp_err_t network_manager_start_scan(void);
size_t network_manager_get_scan_results(network_scan_record_t *out, size_t capacity, bool *complete);
esp_err_t network_manager_configure_wifi(const char *ssid, const char *password);
void network_manager_get_connected_ssid(char *out, size_t size);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: failure because asynchronous scan/configuration APIs are absent.

- [ ] **Step 3: Implement scan and connect workers**

Use FreeRTOS worker tasks for `esp_wifi_scan_start(..., true)` and station
reconfiguration. Copy at most 12 unique SSIDs under a mutex. Validate SSID and
password lengths, attempt the new connection, and commit custom NVS Wi-Fi values
only after `IP_EVENT_STA_GOT_IP`. Preserve previous values on failure.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the same unittest command. Expected: all network-manager contracts pass.

- [ ] **Step 5: Commit Wi-Fi APIs**

```bash
git add components/network_manager/network_manager.c components/network_manager/include/network_manager.h tests/test_monitor_migration_contract.py
git commit -m "feat: add asynchronous Wi-Fi settings APIs"
```

### Task 5: Build Three-Page LVGL Navigation And Settings

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `components/dashboard_ui/CMakeLists.txt`
- Test: `tests/test_dashboard_ui_contract.py`
- Test: `tests/test_board_orientation_contract.py`

- [ ] **Step 1: Implement fixed top navigation**

Set `PAGE_COUNT` to 3. Build fixed targets at NAS x=4 width=92, centered status
x=100 width=188, PVE x=292 width=92, and Settings x=388 width=88. Remove PWR and
the standalone connection dot. Render time on the first center line and IP or the
requested disconnected text on the second. Default to PVE after boot while
retaining top order NAS then PVE then Settings.

- [ ] **Step 2: Implement Settings page controls**

Create stable Wi-Fi, display, and monitor sections. Poll asynchronous scan state
from an LVGL timer, fill a dropdown with bounded SSIDs, and open a modal textarea
plus `lv_keyboard` for password input. Save/connect through
`network_manager_configure_wifi()` without blocking LVGL.

Use a two-button segmented control for 0/180 degrees. On success call
`wt32_board_set_rotation_180()`, persist `rotate180`, and invalidate the active
screen. Use a 10-100 slider for brightness, apply continuously, and persist on
release as `brightness`.

- [ ] **Step 3: Implement portal modal**

`PVE Token 设置` calls `network_manager_start_pve_portal()`. Show SSID, random
password, IP, countdown, and cancel in a top-layer modal. Refresh from
`network_manager_get_portal_status()` and close the modal when provisioning ends.

- [ ] **Step 4: Show redacted live errors**

When a section has no valid data, render its `pve_last_error` or `nas_last_error`
within the existing identity/title region with clipping. When stale data exists,
retain values and append `数据过期` without exposing credentials.

- [ ] **Step 5: Run UI and orientation tests**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract tests.test_board_orientation_contract -v
```

Expected: all top-bar, Settings, rotation, brightness, and existing interaction
contracts pass.

- [ ] **Step 6: Commit UI**

```bash
git add components/dashboard_ui/dashboard_ui.c components/dashboard_ui/CMakeLists.txt tests/test_dashboard_ui_contract.py tests/test_board_orientation_contract.py
git commit -m "feat: add monitor settings page"
```

### Task 6: Harden Live PVE And NAS Collection

**Files:**
- Modify: `components/app_model/include/app_model.h`
- Modify: `components/app_model/live_provider.c`
- Test: `tests/test_monitor_migration_contract.py`

- [ ] **Step 1: Add failing freshness and TLS tests**

Assert bounded section timestamps/freshness flags, a TLS `common_name` derived
from configured certificate identity rather than certificate checking disabled,
and provider waiting on `network_manager_wait_for_settings_change()` with a
10-second timeout.

- [ ] **Step 2: Run focused test and verify RED**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract -v
```

Expected: freshness, expected-name, and immediate-refresh assertions fail.

- [ ] **Step 3: Preserve last valid section snapshots**

Collect PVE and NAS into temporary section structures. Copy a section into the
published snapshot only after a complete successful collection. On failure keep
the previous metrics and lists, update online/configured/stale/error fields, and
record the last successful monotonic timestamp.

- [ ] **Step 4: Correct PVE parsing and TLS identity**

Parse all three `loadavg` array entries, strip `pve-manager/` from the version,
and retain fixed VM limits. Configure `.cert_pem` from NVS and `.common_name`
from the configured node/certificate identity while keeping the HTTPS URL on the
configured IP. Never set `skip_cert_common_name_check`.

- [ ] **Step 5: Validate SNMP request/response behavior**

Use GET (`0xA0`) for scalar OIDs and GETNEXT (`0xA1`) for table walking. Match
response request IDs, reject SNMP error-status responses, stop walks when the OID
leaves the Synology table, and retain all four discovered pools up to the fixed
maximum. Keep the community only in request memory and never log it.

- [ ] **Step 6: Wait for refresh or timeout**

Replace the fixed delay with
`network_manager_wait_for_settings_change(pdMS_TO_TICKS(10000))`, so portal saves
refresh immediately and normal polling remains every 10 seconds.

- [ ] **Step 7: Run all Python tests**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all tests pass with zero failures.

- [ ] **Step 8: Commit provider hardening**

```bash
git add components/app_model/include/app_model.h components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "fix: harden live PVE and Synology collection"
```

### Task 7: Load Display Settings At Startup And Build

**Files:**
- Modify: `main/app_main.c`
- Modify: `components/board_wt32/board_wt32.c`
- Modify: `tests/test_board_orientation_contract.py`

- [ ] **Step 1: Add startup persistence test**

Assert that startup reads `rotate180` and `brightness` after NVS initialization
and before the UI task begins, clamps brightness to 10-100, and applies both board
setters.

- [ ] **Step 2: Run orientation test and verify RED**

Run:

```bash
python3 -m unittest tests.test_board_orientation_contract -v
```

Expected: startup persistence assertions fail.

- [ ] **Step 3: Apply persisted display settings**

After `init_nvs()`, read rotation with default 1 and brightness with default 72.
Pass initial values into board initialization or apply them before creating LVGL
objects. Keep display and touch rotation synchronized.

- [ ] **Step 4: Run tests and full build**

Run:

```bash
python3 -m unittest discover -s tests -v
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py reconfigure
idf.py build
```

Expected: all tests pass; build exits 0; no enabled SPIRAM configuration appears;
application size remains below the detected 1664 KiB app slot.

- [ ] **Step 5: Commit startup and build integration**

```bash
git add main/app_main.c components/board_wt32/board_wt32.c tests/test_board_orientation_contract.py
git commit -m "feat: restore display settings at startup"
```

### Task 8: Provision, Flash, And Verify Real Hardware

**Files:**
- No repository credential files.
- Temporary certificate and provisioning payloads only under `/private/tmp`.

- [ ] **Step 1: Retrieve and inspect the PVE certificate chain**

Use `openssl s_client` against the configured PVE endpoint, save only under
`/private/tmp`, and inspect subject, issuer, SAN, validity, and SHA-256 fingerprint.
Reject an expired certificate or an identity that cannot be matched explicitly.

- [ ] **Step 2: Verify live PVE and NAS reachability from the workstation**

Call the two read-only PVE endpoints using the supplied runtime Token and the
retrieved CA. Query the required Synology scalar and RAID-table OIDs using the
supplied read-only community. Do not print or save secrets in the repository.
Expected: node plus dynamic VM data and four storage pools are returned.

- [ ] **Step 3: Re-read target partition table**

Read 4 KiB at `0x8000` from `/dev/cu.usbserial-01E725F0` and parse it. Expected:
`otadata=0xe000`, `app0=0x10000`, and app0 is larger than the built binary.

- [ ] **Step 4: Flash application and OTA data without erasing NVS**

Write `build/ota_data_initial.bin` at `0xe000` and
`build/wt32_dashboard.bin` at `0x10000` using ROM download mode at 115200 baud.
Expected: both write hashes verify.

- [ ] **Step 5: Provision credentials through the temporary portal**

Open the portal from Settings, connect to the displayed WPA2 AP, and submit the
runtime PVE host/node/Token/CA. Provision NAS host/community through checked NVS
settings without placing values in source. Confirm the AP closes after save.

- [ ] **Step 6: Capture clean boot and two refresh cycles**

Read at least 25 seconds of serial output. Expected log evidence:

```text
Touch controller at 0x38
Live PVE/NAS provider started
Created NAS/PVE/Settings LVGL pages
Wi-Fi connected
PVE refresh succeeded
NAS refresh succeeded: pools=4
```

No PSRAM error, reset loop, panic, secret, community, or CA body may appear.

- [ ] **Step 7: Perform physical interaction checks**

Verify NAS/PVE/Settings top targets, center time/IP, disconnected message path,
Wi-Fi keyboard flow, both rotations and touch corners, brightness persistence,
portal start/cancel/save, five-minute timeout, PVE VM pages, and all four NAS
pool rows. Record any item requiring user visual confirmation explicitly.

- [ ] **Step 8: Run final verification**

Run fresh:

```bash
python3 -m unittest discover -s tests -v
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py build
git diff --check
```

Expected: zero test failures, build exit 0, no whitespace errors, and no secret
values in `git diff` or tracked files.
