# Live Data, Provisioning, And Eight-Tab UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and flash a WT32-SC01 firmware that provisions Wi-Fi through the open `InfoDisplay` AP, reads normalized live stock and fuel data from Dashboard Gateway, and presents eight equal top tabs plus a notification drawer.

**Architecture:** Dashboard Gateway owns LongPort and ShowAPI credentials, normalization, cache, and stale fallback. ESP32 owns NVS device preferences, AP+STA provisioning, a bounded Gateway HTTP client, and LVGL rendering; all cross-task updates enter LVGL through queues so no network task touches UI objects. Existing Mock providers remain explicit development fixtures and never silently replace unavailable live data.

**Tech Stack:** ESP-IDF, FreeRTOS, NVS, esp_wifi, esp_http_server, esp_http_client, lwIP, cJSON, LVGL 8.3; Python 3.11+, FastAPI, Pydantic 2, httpx, official `longport` SDK, SQLite, pytest.

---

## File Map

### Dashboard Gateway

- Modify `gateway/app/models.py`: normalized stock history, nested provider states, fuel metadata, eight-page snapshot.
- Modify `gateway/app/config.py`: provider credentials, cache path, refresh settings.
- Create `gateway/app/providers/base.py`: stock and fuel provider protocols plus provider exceptions.
- Create `gateway/app/providers/normalization.py`: stock-symbol and Chinese city/province normalization.
- Create `gateway/app/providers/longport_stock.py`: official LongPort SDK adapter.
- Create `gateway/app/providers/showapi_fuel.py`: ShowAPI HTTP adapter.
- Create `gateway/app/info_service.py`: TTL selection, SQLite last-good cache, stale/error conversion.
- Modify `gateway/app/mock_state.py`: retain non-information demo sections and accept externally built Information data.
- Modify `gateway/app/main.py`: query parameters, provider wiring, health mode, eight-page bootstrap.
- Modify `gateway/pyproject.toml`, `gateway/requirements.lock`, `gateway/requirements-test.lock`, `gateway/.env.example`, `gateway/compose.yaml`: dependencies and provider configuration.
- Create `gateway/tests/test_normalization.py`, `gateway/tests/test_longport_stock.py`, `gateway/tests/test_showapi_fuel.py`, `gateway/tests/test_info_service.py`.
- Modify `gateway/tests/test_api.py`: public contract and no-fake assertions.

### ESP32 Firmware

- Modify `components/board_wt32/include/board_wt32.h` and `components/board_wt32/board_wt32.c`: runtime orientation getter/setter.
- Create `components/app_settings/CMakeLists.txt`, `components/app_settings/include/app_settings.h`, `components/app_settings/app_settings.c`: validated NVS settings.
- Create `components/network_manager/CMakeLists.txt`, `components/network_manager/include/network_manager.h`, `components/network_manager/network_manager.c`, `components/network_manager/captive_dns.c`, `components/network_manager/portal_http.c`, `components/network_manager/portal/index.html`: AP+STA state machine and captive portal.
- Create `components/gateway_client/CMakeLists.txt`, `components/gateway_client/include/gateway_client.h`, `components/gateway_client/gateway_client.c`, `components/gateway_client/gateway_json.c`: polling, URL encoding, JSON parsing, stale state.
- Modify `components/app_model/include/app_model.h`, `components/app_model/mock_provider.c`, and `components/app_model/CMakeLists.txt`: expanded bounded model and explicit demo provider.
- Modify `components/dashboard_ui/include/dashboard_ui.h`, `components/dashboard_ui/dashboard_ui.c`, `components/dashboard_ui/CMakeLists.txt`, and font sources: eight tabs, information fields, notification drawer, connection sheet, rotation and provisioning controls.
- Modify `main/app_main.c`, `main/CMakeLists.txt`, `main/idf_component.yml`: initialize settings/network/Gateway tasks and route UI events.
- Modify `sdkconfig.defaults`: Wi-Fi, HTTP server/client, mDNS, cJSON, and buffer defaults required by production startup.
- Modify `README.md`, `gateway/README.md`: setup, provider credentials, licensing boundary, portal and flash workflow.
- Modify `tests/test_board_orientation_contract.py`, `tests/test_dashboard_ui_contract.py`.
- Create `tests/test_app_settings_contract.py`, `tests/test_network_manager_contract.py`, `tests/test_gateway_client_contract.py`.

## Task 1: Finish The Approved Rotation And Time-Page Transition

**Files:**
- Modify: `tests/test_board_orientation_contract.py`
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/board_wt32/include/board_wt32.h`
- Modify: `components/board_wt32/board_wt32.c`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Extend the orientation contract test before production edits**

Add assertions for runtime state and both mappings:

```python
def test_runtime_rotation_api_and_both_touch_mappings(self) -> None:
    header = BOARD_HEADER.read_text(encoding="utf-8")
    self.assertIn("esp_err_t wt32_board_set_rotation_180(bool enabled);", header)
    self.assertIn("bool wt32_board_get_rotation_180(void);", header)
    self.assertIn("if (s_rotation_180)", self.source)
    self.assertIn("esp_lcd_panel_mirror(s_panel, enabled, enabled)", self.source)
    self.assertIn("x = (TOUCH_NATIVE_HEIGHT - 1) - raw_y;", self.source)
    self.assertIn("x = raw_y;", self.source)
```

Extend the UI contract with NVS key `rotate180`, label `画面 180°`, and root invalidation.

- [ ] **Step 2: Run the focused tests and verify RED**

Run: `python3 -m unittest tests.test_board_orientation_contract tests.test_dashboard_ui_contract -v`

Expected: FAIL because the board runtime API and rotation switch do not exist.

- [ ] **Step 3: Implement the minimal board API and LVGL switch**

Expose:

```c
esp_err_t wt32_board_set_rotation_180(bool enabled);
bool wt32_board_get_rotation_180(void);
```

Store `s_rotation_180`, call `esp_lcd_panel_mirror(s_panel, enabled, enabled)`, and branch touch mapping between original and rotated transforms. In Settings, load `rotate180` with default `1`, apply it before registering the switch callback, persist after successful panel change, and roll back board and switch on NVS failure.

- [ ] **Step 4: Run focused tests and an ESP-IDF compile**

Run: `python3 -m unittest tests.test_board_orientation_contract tests.test_dashboard_ui_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: `Project build complete`.

- [ ] **Step 5: Commit only the rotation/time transition**

```bash
git add README.md components/board_wt32 components/dashboard_ui gateway/app/main.py gateway/tests/test_api.py tests
git commit -m "feat: finish rotation setting and remove clock page"
```

## Task 2: Define The Eight-Page And Live Information Contract

**Files:**
- Modify: `gateway/tests/test_api.py`
- Modify: `gateway/app/models.py`
- Modify: `gateway/app/main.py`
- Modify: `gateway/app/mock_state.py`

- [ ] **Step 1: Write failing API contract tests**

Assert the eight page IDs and absence of Album:

```python
expected_ids = [
    "info", "calendar", "weather", "pve",
    "nas", "antigravity", "home", "settings",
]
assert [page["id"] for page in pages] == expected_ids
assert "album" not in response.json()["data"]
assert "alerts" in response.json()["data"]
```

Add a model test constructing a closed quote with `trading_date`, OHLC, `previous_close`, `updated_at`, and two ordered minute points. Add a fuel result containing exactly `92#` and `95#`, normalized region, and upstream update time.

- [ ] **Step 2: Run the focused tests and verify RED**

Run: `cd gateway && .venv/bin/pytest tests/test_api.py -q`

Expected: FAIL because bootstrap still contains Album/Alerts pages and live fields are missing.

- [ ] **Step 3: Add normalized provider result models**

Add consistent types:

```python
class MarketPoint(BaseModel):
    minute: int = Field(ge=0, le=1439)
    price: float = Field(gt=0)

class StockQuote(BaseModel):
    symbol: str
    name: str
    currency: str = "CNY"
    market_status: Literal["preopen", "open", "lunch", "closed"]
    trading_date: date
    price: float = Field(gt=0)
    previous_close: float = Field(gt=0)
    open: float = Field(gt=0)
    close: float | None = Field(default=None, gt=0)
    high: float = Field(gt=0)
    low: float = Field(gt=0)
    change: float
    change_percent: float
    updated_at: datetime
    delayed: bool
    points: list[MarketPoint] = Field(max_length=242)

class FuelData(BaseModel):
    requested_region: str
    province: str
    updated_at: datetime
    prices: list[FuelPrice] = Field(min_length=2, max_length=2)

class ProviderResult(BaseModel, Generic[T]):
    status: DataStatus
    data: T | None = None
    error: ErrorDetail | None = None
```

Make `InfoData.stock` and `InfoData.fuel` independent `ProviderResult` values so one provider can remain live when the other is unavailable. Remove `AlbumData` from `SnapshotData`; retain `AlertsData`.

- [ ] **Step 4: Update bootstrap and Mock state without inventing live values**

Publish eight pages. Replace `_info()` fabricated quote/fuel values with `unsupported` provider results whose error codes are `stock_provider_unconfigured` and `fuel_provider_unconfigured`. Keep non-information demo sections unchanged and labeled by settings backend mode.

- [ ] **Step 5: Verify GREEN and commit**

Run: `cd gateway && .venv/bin/pytest tests/test_api.py -q`

Expected: PASS.

```bash
git add gateway/app/models.py gateway/app/main.py gateway/app/mock_state.py gateway/tests/test_api.py
git commit -m "feat(gateway): define live information contract"
```

## Task 3: Add Symbol And Fuel-Region Normalization

**Files:**
- Create: `gateway/app/providers/__init__.py`
- Create: `gateway/app/providers/base.py`
- Create: `gateway/app/providers/normalization.py`
- Create: `gateway/app/providers/china_regions.json`
- Create: `gateway/tests/test_normalization.py`

- [ ] **Step 1: Write normalization tests**

```python
@pytest.mark.parametrize(("raw", "expected"), [
    ("002594", "002594.SZ"),
    ("002594.sz", "002594.SZ"),
    ("600519", "600519.SH"),
])
def test_normalize_a_share_symbol(raw: str, expected: str) -> None:
    assert normalize_stock_symbol(raw) == expected

def test_shenzhen_normalizes_to_guangdong() -> None:
    assert normalize_fuel_region("深圳") == ("深圳", "广东")
```

Also reject blank, unsafe, and unsupported stock strings and unknown fuel locations.

- [ ] **Step 2: Run and verify RED**

Run: `cd gateway && .venv/bin/pytest tests/test_normalization.py -q`

Expected: collection error because the normalization module is absent.

- [ ] **Step 3: Implement protocols, exceptions, and deterministic normalization**

Define:

```python
class ProviderUnavailable(RuntimeError):
    code: str
    retryable: bool

class StockProvider(Protocol):
    def fetch(self, symbol: str, now: datetime) -> StockQuote: ...

class FuelProvider(Protocol):
    def fetch(self, requested_region: str, province: str, now: datetime) -> FuelData: ...
```

Normalize six-digit Shenzhen prefixes `000`, `001`, `002`, `003`, `300`, and `301` to `.SZ`; Shanghai prefixes `600`, `601`, `603`, `605`, `688`, and `689` to `.SH`. Use a checked-in UTF-8 JSON mapping for province and prefecture-level names and strip only common suffixes such as `市` and `省`.

- [ ] **Step 4: Verify GREEN and commit**

Run: `cd gateway && .venv/bin/pytest tests/test_normalization.py -q`

Expected: PASS.

```bash
git add gateway/app/providers gateway/tests/test_normalization.py
git commit -m "feat(gateway): normalize stock and fuel regions"
```

## Task 4: Implement The LongPort Stock Adapter

**Files:**
- Create: `gateway/app/providers/longport_stock.py`
- Create: `gateway/tests/test_longport_stock.py`
- Modify: `gateway/pyproject.toml`
- Modify: `gateway/requirements.lock`
- Modify: `gateway/requirements-test.lock`

- [ ] **Step 1: Write failing adapter tests around a fake SDK context**

Use a fake returning quote and minute bar objects. Assert:

```python
quote = provider.fetch("002594.SZ", datetime(2026, 8, 14, 15, 10, tzinfo=SHANGHAI))
assert quote.symbol == "002594.SZ"
assert quote.market_status == "closed"
assert quote.open == 111.26
assert quote.close == 112.80
assert quote.change_percent == pytest.approx(1.42, abs=0.01)
assert [point.minute for point in quote.points] == sorted(point.minute for point in quote.points)
assert len(quote.points) <= 242
```

Add cases for an open session, lunch break, weekend selecting the last trading date, empty bars, and SDK authentication errors.

- [ ] **Step 2: Run and verify RED**

Run: `cd gateway && .venv/bin/pytest tests/test_longport_stock.py -q`

Expected: collection error because the adapter is absent.

- [ ] **Step 3: Implement the adapter behind an injected SDK seam**

Create `LongPortStockProvider(context_factory, timezone)`; use `from longport.openapi import Config, QuoteContext, Period, AdjustType` only inside the default factory. Pull quote plus up to 1000 one-minute candlesticks, select the latest relevant Asia/Shanghai trading date, sort and deduplicate minutes, and compute change against `prev_close` with decimal-safe intermediate arithmetic.

Map provider failures to `ProviderUnavailable(code="stock_authentication_failed", retryable=False)` or `ProviderUnavailable(code="stock_upstream_unavailable", retryable=True)` without including credential text.

- [ ] **Step 4: Lock dependencies and verify GREEN**

Add runtime `longport` and `httpx` ranges to `pyproject.toml`, install into `.venv`, and regenerate both lock files using the repository's existing plain requirement format.

Run: `cd gateway && .venv/bin/pytest tests/test_longport_stock.py -q`

Expected: PASS without network access.

- [ ] **Step 5: Commit**

```bash
git add gateway/app/providers/longport_stock.py gateway/tests/test_longport_stock.py gateway/pyproject.toml gateway/requirements*.lock
git commit -m "feat(gateway): add LongPort stock provider"
```

## Task 5: Implement The ShowAPI Fuel Adapter

**Files:**
- Create: `gateway/app/providers/showapi_fuel.py`
- Create: `gateway/tests/test_showapi_fuel.py`

- [ ] **Step 1: Write failing response-mapping tests**

Use `httpx.MockTransport` for an HTTP 200 fixture containing `ret_code=0`, `prov=广东`, `p92`, `p95`, and `ct`. Assert only two grades are returned, numeric strings are parsed, and the source time is preserved. Add cases for empty 95, nonzero `ret_code`, malformed JSON, timeout, and HTTP 429.

- [ ] **Step 2: Run and verify RED**

Run: `cd gateway && .venv/bin/pytest tests/test_showapi_fuel.py -q`

Expected: collection error because the adapter is absent.

- [ ] **Step 3: Implement strict HTTPS mapping**

Create `ShowApiFuelProvider(app_key, client, timezone)` and send a form POST to `https://route.showapi.com/138-46` with `appKey` and normalized province. Parse prices through `Decimal`, reject non-positive/missing values, construct exactly `92#` and `95#`, and convert `ct` to an aware Asia/Shanghai datetime.

- [ ] **Step 4: Verify GREEN and commit**

Run: `cd gateway && .venv/bin/pytest tests/test_showapi_fuel.py -q`

Expected: PASS.

```bash
git add gateway/app/providers/showapi_fuel.py gateway/tests/test_showapi_fuel.py
git commit -m "feat(gateway): add ShowAPI fuel provider"
```

## Task 6: Add Last-Good Cache And Wire Live Gateway Data

**Files:**
- Create: `gateway/app/info_service.py`
- Create: `gateway/tests/test_info_service.py`
- Modify: `gateway/app/config.py`
- Modify: `gateway/app/main.py`
- Modify: `gateway/app/mock_state.py`
- Modify: `gateway/.env.example`
- Modify: `gateway/compose.yaml`
- Modify: `gateway/tests/test_api.py`

- [ ] **Step 1: Write failing cache and integration tests**

Test these transitions with fake providers and a temporary SQLite path:

```python
first = service.get_info("002594.SZ", "深圳", now)
assert first.stock.status == DataStatus.fresh

stock_provider.error = ProviderUnavailable("stock_upstream_unavailable", retryable=True)
second = service.get_info("002594.SZ", "深圳", now + timedelta(seconds=31))
assert second.stock.status == DataStatus.stale
assert second.stock.data == first.stock.data
```

Also assert an unconfigured provider with no cache returns `unsupported`, cache survives service reconstruction, fuel is not refreshed before the next 07:10 boundary, and `/v1/snapshot` accepts and normalizes query inputs.

- [ ] **Step 2: Run and verify RED**

Run: `cd gateway && .venv/bin/pytest tests/test_info_service.py tests/test_api.py -q`

Expected: FAIL because the service and query integration are absent.

- [ ] **Step 3: Implement SQLite cache and refresh rules**

Use one table keyed by `(kind, normalized_key)` with `updated_at`, `status`, and validated JSON payload. Stock TTL is 15 seconds during trading and 15 minutes when closed; fuel refreshes after 07:10 Asia/Shanghai once the stored upstream date is older than the current source date. A failed refresh returns validated cached data as `stale`; corrupt cache rows are deleted and treated as missing.

- [ ] **Step 4: Wire configuration and endpoint**

Extend `Settings` with optional LongPort credentials, optional ShowAPI key, and `GATEWAY_CACHE_PATH=/data/gateway.sqlite3`. Construct configured adapters at startup. Update:

```python
@app.get("/v1/snapshot", response_model=ApiEnvelope[SnapshotData])
def snapshot(symbol: str = "002594.SZ", fuel_region: str = "深圳") -> ApiEnvelope[SnapshotData]:
    info = info_service.get_info(symbol, fuel_region, state.now())
    revision, data = state.snapshot_with_revision(info=info)
    return envelope(data, revision=revision)
```

Mount a persistent `/data` volume in Compose and document env names without values.

- [ ] **Step 5: Verify all Gateway tests and commit**

Run: `cd gateway && .venv/bin/pytest -q`

Expected: all tests PASS and no real network request occurs.

```bash
git add gateway/app gateway/tests gateway/.env.example gateway/compose.yaml
git commit -m "feat(gateway): serve cached live information data"
```

## Task 7: Add Validated Persistent Device Settings

**Files:**
- Create: `components/app_settings/CMakeLists.txt`
- Create: `components/app_settings/include/app_settings.h`
- Create: `components/app_settings/app_settings.c`
- Create: `tests/test_app_settings_contract.py`

- [ ] **Step 1: Write a failing settings contract test**

Assert an `app_settings_t` with fixed bounds and defaults:

```c
#define APP_WIFI_SSID_MAX 32
#define APP_WIFI_PASSWORD_MAX 64
#define APP_GATEWAY_URL_MAX 128
#define APP_STOCK_SYMBOL_MAX 16
#define APP_FUEL_REGION_MAX 24

typedef struct {
    char wifi_ssid[APP_WIFI_SSID_MAX + 1];
    char wifi_password[APP_WIFI_PASSWORD_MAX + 1];
    char gateway_url[APP_GATEWAY_URL_MAX + 1];
    char stock_symbol[APP_STOCK_SYMBOL_MAX + 1];
    char fuel_region[APP_FUEL_REGION_MAX + 1];
    bool rotate_180;
} app_settings_t;
```

The Python contract also asserts namespace `dashboard`, default URL `http://dashboard-gateway.local:8080`, symbol `002594.SZ`, fuel region `深圳`, and that logging never prints `wifi_password`.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_app_settings_contract -v`

Expected: FAIL because the component is absent.

- [ ] **Step 3: Implement load, validate, and atomic save**

Expose:

```c
esp_err_t app_settings_load(app_settings_t *out);
esp_err_t app_settings_validate(const app_settings_t *candidate,
                                app_settings_field_t *invalid_field);
esp_err_t app_settings_save(const app_settings_t *settings);
bool app_settings_has_wifi(const app_settings_t *settings);
```

Validate UTF-8 byte lengths, `http://` or `https://` Gateway scheme, six-digit or suffixed stock symbol, nonempty fuel region, and password length 0 or 8-64. Write candidate keys then a monotonically increasing `config_rev`; a failed commit leaves the caller's active settings unchanged.

- [ ] **Step 4: Verify and commit**

Run: `python3 -m unittest tests.test_app_settings_contract -v`

Expected: PASS.

```bash
git add components/app_settings tests/test_app_settings_contract.py
git commit -m "feat(firmware): persist validated dashboard settings"
```

## Task 8: Implement AP+STA Provisioning And Captive Portal

**Files:**
- Create: `components/network_manager/CMakeLists.txt`
- Create: `components/network_manager/include/network_manager.h`
- Create: `components/network_manager/network_manager.c`
- Create: `components/network_manager/captive_dns.c`
- Create: `components/network_manager/portal_http.c`
- Create: `components/network_manager/portal/index.html`
- Create: `tests/test_network_manager_contract.py`

- [ ] **Step 1: Write failing state-machine and portal contract tests**

Assert constants `InfoDisplay`, `192.168.4.1`, open auth mode, 15-second initial timeout, and endpoints:

```text
GET  /api/config
GET  /api/networks
POST /api/config
GET  /generate_204
GET  /hotspot-detect.html
```

Assert portal HTML has fields `ssid`, `password`, `gateway_url`, `stock_symbol`, and `fuel_region`, but never embeds a saved password or third-party credential field.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_network_manager_contract -v`

Expected: FAIL because the component is absent.

- [ ] **Step 3: Implement the public queue/callback boundary**

Expose:

```c
typedef enum {
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_PROVISIONING,
    NETWORK_STATE_FAILED,
} network_state_t;

typedef void (*network_event_cb_t)(network_state_t state,
                                   const app_settings_t *active,
                                   void *context);

esp_err_t network_manager_start(const app_settings_t *initial,
                                network_event_cb_t callback,
                                void *context);
esp_err_t network_manager_request_provisioning(void);
```

Event handlers update internal state and enqueue work; they do not call LVGL.

- [ ] **Step 4: Implement STA timeout and open setup AP**

Initialize netif/event loop once. Attempt saved STA for 15 seconds. On no credentials or failure, enter AP+STA with SSID `InfoDisplay`, `WIFI_AUTH_OPEN`, DHCP address `192.168.4.1`, and background STA retries. Shut down AP only after `IP_EVENT_STA_GOT_IP` for the candidate network.

- [ ] **Step 5: Implement DNS and HTTP portal**

Answer A-record DNS queries with `192.168.4.1`. Serve embedded HTML and captive-probe redirects. JSON POST validation calls `app_settings_validate`, attempts candidate Wi-Fi without overwriting the active struct, saves only after connection succeeds, and reports Gateway reachability separately.

- [ ] **Step 6: Verify contracts, build, and commit**

Run: `python3 -m unittest tests.test_network_manager_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: component compiles with no undefined Wi-Fi, HTTP, or lwIP symbols.

```bash
git add components/network_manager tests/test_network_manager_contract.py
git commit -m "feat(firmware): add InfoDisplay Wi-Fi provisioning"
```

## Task 9: Expand The Firmware Model And Parse Gateway JSON

**Files:**
- Modify: `components/app_model/include/app_model.h`
- Modify: `components/app_model/mock_provider.c`
- Modify: `components/app_model/CMakeLists.txt`
- Create: `components/gateway_client/CMakeLists.txt`
- Create: `components/gateway_client/include/gateway_client.h`
- Create: `components/gateway_client/gateway_client.c`
- Create: `components/gateway_client/gateway_json.c`
- Create: `tests/test_gateway_client_contract.py`

- [ ] **Step 1: Write failing bounded-model and parser contract tests**

Require:

```c
#define APP_MARKET_POINT_MAX 242

typedef enum {
    APP_DATA_FRESH,
    APP_DATA_STALE,
    APP_DATA_ERROR,
    APP_DATA_UNSUPPORTED,
} app_data_status_t;

typedef struct {
    uint16_t minute;
    float price;
} app_market_point_t;
```

Assert `app_snapshot_t` carries stock/fuel status, symbol/name, OHLC, market state, point count, 92/95 prices, province, both update strings, Wi-Fi state, and alert items. Assert production main does not call `app_model_start_mock_provider()`.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_gateway_client_contract -v`

Expected: FAIL because types and Gateway client are absent.

- [ ] **Step 3: Expand the fixed-size model**

Use bounded UTF-8 arrays, `size_t market_point_count`, and at most three compact notification items. Keep current weather/PVE/NAS/Home fields so unchanged pages remain functional. Make Mock start explicit as `app_model_start_demo_provider()` and mark every generated section `APP_DATA_UNSUPPORTED` or demo-source instead of live.

- [ ] **Step 4: Implement strict JSON parsing**

Expose a pure parse seam:

```c
esp_err_t gateway_json_parse_snapshot(const char *json, size_t length,
                                      app_snapshot_t *out);
```

Require schema version `1.0`, reject type mismatches and more than 242 points, validate sorted minute bounds and positive finite prices, and update `out` only after the complete document validates.

- [ ] **Step 5: Implement bounded polling**

Expose:

```c
esp_err_t gateway_client_start(QueueHandle_t ui_events,
                               const app_settings_t *settings);
esp_err_t gateway_client_update_settings(const app_settings_t *settings);
```

URL-encode symbol/region, use a 5-second HTTP timeout and a capped 48 KiB response buffer in PSRAM, poll every 15 seconds while connected, retain last valid model as stale on error, and never log response bodies or credentials.

- [ ] **Step 6: Verify, build, and commit**

Run: `python3 -m unittest tests.test_gateway_client_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: build succeeds and application fits both OTA slots.

```bash
git add components/app_model components/gateway_client tests/test_gateway_client_contract.py
git commit -m "feat(firmware): consume normalized Gateway snapshots"
```

## Task 10: Replace Dots With Eight Equal Icon Tabs

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`
- Modify: `components/dashboard_ui/fonts/app_font_14.c`
- Modify: `components/dashboard_ui/fonts/app_font_18.c`

- [ ] **Step 1: Rewrite the UI contract for the approved layout**

Assert `PAGE_COUNT 8`, exact page builders, no `create_gallery_page`, no standalone `create_alert_page`, no `dots`, no bottom `page_bar`, and a tab width of `WT32_LCD_WIDTH / PAGE_COUNT`. Assert each tab has a separate LVGL symbol label and Chinese text label and its click handler selects the matching tile with `LV_ANIM_ON`.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: FAIL because the ten-page dot navigation remains.

- [ ] **Step 3: Implement stable top geometry**

Use a 26-pixel status bar, 44-pixel tab strip, and 250-pixel tileview. Create eight fixed 60-pixel buttons. Use `LV_SYMBOL_LIST`, `LV_SYMBOL_CALENDAR`, a weather symbol, a chart/server symbol, `LV_SYMBOL_DRIVE`, `LV_SYMBOL_BATTERY_FULL`, `LV_SYMBOL_HOME`, and `LV_SYMBOL_SETTINGS`; pair each with text `资讯`, `日历`, `天气`, `PVE`, `NAS`, `额度`, `家居`, `设置`.

The active state changes color and a 3-pixel underline only. `update_navigation()` changes tab state without resizing objects. Remove the changing status title because the selected tab identifies the page.

- [ ] **Step 4: Preserve swipe behavior and deferred updates**

Keep tileview horizontal snap and the existing scroll-in-progress snapshot deferral. Tab clicks animate to the page; tile `LV_EVENT_VALUE_CHANGED` updates the tab. Do not reconstruct pages or invalidate the whole screen.

- [ ] **Step 5: Verify, build, and commit**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: PASS.

```bash
git add components/dashboard_ui tests/test_dashboard_ui_contract.py
git commit -m "feat(ui): add eight equal top tabs"
```

## Task 11: Render Real Stock And Fuel States

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Add failing real-information UI assertions**

Require persistent objects for name/symbol, market state, price/change, open/close, chart, 92/95 prices, province, fuel update time, and provider-state labels. Assert the chart is populated with `lv_chart_set_point_count` plus `lv_chart_set_ext_y_array` or a deterministic full-series loop, not `lv_chart_set_next_value` on each snapshot.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: FAIL because only two BYD fields and a generated rolling chart exist.

- [ ] **Step 3: Rebuild the Information page to the approved composition**

Use the left region for quote, OHLC, market status, and full-day chart. Use the right region for two fuel cards and update metadata. During `fresh` or `stale`, load all returned chart points and derive the Y range from min/max with padding. During `unsupported` or `error`, hide numeric fields and show `行情未配置` or `油价未配置`. Stale data shows its original update time and a muted `已缓存` label.

- [ ] **Step 4: Verify differential updates and commit**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: PASS with no new full-screen invalidation.

```bash
git add components/dashboard_ui tests/test_dashboard_ui_contract.py
git commit -m "feat(ui): render live market and fuel data"
```

## Task 12: Add Notification Drawer And Provisioning Controls

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/include/dashboard_ui.h`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Add failing notification and network-state contracts**

Assert the header exposes:

```c
typedef struct {
    void (*request_provisioning)(void *context);
    void *context;
} dashboard_ui_callbacks_t;

esp_err_t dashboard_ui_create(const dashboard_ui_callbacks_t *callbacks);
void dashboard_ui_set_network_state(network_state_t state);
```

Require a notification button/badge, `lv_layer_top()` drawer, `全部已读`, top-edge vertical gesture threshold, `重新配网`, and the connection sheet strings `Wi-Fi 未连接`, `InfoDisplay`, and `192.168.4.1`.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: FAIL because those controls are absent.

- [ ] **Step 3: Implement queue-safe callbacks and connection sheet**

Store callbacks by value. Settings `重新配网` calls only the supplied enqueue callback. `dashboard_ui_set_network_state` runs on the LVGL task and creates or removes one reusable overlay; it never blocks while Wi-Fi connects.

- [ ] **Step 4: Implement notification drawer**

Build on `lv_layer_top()` with a dim mask and a 282-pixel drawer. Open on bell click or a downward gesture that begins at `y <= 30` and satisfies `delta_y >= 36 && delta_y > abs(delta_x)`. Close on upward gesture or mask click. Render at most three newest active alerts and update a fixed-size badge. `全部已读` sets local read revision and hides the badge without deleting model data.

- [ ] **Step 5: Verify, build, and commit**

Run: `python3 -m unittest tests.test_dashboard_ui_contract -v`

Expected: PASS.

Run: `idf.py build`

Expected: PASS.

```bash
git add components/dashboard_ui tests/test_dashboard_ui_contract.py
git commit -m "feat(ui): add notifications and provisioning controls"
```

## Task 13: Wire Production Startup And Minute-Level UI Updates

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`
- Modify: `tests/test_gateway_client_contract.py`

- [ ] **Step 1: Add failing orchestration assertions**

Assert initialization order NVS -> board -> LVGL/UI -> settings -> network, use of an `app_ui_event_t` queue, SNTP after IP, Gateway client start after connection, and no production Mock provider call. Assert network callbacks only enqueue events.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest tests.test_gateway_client_contract -v`

Expected: FAIL because `app_main` starts the Mock provider directly.

- [ ] **Step 3: Introduce typed UI events**

Define:

```c
typedef enum {
    APP_UI_EVENT_SNAPSHOT,
    APP_UI_EVENT_NETWORK_STATE,
} app_ui_event_type_t;

typedef struct {
    app_ui_event_type_t type;
    union {
        app_snapshot_t snapshot;
        network_state_t network_state;
    } data;
} app_ui_event_t;
```

The UI task is the only consumer and the only task calling dashboard UI functions. Snapshot overwrite behavior remains bounded; network state transitions are not lost.

- [ ] **Step 4: Wire startup and settings changes**

Start UI with an initial provisioning/offline event, then network manager. On `CONNECTED`, start SNTP and Gateway polling with the current settings. On a successful portal save, update Gateway client settings atomically. The Settings callback sends a provisioning command to network manager.

Update component dependencies for `esp_wifi`, `esp_event`, `esp_netif`, `esp_http_server`, `esp_http_client`, `lwip`, `json`, and `mdns` only where used.

- [ ] **Step 5: Verify tests, build, size, and commit**

Run: `python3 -m unittest discover -s tests -v`

Expected: all host contracts PASS.

Run: `idf.py build size`

Expected: build PASS and app image remains below the 0x1d0000 OTA slot size.

```bash
git add main sdkconfig.defaults tests/test_gateway_client_contract.py
git commit -m "feat(firmware): wire production network startup"
```

## Task 14: Document Deployment And Start Gateway

**Files:**
- Modify: `README.md`
- Modify: `gateway/README.md`
- Modify: `gateway/.env.example`
- Modify: `gateway/launchd/com.wt32.dashboard-gateway.plist.in`

- [ ] **Step 1: Add documentation verification assertions**

Extend a contract test to require documentation of `InfoDisplay`, `192.168.4.1`, `LONGPORT_APP_KEY`, `LONGPORT_APP_SECRET`, `LONGPORT_ACCESS_TOKEN`, `SHOWAPI_APP_KEY`, provider licensing, and explicit unavailable behavior.

- [ ] **Step 2: Run and verify RED**

Run: `python3 -m unittest discover -s tests -v`

Expected: FAIL on missing deployment text.

- [ ] **Step 3: Update deployment files**

Document creating `gateway/.env`, keeping it untracked, starting with Compose or the local launchd service, setting the device Gateway URL, and distinguishing real Information data from remaining demo integrations. Ensure launchd passes only paths/env-file references and no literal secrets.

- [ ] **Step 4: Verify Gateway runtime**

Run: `cd gateway && .venv/bin/pytest -q`

Expected: all Gateway tests PASS.

Run: `cd gateway && .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8080`

In a second shell run: `curl --fail http://127.0.0.1:8080/healthz`

Expected: HTTP 200 envelope. Without provider credentials, stock/fuel are explicitly unsupported; with credentials, they are fresh or stale and contain no Mock values.

Stop the Uvicorn session after the response is verified; do not leave an untracked test server running.

- [ ] **Step 5: Commit**

```bash
git add README.md gateway/README.md gateway/.env.example gateway/launchd
git commit -m "docs: describe live providers and device provisioning"
```

## Task 15: Full Verification, Flash, And Physical Smoke Test

**Files:**
- Modify only when a failing verification identifies a root cause in an in-scope file.

- [ ] **Step 1: Run the complete automated suite from a clean command context**

Run: `python3 -m unittest discover -s tests -v`

Expected: all firmware host contracts PASS.

Run: `cd gateway && .venv/bin/pytest -q`

Expected: all Gateway tests PASS.

Run: `idf.py fullclean build size`

Expected: build PASS, no warnings introduced by this work, and image below the OTA partition limit.

- [ ] **Step 2: Confirm the exact serial target**

Run: `ls -l /dev/cu.usbserial-01E725F0`

Expected: the configured WT32 serial device exists. If it does not, list `/dev/cu.usbserial-*` read-only and select the single matching connected device; do not guess when multiple devices are present.

- [ ] **Step 3: Flash without erasing NVS and verify hashes**

Run: `idf.py -p /dev/cu.usbserial-01E725F0 flash`

Expected: every written partition reports `Hash of data verified` and the command exits 0. Do not run `erase-flash`, because existing settings are user data.

- [ ] **Step 4: Capture a bounded boot log**

Run: `idf.py -p /dev/cu.usbserial-01E725F0 monitor`

Expected within 30 seconds: board ready, touch ready, UI created with 8 pages, settings loaded, and either STA connected plus Gateway polling or `InfoDisplay` provisioning AP started. Exit monitor with its documented quit sequence; do not leave the session running.

- [ ] **Step 5: Perform physical behavior checks**

Verify all eight tab taps, horizontal swipes, top notification pull-down/up, 180-degree rotation in both modes, four touch corners, and no bottom dots. With unavailable Wi-Fi, connect a phone/computer to open AP `InfoDisplay` and load `http://192.168.4.1`. Hand control to the user for Wi-Fi password entry; after they submit, confirm invalid-password recovery or successful AP shutdown and reboot persistence without reading or logging the password.

- [ ] **Step 6: Verify real-data honesty**

With no Gateway provider credentials, confirm Information shows `行情未配置` and `油价未配置`. With configured credentials, confirm BYD symbol/default name, quote source time, open/close, a real complete latest-day line after close, only 92/95 Guangdong prices, and upstream fuel update time.

- [ ] **Step 7: Review and publish**

Run: `git status --short --branch`

Expected: only explicitly understood user-local files remain uncommitted.

Run: `git log --oneline --decorate -12`

Review each task commit, then push the non-force update:

```bash
git push origin main
```

Expected: remote fast-forwards; never use `--force`.
