# WT32-SC01 Dashboard

WT32-SC01 V3.2 dashboard firmware for the classic ESP32-WROVER-B board. The
current firmware is an interaction prototype: LVGL renders local mock data while
the display, touch panel, page navigation, controls, memory use, and update flow
are validated on the real board.

## Supported Hardware

The tested/default board profile is:

- ESP32-WROVER-B, 4 MB Flash, 8 MB physical PSRAM
- 3.5-inch ST7796 LCD, native 320x480, used as a 480x320 landscape display
- FT5x06/FT6336U-compatible touch controller at I2C address `0x38`
- CP2104 USB-to-UART bridge

| Function | GPIO |
| --- | ---: |
| LCD MOSI | 13 |
| LCD SCLK | 14 |
| LCD CS | 15 |
| LCD DC | 21 |
| LCD reset | 22 |
| Backlight PWM | 23 |
| Touch SDA | 18 |
| Touch SCL | 19 |

Some sellers document an `ILI9488 + GSLX680` batch under the same product name.
That batch is not supported by this firmware. The LCD has no MISO connection, so
its controller cannot be identified reliably in software. Use the hardware probe
before flashing the dashboard if the board batch is uncertain.

## Toolchain

This hardware-validation branch deliberately targets **ESP-IDF v4.4.8** and
**LVGL v8.3.11**. The version matches the installed toolchain and the original
WT32 reference project. Do not regenerate the project with ESP-IDF 5.x yet; that
migration also requires updating the I2C, LCD, and LVGL integration APIs.

Install ESP-IDF v4.4.8, then activate it in every new terminal:

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py --version
```

The second command should report `ESP-IDF v4.4.8` (a local commit suffix is
acceptable).

## Build

From the repository root:

```bash
idf.py set-target esp32
idf.py build
idf.py size
```

`sdkconfig.defaults` is applied when a new `sdkconfig` is generated. It does not
overwrite an existing `sdkconfig`. After changing configuration, inspect it with:

```bash
idf.py menuconfig
```

Important settings are:

- target `esp32`, 4 MB Flash, DIO mode, 40 MHz Flash frequency
- custom partition table `partitions.csv`
- 240 MHz CPU
- external RAM enabled, auto-detected, 40 MHz, exposed through `malloc()`
- 64 KB internal-memory reserve for DMA and task stacks
- LVGL RGB565 with 16-bit byte swap and standard `malloc/free`
- size optimization and unused LVGL examples disabled

The 40 MHz PSRAM setting is intentional for the first hardware baseline. The
classic ESP32 can map at most about 4 MB of PSRAM into its normal address space;
the remaining physical memory would require the `esp_himem` bank-switch API and
cannot be used as ordinary LVGL memory.

## Back Up Existing Firmware

Create a complete backup before the first flash. A full image may contain Wi-Fi
credentials or other secrets, so `backups/*.bin` is ignored by Git.

Find the serial port on macOS:

```bash
ls /dev/cu.usbserial-*
```

Set the returned port and read all 4 MB:

```bash
export WT32_PORT=/dev/cu.usbserial-01E725F0
mkdir -p backups
esptool.py --chip esp32 --port "$WT32_PORT" --baud 460800 \
  read_flash 0x0 0x400000 backups/wt32-original.bin
ls -lh backups/wt32-original.bin
shasum -a 256 backups/wt32-original.bin
```

The file must be exactly 4,194,304 bytes. Keep the checksum with the backup.

## Flash And Monitor

```bash
idf.py -p "$WT32_PORT" flash
idf.py -p "$WT32_PORT" monitor
```

Use `Ctrl-]` to exit the monitor. The dashboard uses 115200 baud. If automatic
download mode fails, hold the board's BOOT button while starting the flash and
release it after the connection begins.

To flash and immediately open the monitor:

```bash
idf.py -p "$WT32_PORT" flash monitor
```

## Restore The Original Backup

Stop the serial monitor before restoring. Verify the backup size and checksum,
then write the complete image back at address zero:

```bash
esptool.py --chip esp32 --port "$WT32_PORT" --baud 460800 \
  write_flash 0x0 backups/wt32-original.bin
```

Do not run `erase_flash` unless a valid full backup has already been verified.
Restoring the full image replaces the bootloader, partition table, applications,
NVS, and all other data on the board.

## Hardware Probe

The standalone probe reports the detected Flash and PSRAM sizes and scans the
touch I2C bus. Build it from its own project directory:

```bash
cd tools/hw_probe
idf.py set-target esp32
idf.py build
idf.py -p "$WT32_PORT" flash monitor
```

Expected output for the supported batch includes an ACK at `0x38` and FT5x06
identification registers. An ACK at `0x40`, or no ACK at `0x38`, indicates that
the touch controller needs separate investigation. Return to the repository root
before building the dashboard again.

## Flash Layout

The custom table fits inside 4 MB and keeps two equal OTA application slots:

| Partition | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | 24 KB | settings and future credentials |
| `otadata` | `0xf000` | 8 KB | OTA selection metadata |
| `phy_init` | `0x11000` | 4 KB | optional RF calibration data |
| `ota_0` | `0x20000` | 1.8125 MB | application slot A |
| `ota_1` | `0x1f0000` | 1.8125 MB | application slot B |
| `storage` | `0x3c0000` | 192 KB | reserved SPIFFS data |

The table ends at `0x3f0000`, leaving the final 64 KB unused. Each application
binary must remain below `0x1d0000`; run `idf.py size` after every material UI,
font, TLS, or networking change. OTA and SPIFFS are reserved but not implemented
in the current prototype.

## Runtime Architecture

```text
main/app_main.c
  board_wt32       LCD, touch, backlight, board profile
  app_model        immutable snapshot and local mock provider
  dashboard_ui     LVGL pages, navigation, controls, theme
```

The mock provider runs on core 0 at priority 3 and overwrites a one-item queue
with the latest immutable snapshot. The only task that calls LVGL is the UI task,
which runs on core 1 at priority 5 with a 12 KB internal stack. The LCD DMA ISR
only calls LVGL's ISR-safe `lv_disp_flush_ready()` completion primitive; all
object creation, mutation, input handling, and timer work stays in the UI task.
The completion call only clears the two volatile flags in the static internal
draw-buffer descriptor. This is required because LVGL waits for the previous DMA
transfer inside `lv_timer_handler()` before reusing a partial double buffer.

The display uses two internal DMA-capable `480 x 20` RGB565 draw buffers (38,400
bytes total). Rotation is performed by the ST7796 controller with `swap_xy` and
dual-axis mirroring; no full framebuffer or LVGL software rotation is used. The
touch transform applies the same 180-degree orientation:

```text
screen_x = 479 - raw_y
screen_y = raw_x
```

## Directory Layout

```text
components/
  app_model/       snapshot contract and mock data producer
  board_wt32/      WT32-SC01 board support
  dashboard_ui/    LVGL UI and generated subset fonts
main/              firmware entry point and task orchestration
tools/hw_probe/    standalone non-UI hardware diagnostic firmware
gateway/           Mock-first Dashboard Gateway service and tests
partitions.csv     4 MB dual-OTA partition table
sdkconfig.defaults reproducible project defaults
```

## Dashboard Gateway

`gateway/` is a separately runnable FastAPI prototype. It exposes bootstrap and
11-section snapshot responses, a deterministic Mock tick, and idempotent
Home Assistant-style commands. The two mutating endpoints are protected by an
`X-API-Token`; Docker fails closed when no token is configured.

```bash
cd gateway
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-test.lock
cp .env.example .env
# Set a random GATEWAY_API_TOKEN of at least 16 characters in .env.
uvicorn app.main:app --host 0.0.0.0 --port 8080
```

Run `pytest` from `gateway/`, or use `docker compose up --build`. See
`gateway/README.md` for the response contract, command examples, and security
notes. The service is implemented and tested, but this local-Mock firmware does
not consume it yet; that connection is intentionally deferred until the screen
interaction is approved.

## Known Limitations

- Firmware data is local mock data; the implemented Dashboard Gateway is not
  connected to the device yet.
- Only the ST7796 plus FT5x06-compatible hardware batch is supported.
- Touch rotation follows the official board profile but still needs a physical
  four-corner test on each display batch.
- The photo page is a UI mock. SMB, JPEG delivery, caching, and NAS credentials
  are intentionally deferred to the gateway design.
- The 4 MB Flash limits each OTA image to 1.8125 MB. Arbitrary Chinese RSS text
  is not possible with the current subset fonts.
- The board has no speaker, codec, or amplifier; the music page was removed.
- Wi-Fi, service authentication, OTA download, and production error recovery are
  not implemented in this firmware yet.
