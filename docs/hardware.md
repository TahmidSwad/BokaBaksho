# Hardware

BokaBaksho targets an ESP32 DevKit v1 with a 128×64 SSD1306 OLED, four
momentary buttons, and a BLE radio. This document is the authoritative pin and
peripheral reference.

All values are defined in `src/common/config.h` under the `config` namespace.

---

## 1. Target Board

| Property | Value |
|---|---|
| PlatformIO environment | `esp32doit-devkit-v1` |
| Platform | `espressif32` |
| Framework | Arduino |
| CPU / RAM | 240 MHz dual-core Xtensa, 320 KB SRAM |
| Flash partition table | `huge_app.csv` (3,145,728 B app partition) |
| Flash usage today | 38.7% |
| RAM usage today | 23.9% |

---

## 2. Pin Map

| Function | GPIO | `config::pins::` symbol | Mode | Notes |
|---|---|---|---|---|
| Increment button | **26** | `ButtonIncrement` | `INPUT_PULLUP`, active-low | Wired to GND |
| Decrement button | **25** | `ButtonDecrement` | `INPUT_PULLUP`, active-low | Wired to GND |
| ENTER button | **27** | `ButtonEnter` | `INPUT_PULLUP`, active-low | Wired to GND |
| BACK button | **14** | `ButtonBack` | `INPUT_PULLUP`, active-low | Wired to GND |
| OLED SDA | **21** | `OledSda` | Hardware I2C | — |
| OLED SCL | **22** | `OledScl` | Hardware I2C | — |

All four buttons share the same electrical design: one terminal to the GPIO,
the other to GND, relying on the internal pull-up. Pressing the button reads
`LOW`.

The buttons post `ButtonIncrement` / `ButtonDecrement`, **not** `RotateRight` /
`RotateLeft` — those two are produced only by the serial debug keys `l` / `r`
in `InputService::PollSerialDebug()`. See
[api-reference.md §4.2](api-reference.md#42-inputservice--srcservicesinput_serviceh-cpp).

---

## 3. OLED Display

| Property | Value |
|---|---|
| Controller | SSD1306 |
| Resolution | 128 × 64 |
| Bus | Hardware I2C (Wire) |
| Preferred address | `0x3C` (`config::display::I2cAddr`) |
| Fallback address | `0x3D` (`config::display::I2cAddrFallback`) |
| Library | `olikraus/U8g2` |
| U8g2 instance | `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`, `U8G2_R0`, full framebuffer |

### 3.1 Boot-time probing

`Oled::Begin()` cannot rely on U8g2's return value — `u8g2.begin()` always
reports success — so it probes the bus directly:

1. `Wire.begin(21, 22)`
2. `ProbeAddress(0x3C)` → if ACK, use it
3. else `ProbeAddress(0x3D)` → if ACK, use it
4. else print `OLED I2C: FAILED (check wiring / address / power)` and return
   `false`, which halts the firmware
5. `u8g2.setI2CAddress(addr << 1)` — U8g2 expects the **8-bit** form
   (`0x3C` → `0x78`), so the 7-bit address is shifted left by one
6. `u8g2.begin()`, clear, flush

Serial output on success: `OLED I2C: device at 0x3C` (or `0x3D`).

### 3.2 Framebuffer discipline

Every render is a full `Clear()` → draw primitives → `Update()` cycle.
`Update()` maps to `u8g2.sendBuffer()`, which pushes the entire 1 KB
framebuffer over I2C. Partial updates are never used.

Coordinates are the U8g2 defaults: origin at the top-left, x grows right
(0–127), y grows down (0–63). Text baselines use the font's ascent.

---

## 4. Fonts

`Oled::Font` maps to four U8g2 faces:

| `Oled::Font` | U8g2 font | Size | Typical use |
|---|---|---|---|
| `Small` | `u8g2_font_courR08_tr` | ~8 px | Status lines, scroll arrows, `n / total` counters |
| `Medium` | `u8g2_font_courR10_tr` | ~10 px | Song lists, status messages, `ShowBigTime` top label |
| `Large` | `u8g2_font_courR18_tr` | ~18 px | Launcher app name, lyric words ≤ 8 characters |
| `LargeBold` | `u8g2_font_bubble_tn` | large | The big centred time in the Timer app |

Metrics are queried at runtime through `Oled::GetTextWidth()`,
`GetTextHeight()` (= ascent − descent) and `GetAscent()`.

`DisplayService` exposes the same four sizes through the abstract
`TextSize::{Small, Medium, Large}` enum plus an implicit `LargeBold` used only
by `ShowBigTime`.

---

## 5. Buttons

`Button` (`src/drivers/button/button.h/.cpp`) is a debounced, active-low,
edge-latching driver.

| Member | Purpose |
|---|---|
| `pin_` | Configured GPIO |
| `raw_` | Last raw level (`true` = pressed) |
| `debounced_` | Stable level after the debounce window |
| `latched_` | Pending press event, consumed by `WasPressed()` |
| `hold_until_` | End of the current debounce window (`millis() + 20`) |

Algorithm:

1. Read the pin. If it differs from `raw_`, update `raw_` and restart a
   **20 ms** debounce window.
2. While `millis() < hold_until_`, return without changing state.
3. If the settled level differs from `debounced_`, accept it. On a
   falling edge → rising press, set `latched_ = true`.
4. `WasPressed()` returns `latched_` once and clears it.

Only **press** events are reported; release transitions are ignored. Holding a
button produces exactly one event, because the latch is edge-triggered on the
press and there is no auto-repeat.

`Begin(pin)` seeds `raw_` from the current pin level so a button already held
at boot does not generate a phantom press.

---

## 6. BLE Radio

| Property | Value |
|---|---|
| Advertised name | `BokaBaksho` (`kBleDeviceName`) |
| Service UUID | `12345678-1234-5678-9abc-def012345678` |
| TX characteristic | `12345678-1234-5678-9abc-def012345679` (NOTIFY) |
| RX characteristic | `12345678-1234-5678-9abc-def012345680` (WRITE) |
| CCCD | `BLE2902` descriptor added to TX so notifications can be enabled |
| Requested MTU | 517 (BLE maximum) |
| Scan response | enabled |
| Preferred connection interval | `setMinPreferred(0x06)` / `setMaxPreferred(0x12)` → 7.5 ms … 22.5 ms |
| Advertising | starts in `Begin()`; restarts automatically on disconnect |

Serial markers: `BLE_SERVICE_INIT_OK`, `BLE_CLIENT_CONNECTED`,
`BLE_CLIENT_DISCONNECTED`, `BLE_SERVICE_STOPPED`.

The two preferred values are the *slave connection interval range* AD field,
in units of 1.25 ms (`0x06` = 7.5 ms, `0x12` = 22.5 ms). They are independent
fields: until 2026-10-08 both were passed to `setMinPreferred()`, which
overwrote the minimum and left the maximum at the `BLEAdvertising()` default of
`0x40` (80 ms).

The BLE stack runs on its own FreeRTOS task. See
[architecture.md](architecture.md#9-concurrency-model) for how this is kept
away from the render path.

---

## 7. Serial Console

| Property | Value |
|---|---|
| Baud rate | 115200 (`monitor_speed`) |
| `monitor_rts` / `monitor_dtr` | `0` / `0` — suppresses the auto-reset pulse |
| Debug input | `l r e b + -` (see [build-and-test.md](build-and-test.md#4-serial-debug-input)) |

Boot markers, in order: `OLED I2C: ...`, `BLE_SERVICE_INIT_OK`, `READY`.

---

## 8. Storage

| Property | Value |
|---|---|
| Filesystem | LittleFS (`board_build.filesystem = littlefs`) |
| Mount point | `/` (default) |
| Failure handling | Fatal — `setup()` prints `Storage init failed` and spins |

The filesystem is mounted at every boot even though no application currently
reads or writes it. Uploading a `data/` directory is done with
`pio run -t uploadfs`.

---

## 9. Electrical Summary

```
                 ESP32 DevKit v1
              ┌────────────────────┐
   Increment ─┤ GPIO 26            │
   Decrement ─┤ GPIO 25            │      SSD1306 OLED
      ENTER ──┤ GPIO 27            │   ┌────────────────┐
       BACK ──┤ GPIO 14            ├───┤ SDA  (GPIO 21) │
              │                    ├───┤ SCL  (GPIO 22) │
              │              GND ──┼───┤ GND            │
              │             3V3 ───┼───┤ VCC            │
              └────────────────────┘   └────────────────┘

  Each button:  GPIO ──┬──── button ──── GND
                       └── internal pull-up (INPUT_PULLUP)
```
