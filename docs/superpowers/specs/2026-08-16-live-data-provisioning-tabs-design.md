# Live Data, Wi-Fi Provisioning, And Eight-Tab UI Design

## Goal

Deliver a flashable WT32-SC01 firmware revision that:

- connects to Wi-Fi from persistent configuration;
- opens an unauthenticated `InfoDisplay` setup AP when Wi-Fi is not configured or cannot connect;
- lets the user configure Wi-Fi, Gateway URL, stock symbol, and fuel region in a captive web portal;
- obtains normalized stock and fuel data from Dashboard Gateway rather than calling third-party APIs from ESP32;
- replaces the bottom page dots with eight equal-width top tabs containing an icon and text;
- removes the standalone Album page;
- replaces the standalone Alerts page with a top notification drawer;
- retains page swiping, the persistent 180-degree rotation setting, and the existing page content not changed by this delivery.

The selected visual direction is browser prototype option A, `八页均分`.

## Scope And Page Order

The firmware and Gateway bootstrap expose these eight pages in order:

1. `资讯`
2. `日历`
3. `天气`
4. `PVE`
5. `NAS`
6. `额度`
7. `家居`
8. `设置`

The Album page is removed from navigation and snapshot payloads. Alerts remain a Gateway data section because they feed the notification drawer, but they are not a page. Clock data also remains a snapshot section for the top status bar.

PVE, NAS, Home Assistant, weather, calendar, quota, and notifications keep their current provider status in this increment. Mock-backed sections must display a local `DEMO` source marker; the UI must not imply that they are live. The Information page must not display fabricated stock prices, fuel prices, update times, or headlines.

## Architecture

```text
LongPort OpenAPI ----\
                     -> Dashboard Gateway -> HTTP JSON -> WT32-SC01
ShowAPI oil price ---/       |                    |
                             |                    +-> NVS device settings
                             +-> cache / stale fallback
```

Third-party credentials exist only in Dashboard Gateway environment variables. They are never returned by an API, saved in ESP32 NVS, shown in the captive portal, or committed to Git.

The Gateway owns provider authentication, symbol and region normalization, rate limiting, time-zone handling, caching, market-session decisions, and error conversion. The device owns network provisioning, polling the Gateway, compact model storage, and rendering.

The default Gateway URL is `http://dashboard-gateway.local:8080`. The captive portal exposes it as an editable field because mDNS resolution is not guaranteed on every network.

## Real Data Providers

### Stock

The primary stock provider is LongPort OpenAPI through the official `longport` Python package. Default symbol is BYD, `002594.SZ`. Portal input `002594` is normalized to `002594.SZ`; a full provider symbol is also accepted.

Gateway credentials are supplied as:

- `LONGPORT_APP_KEY`
- `LONGPORT_APP_SECRET`
- `LONGPORT_ACCESS_TOKEN`

Gateway reads quote fields and the current or most recent trading day's minute series. During an open market it returns today's minute series through the latest completed minute. After close, on weekends, and on exchange holidays it returns the latest completed trading day's complete series. The normalized response includes:

- symbol and display name;
- trading date and market status;
- current or final price;
- previous close, open, close, high, and low;
- absolute and percentage change;
- source update time and delayed flag;
- up to 242 one-minute `(minute, price)` points sorted by time.

The device displays open and close values. The chart uses the returned minute series; it never generates a decorative or random line. If LongPort is not configured and no valid cache exists, the stock area displays `行情未配置` instead of Mock data.

### Fuel

The primary fuel provider is ShowAPI `今日油价`. Gateway credentials are supplied as `SHOWAPI_APP_KEY`. The free allowance of 100 requests per day is sufficient because Gateway refreshes once daily after the upstream 07:00 Asia/Shanghai update and caches the result.

Oil prices are provincial reference prices. Captive portal input accepts a province or prefecture-level city. Gateway normalizes the input with a bundled administrative-division mapping; the default `深圳` maps to `广东`. The UI displays the normalized province so it does not suggest a city-specific pump price.

Only 92 and 95 gasoline are returned to the device. Each response includes the upstream update time. Missing, non-numeric, or structurally invalid prices are rejected and do not overwrite the last valid cache.

If ShowAPI is not configured and no valid cache exists, the fuel area displays `油价未配置`. If a refresh fails after a successful request, Gateway returns the last valid values with `stale` status and the original source update time.

### Provider Licensing Boundary

The integration is for the owner's private dashboard. Gateway does not expose a public resale feed. The README must state that LongPort and ShowAPI accounts and their applicable personal-use terms are the user's responsibility.

## Gateway API Contract

The device requests:

```text
GET /v1/snapshot?symbol=002594.SZ&fuel_region=深圳
```

The Information section adds normalized quote history and fuel metadata. A compact conceptual shape is:

```json
{
  "stock": {
    "symbol": "002594.SZ",
    "name": "比亚迪",
    "market_status": "closed",
    "trading_date": "2026-08-14",
    "price": 112.80,
    "previous_close": 111.22,
    "open": 111.26,
    "close": 112.80,
    "high": 114.05,
    "low": 110.91,
    "change": 1.58,
    "change_percent": 1.42,
    "updated_at": "2026-08-14T15:05:00+08:00",
    "delayed": false,
    "points": [{"minute": 570, "price": 111.26}]
  },
  "fuel_prices": [
    {"grade": "92#", "price": 7.80, "unit": "CNY/L"},
    {"grade": "95#", "price": 8.45, "unit": "CNY/L"}
  ],
  "fuel_region": "广东",
  "fuel_updated_at": "2026-08-15T07:00:00+08:00"
}
```

Gateway keeps provider adapters behind narrow interfaces and tests them with recorded, redacted response fixtures. Network calls are never made during unit tests. Cache keys include normalized stock symbol and fuel province.

## Wi-Fi Startup And Captive Portal

### Boot State Machine

1. Initialize NVS, board, LVGL, and the network status model.
2. If saved Wi-Fi credentials exist, start STA mode and attempt connection for 15 seconds.
3. On success, synchronize time with SNTP, resolve the configured Gateway, close any setup AP, and start snapshot polling.
4. If credentials are missing or the connection attempt fails, start AP+STA mode with open SSID `InfoDisplay` and address `192.168.4.1`.
5. Keep retrying the configured STA network in the background while the portal remains available.

The device screen shows a blocking connection sheet when setup is required. It contains `Wi-Fi 未连接`, SSID `InfoDisplay`, and `192.168.4.1`. It does not hide startup errors behind an empty dashboard.

The setup AP intentionally has no password, as requested. To limit exposure, it starts only when needed or when the user selects `重新配网` in Settings, and it shuts down immediately after a verified STA connection. Existing saved credentials are never rendered back into the web page.

### Portal Behavior

ESP-IDF supplies SoftAP, DHCP, DNS redirection for common captive-portal probes, and an HTTP server. The page is stored in firmware and works without Internet access.

The form contains:

- scanned Wi-Fi SSID selection plus manual SSID entry;
- Wi-Fi password;
- Gateway URL;
- stock symbol, default `002594.SZ`;
- fuel region, default `深圳`.

On submit, the device validates lengths and formats, attempts the new STA connection, and reports progress without discarding the active AP. Only a successful STA connection commits the new Wi-Fi credentials. Gateway reachability is reported separately and does not prevent saving working Wi-Fi credentials. Stock, fuel, and Gateway settings are stored in NVS after validation.

The portal never accepts or stores LongPort or ShowAPI secrets. Those remain Gateway environment settings.

## Device Data Client

After Wi-Fi connects, a dedicated task polls Gateway every 15 seconds with a bounded timeout. It parses JSON with ESP-IDF `json` support into an expanded `app_snapshot_t`. The stock point array has a fixed maximum of 242 entries. A single-element overwrite queue continues to decouple networking from LVGL.

The client preserves the last valid snapshot across transient request or parse failures. Each section carries `fresh`, `stale`, `error`, or `unsupported` state and an update time. UI updates occur only when relevant fields change; a network poll must not force a full-screen redraw.

The existing local Mock provider remains build-selectable for automated UI development but is not the default production provider. Production startup uses the Gateway client. If Gateway is offline, the device shows an offline/stale state rather than switching silently to Mock values.

## Eight-Tab UI

The 480-pixel top tab strip contains eight fixed 60-pixel targets. Each target stacks an LVGL symbol icon above its text label. The strip sits below the compact status bar and uses an accent underline and subtle background change for the active page.

- Tapping a tab selects the corresponding LVGL tile with animation.
- Swiping the page horizontally remains enabled.
- A page swipe updates the active tab without changing layout size.
- The bottom page-dot container is deleted, and its space is returned to content.
- The active tab never changes width, so selection cannot shift neighboring controls.

The status bar keeps current time, network state, and a notification icon with unread count. The old changing page title is removed because the active tab now communicates the current page. Required icon glyphs are added to the firmware font set and each icon retains its text label.

To preserve swipe smoothness, dynamic updates invalidate only changed labels, arcs, and chart regions. Page selection does not rebuild page objects. The animation duration is fixed and no per-second snapshot update occurs; top time updates once per minute.

## Notification Drawer

Alerts are rendered on the LVGL top layer as a notification drawer, not as a carousel page. It opens by tapping the notification icon or by a downward vertical gesture beginning in the status-bar area. A vertical-distance threshold must exceed horizontal movement so normal page swipes are not intercepted.

The drawer shows unread count and the newest alerts in severity order. Each item contains source icon, title, short message, relative time, and severity accent. It closes on upward swipe or a tap outside the drawer. `全部已读` clears the local unread state but does not delete Gateway alert history.

## Settings Integration

Settings retains theme and brightness controls and implements the previously approved persistent `画面 180°` LVGL switch. It also adds `重新配网`, which intentionally starts the `InfoDisplay` AP without erasing the current credentials. Saving a replacement network follows the same verified-connect flow as first boot.

Rotation applies immediately to both ST7796 direction and touch coordinate mapping, persists as NVS key `rotate180`, and defaults to enabled on a new device.

## Error Handling

- Wi-Fi failure opens the setup AP and displays actionable connection information.
- Gateway DNS, connection, HTTP, schema, or timeout failures retain valid cached data and mark it stale.
- Provider authentication or quota failures are logged by Gateway without exposing secrets to the client.
- A provider error with no cache renders `未配置` or `数据不可用`, never a fabricated value.
- Invalid portal input is rejected next to the field and is not persisted.
- NVS write failure leaves the previous valid configuration active.
- Oversized stock history is rejected or deterministically reduced to the fixed device limit.

## Verification

Automated verification includes:

- Gateway model and endpoint tests for the eight-page bootstrap;
- provider adapter tests for valid, missing, malformed, stale, closed-market, weekend, and credential-error responses;
- stock symbol and city-to-province normalization tests;
- firmware host/source contract tests for the eight tabs, no bottom dots, no Album page, notification drawer, NVS keys, and production Gateway provider;
- captive portal validation and state-machine tests where ESP-IDF seams permit host testing;
- a complete ESP-IDF build with size report.

Hardware verification on `/dev/cu.usbserial-01E725F0` includes:

- erase-free flashing and esptool hash verification;
- clean boot log with LCD, touch, NVS, Wi-Fi, and UI initialization;
- AP `InfoDisplay` visibility with no password when STA is unavailable;
- captive portal load, invalid-password recovery, successful configuration, AP shutdown, and reboot persistence;
- all eight tab targets, horizontal page swipes, notification pull-down/up, rotation in both orientations, and touch corners;
- live Gateway rendering when credentials are configured and explicit unavailable states when they are not.

## Out Of Scope

- Public redistribution of stock or fuel data;
- storing provider credentials on the WT32;
- real PVE, NAS, Home Assistant, weather, RSS, calendar-holiday, or quota adapters in this delivery;
- Synology photo browsing, because the Album page has been removed;
- OTA update implementation beyond retaining the existing partition layout.
