# Compact Monitor Themes And Active-Page Refresh Design

## Goal

Refine the 480 x 320 WT32-SC01 monitor UI for denser NAS and PVE status,
eliminate missing-glyph boxes, add five persistent color themes, and reduce
monitor traffic by polling only the visible monitor page.

## Navigation And Refresh

The top bar remains 32 pixels high but renders time and station IP on one line:

```text
NAS | 00:09 | 192.168.31.22 | PVE | Settings
```

PVE and NAS are never polled together on a timer. Selecting NAS or PVE changes
the active monitor and wakes the provider immediately. Selecting Settings pauses
monitor network requests. Returning to a monitor page triggers an immediate
request before the configured interval starts.

Settings offers fixed intervals of 10, 30, 60, and 120 seconds. The persisted
default is 10 seconds. The top-bar clock is updated locally so long intervals or
the Settings page do not make the displayed time stale.

## Themes

Settings offers five palettes:

1. Deep Ocean: dark teal with cyan and blue status accents.
2. High Contrast: black, white, yellow, green, and red.
3. Mist Gray: light gray and white with blue and green accents.
4. Graphite Green: graphite surfaces with green and blue accents.
5. Charcoal Coral: charcoal surfaces with coral, mint, and blue accents.

Graphite Green is the default when no theme setting exists. The theme index is
stored in NVS and applied before the first page is shown. Theme changes update
shared LVGL styles and all current dynamic status colors without recreating the
screen.

## NAS Page

Four storage rows keep their current capacity values and used/free bars. The
middle text no longer claims a RAID type that is not available. It shows the
best SNMP-native volume description, path, or mapped health state. Synology's
standard SNMP MIB does not expose the custom Storage Manager description shown
in DSM; obtaining that value would require a separate authenticated DSM WebAPI
integration and is outside this increment.

The bottom metrics use a single text-height row, approximately 24 pixels high:

```text
CPU 2% | Memory 92% | Temp 57 C | Up 10M/s | Down 1K/s
```

Network rates come from 64-bit IF-MIB octet counters. The provider selects an
active non-loopback interface, stores the prior sample and timestamp, and
computes bytes per second from counter deltas. The first sample, counter reset,
interface change, or failed query displays `--` until two valid samples exist.
Rates are clamped against impossible time deltas and formatted as B/s, K/s,
M/s, or G/s.

## PVE Page

The summary remains compact. There is enough visual width for another small
metric, but the standard PVE node status API does not expose CPU temperature.
The firmware therefore does not display a temperature placeholder or fabricated
value. A future temperature field requires an explicit source such as a custom
PVE-side service or another configured monitoring protocol.

Each VM row renders one line:

```text
100 fnos  192.168.31.138  3%  6.2G/16G  100G/512G
```

The name and IP columns have bounded widths; usage columns remain right aligned.
Arrow controls become narrower so the list receives more horizontal space.
For running QEMU guests, IPv4 comes from the existing QEMU Guest Agent path by
requesting `network-get-interfaces` instead of a separate ping. Loopback,
link-local, and IPv6 addresses are ignored. Guests without a running agent or a
usable IPv4 show `--`. LXC addresses are queried only when the supported PVE
interfaces endpoint responds; otherwise they also show `--`. A failed per-guest
IP request does not fail the node snapshot.

## Glyph Handling

Static UI text must contain no glyph outside the generated subset fonts. Status
dots are LVGL circle objects instead of Unicode bullets. Arrow controls use
ASCII where practical or explicitly included arrow glyphs. All new Chinese
labels are included when the 14- and 18-pixel subset fonts are regenerated.

Scanned SSIDs are dynamic and cannot be covered by a fixed CJK subset within the
firmware partition budget. Unsupported UTF-8 characters are replaced with a
single readable `?` per code point while preserving ASCII, so an SSID remains
selectable without square glyphs or broken UTF-8.

## Persistence And Error Behavior

Theme and refresh interval use checked NVS keys no longer than 15 characters.
Invalid persisted values fall back to Graphite Green and 10 seconds. Changing
either setting is non-destructive to Wi-Fi, PVE, NAS, rotation, brightness, and
credential settings.

The provider preserves the last successful monitor data after a transient
request failure, marks it stale, and keeps the source-specific error. Optional
network-rate, description, and guest-IP failures do not turn an otherwise valid
NAS or PVE snapshot offline.

## Verification And Flashing

Contract tests cover palette definitions, Graphite Green default, all four
refresh intervals, active-page-only polling, immediate page-switch refresh,
compact NAS metrics, IF-MIB Counter64 parsing and delta rules, VM IPv4 parsing,
missing-data fallbacks, and the absence of unsupported bullet glyphs.

Verification includes focused unit tests, the complete Python test suite, an
ESP-IDF 4.4.8 build, application partition-size checking, and an erase-free
flash to the known WT32 serial device. NVS is not erased, so existing Wi-Fi and
monitor credentials remain intact. Serial output must confirm a clean boot,
display/touch initialization, the selected active monitor, and successful or
clearly diagnosed PVE/NAS requests.

## Out Of Scope

- An authenticated DSM WebAPI client for custom Storage Manager descriptions.
- A custom PVE-side CPU temperature service.
- Guessing an IP from guest names, DHCP state, ARP tables, or configured CIDRs.
- Polling hidden monitor pages in the background.
