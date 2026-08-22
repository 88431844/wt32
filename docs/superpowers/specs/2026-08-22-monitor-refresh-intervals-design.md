# Monitor Refresh Intervals Design

## Goal

Change the NAS and PVE monitor refresh choices to 5, 10, 30, and 60 seconds, with 5 seconds as the default.

## Scope

- Keep the existing active-page polling behavior: only the visible NAS or PVE page performs provider requests.
- Show exactly four refresh controls in Settings: `5S`, `10S`, `30S`, and `60S`.
- Use 5 seconds when no refresh preference has been saved.
- Preserve saved values of 10, 30, or 60 seconds.
- Treat the formerly supported 120-second value, and every other unsupported value, as invalid and fall back to 5 seconds at startup.
- Do not expose a 1-second option.

## Rationale

A complete PVE refresh performs at least two HTTPS requests and can perform an additional request for every running guest. A complete Synology refresh performs multiple sequential SNMP requests, each with a bounded timeout. A 1-second interval therefore cannot reliably complete under slow or failing network conditions. Five seconds is the supported lower bound while retaining responsive monitoring.

## Implementation

The dashboard UI and live provider will share the same strict set of supported values through equivalent local validation:

- The UI initializes its refresh selection to 5 seconds, renders four buttons, accepts persisted values only when they are 5, 10, 30, or 60, and otherwise selects 5 seconds.
- The provider uses a 5-second default and accepts refresh commands or persisted values only when they are 5, 10, 30, or 60.
- Selecting a refresh button persists the value and immediately wakes the provider using the existing control API.

The scheduler remains single-threaded. Refresh duration is subtracted from the selected interval. If one collection takes longer than its interval, the next collection starts without an additional delay, but requests never overlap or queue concurrently.

## Error Handling And Compatibility

No NVS migration write is required. Unsupported persisted values are ignored in memory and resolve to the 5-second default. A subsequent user selection writes a supported value normally. Existing provider timeout and stale-data behavior remains unchanged.

## Testing

Contract tests will verify:

- Settings exposes exactly `5S`, `10S`, `30S`, and `60S`.
- UI and provider defaults are both 5 seconds.
- UI and provider validation accept the same four values.
- The old 120-second option is absent.
- Existing active-page, persistence, immediate-wakeup, and non-overlapping scheduling behavior remains present.

