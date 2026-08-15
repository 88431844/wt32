# Screen Rotation Setting Design

## Goal

Add a persistent LVGL setting that switches the WT32-SC01 between its original
landscape orientation and the same landscape view rotated by 180 degrees. The
display and FT5x06 touch coordinates must always use the same orientation.

## User Experience

- Add a binary LVGL switch at the bottom of the existing Settings page.
- Label the control `画面 180°`, using glyphs already present in the subset font.
- Keep the six existing setting tiles unchanged.
- Reduce the brightness slider width so the brightness, theme, and rotation
  controls fit on one stable row without overlap.
- Rotation is enabled by default for devices with no saved preference.
- A switch change applies immediately without rebooting and invalidates the root
  LVGL object so the complete screen is redrawn in the new orientation.

## State And Persistence

- The board component owns the active orientation state and exposes setter and
  getter functions.
- The setting is stored as an NVS `u8` under the existing `dashboard` namespace
  with key `rotate180`.
- Settings-page initialization loads `rotate180`, defaulting to `1`, applies it
  before the first LVGL refresh, sets the switch state, and only then registers
  the user event callback.
- If the LCD direction command fails, the active state and switch remain at the
  previous value.
- If NVS persistence fails after a direction change, the UI logs the error and
  rolls the board and switch back to the previous state.

## Hardware Mapping

The original landscape mode uses ST7796 `swap_xy=true` and no mirroring. The
rotated mode keeps `swap_xy=true` and enables both X and Y mirroring.

Touch mapping follows the active board state:

```text
original: screen_x = raw_y,       screen_y = 319 - raw_x
rotated:  screen_x = 479 - raw_y, screen_y = raw_x
```

Raw coordinates are range checked before transformation and output coordinates
remain clamped to the 480 x 320 LVGL display.

## Other Dashboard Changes In This Delivery

- Remove only the standalone Time carousel page.
- Keep and update the status-bar, Weather, and Home current-time labels.
- Use a 10-page carousel beginning with Information.
- Publish the same 10-page order from Gateway bootstrap while retaining the
  `clock` data section in snapshots.

## Verification

- Source contract tests cover the 10-page order and all retained clocks.
- Source contract tests cover both LCD mirror modes, both touch transforms, the
  rotation switch, NVS key, and root invalidation.
- The Gateway test suite verifies the 10-page bootstrap and retained 11-section
  snapshot.
- A full ESP-IDF build verifies component APIs and image size.
- Flash verification requires successful esptool hash checks and a clean boot
  log showing board, touch, and 10-page UI initialization.
- Physical verification checks all four touch corners and horizontal swiping in
  both switch positions.
