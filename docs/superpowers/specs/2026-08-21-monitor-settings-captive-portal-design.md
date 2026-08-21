# Monitor Dashboard Settings And Captive Portal Design

## Goal

Update the WT32-SC01 PVE/NAS dashboard so it renders real monitoring data and
can be configured without embedding credentials in source control. The device
must retain responsive LVGL touch interaction while Wi-Fi, HTTPS, SNMP, and
temporary provisioning run in separate tasks.

This specification supersedes the navigation and provisioning behavior in the
older eight-tab design. The production UI has only NAS, PVE, and Settings pages.

## Top Navigation

The 480 x 32 top bar uses this fixed order:

```text
NAS | centered time and network status | PVE | Settings
```

- NAS and PVE are each 92 pixels wide so they remain comfortable touch targets.
- The center region is 188 pixels wide and has two centered lines: current time
  and either the station IP address or `WiFi未连接，请到设置里面设置`.
- Settings is an 88-pixel text button placed immediately after PVE.
- The PWR control is removed.
- Active NAS, PVE, and Settings pages use the existing blue selected state.
- Control dimensions remain fixed when labels or connection state change.

## Pages

### NAS

The NAS page keeps the four-row storage-pool layout and bottom CPU, memory, and
temperature metrics. Data comes only from live SNMP responses. Missing or stale
data is identified explicitly; Mock values are never substituted.

### PVE

The PVE page keeps the overview, VM navigation, paged VM list, and VM detail
dialog. Node and VM values come only from the live PVE API. The previous valid
snapshot remains visible after a transient failure and is marked stale or
offline with the most recent error summary.

### Settings

The Settings page contains three bounded sections:

1. Wi-Fi status and configuration: scan networks, select an SSID, enter a
   password with an LVGL keyboard modal, then save and connect.
2. Display controls: a 0/180-degree segmented orientation control and a 10-100%
   brightness slider with numeric value.
3. Monitoring provisioning: a `PVE Token 设置` command that starts the temporary
   captive portal described below.

Rotation applies immediately to both the ST7796 panel and FT5x06 coordinate
mapping. Rotation and brightness survive reboot. Wi-Fi settings are committed
only after validation and a connection attempt; failure leaves the previous
working configuration available.

## PVE Token Captive Portal

The captive portal starts only after the user taps `PVE Token 设置`.

1. The device enters AP+STA mode, preserving any working station connection.
2. It creates a per-session WPA2 AP named from the device identity and generates
   a random password of at least eight characters.
3. A modal on the WT32 displays the AP SSID, password, `192.168.4.1`, remaining
   setup time, and a cancel button.
4. A wildcard DNS responder redirects captive-portal probes to the local HTTP
   server so supported phones open the form automatically. Direct navigation to
   `http://192.168.4.1` remains available.
5. The form accepts PVE host, node, Token ID, Token Secret, and CA PEM. Existing
   secret values are never rendered into HTML. A blank secret or CA field keeps
   the existing stored value.
6. Values are validated and stored in the `device` NVS namespace using keys no
   longer than ESP-IDF's 15-character limit. The Token Secret key is
   `pve_secret`, replacing the invalid `pve_token_secret` key.
7. A successful save signals the live provider to refresh immediately, closes
   the portal, stops DNS and AP services, and returns Wi-Fi to STA mode.
8. Cancel, five minutes of inactivity, or reboot closes the portal without
   changing credentials.

The HTTP portal is protected by the temporary WPA2 link and is never left
running on the normal LAN. Token Secret, CA, and SNMP community are not logged,
returned in snapshots, committed to Git, or compiled into firmware.

## Persistent Configuration

NVS uses the `device` namespace and bounded values:

| Purpose | Key | Type |
| --- | --- | --- |
| Wi-Fi SSID | `ssid` | string |
| Wi-Fi password | `password` | string |
| PVE host | `pve_host` | string |
| PVE node | `pve_node` | string |
| PVE Token ID | `pve_token_id` | string |
| PVE Token Secret | `pve_secret` | string |
| PVE CA | `pve_ca` | string |
| NAS host | `nas_host` | string |
| SNMP community | `snmp_community` | string |
| Rotation | `rotate180` | u8 |
| Brightness | `brightness` | u8 |

Initial monitor credentials supplied for this device are provisioned directly
to NVS after flashing and are not added to repository files. The PVE certificate
chain is retrieved and inspected during provisioning. TLS uses CA validation and
an explicit expected certificate name when the API URL uses an IP address.

## Data Flow

```text
PVE HTTPS ----\
               -> live_provider task -> bounded snapshot queue -> LVGL task
NAS SNMP v2c -/             ^
                             +-- NVS settings / immediate refresh signal
```

- The live provider reloads configuration before each request cycle.
- Normal refresh remains 10 seconds; successful provisioning triggers an
  immediate cycle.
- Network calls never run in the LVGL task.
- PVE HTTPS uses CA verification and a bounded JSON response buffer.
- NAS uses read-only SNMP v2c and bounded BER response buffers.
- Failed requests retain the previous valid section data and update only the
  section state, age, and error text.
- Wi-Fi disconnects trigger background reconnect without blocking LVGL.

## Error Handling

- Missing Wi-Fi renders the requested actionable message in the top center and
  keeps Settings reachable.
- Missing PVE or NAS credentials render `未配置` plus a concise reason.
- NVS writes are checked; partial form saves are rejected and previous values
  remain active.
- Invalid CA, TLS name mismatch, HTTP status, JSON overflow, SNMP timeout, and
  OID mismatch are distinguishable in logs and in redacted UI error text.
- Captive portal services have explicit start, success, cancel, and timeout
  cleanup paths.
- Secrets are never included in error messages.

## Verification

Automated verification covers:

- top-bar order, dimensions, no PWR control, and offline network text;
- three-page NAS/PVE/Settings navigation;
- NVS key length and the `pve_secret` migration;
- blank-secret preservation and checked NVS save failures;
- captive portal start/stop/timeout state and no normal-LAN HTTP service;
- immediate refresh notification after provisioning;
- persistent rotation and brightness controls;
- existing PVE/NAS model bounds, pagination, and split capacity bars;
- a complete ESP-IDF build and partition-size check.

Hardware verification on `/dev/cu.usbserial-01E725F0` covers:

- erase-free flashing at the detected `otadata` and `app0` offsets;
- write hash verification and a clean boot without PSRAM errors;
- ST7796, FT5x06, LVGL, Wi-Fi, and live-provider initialization;
- all top navigation targets and Settings controls;
- both rotations, touch corners, and brightness persistence;
- temporary AP visibility, WPA2 connection, captive-page opening, save, AP
  shutdown, and five-minute timeout cleanup;
- live PVE node/VM values and all four live NAS storage pools after NVS
  provisioning.

## Out Of Scope

- OTA firmware delivery;
- exposing the captive portal permanently on the normal LAN;
- storing secrets in source, build flags, generated repository files, or logs;
- replacing read-only SNMP v2c with write access;
- adding clock, weather, network-throughput, or VM IP pages.
