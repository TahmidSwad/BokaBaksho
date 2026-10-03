# Build & Test

Everything needed to compile, flash, monitor, and test Boka_Baksho, plus how
to run and package the PC companion.

---

## 1. Prerequisites

| Tool | Version | Notes |
|---|---|---|
| PlatformIO Core | 6.x (verified with 6.1.19) | `pip install platformio` or the VS Code extension |
| Python | 3.6+ for PlatformIO; **3.10+** for the PC client (verified on 3.14) | `pc_client/` — see [§6](#6-pc-companion-client) |
| USB cable | — | ESP32 DevKit v1 flashing |
| Serial terminal | — | `pio device monitor`, 115200 baud |

Libraries are resolved automatically by `platformio.ini`
(`lib_deps = olikraus/U8g2`); no manual installation is needed.

---

## 2. Build Commands

```sh
pio run                       # compile for esp32doit-devkit-v1
pio run -t upload             # flash firmware over USB
pio run -t uploadfs           # flash the LittleFS image from data/ (none present today)
pio run -t clean              # remove build artifacts
pio device monitor            # 115200 baud serial monitor
pio run -t size               # RAM / flash usage report
```

A full build takes roughly 15 seconds warm.

### 2.1 Windows helpers

| File | Behaviour |
|---|---|
| `build_pio.bat` | Calls a hard-coded PlatformIO install path (`c:\Users\Tahmidur Rahman\.platformio\penv\Scripts\platformio.exe`) with `run` |
| `scripts/do_build.ps1` | Same path, appended to `$env:Path`, output not redirected |
| `scripts/run_build.ps1` | Stub — prints `test` and does nothing |

These reference a specific user's Windows profile and will fail on any other
machine. Prefer `pio run` directly, or edit the paths.

### 2.2 Build output (verified 2026-10-03)

```
RAM:   [==        ]  23.9% (used 78440 bytes from 327680 bytes)
Flash: [====      ]  38.7% (used 1216249 bytes from 3145728 bytes)
========================= [SUCCESS] Took 7.02 seconds =========================
```

The figure above is an incremental rebuild after the `LoadSelectedLyric`
lyrics-timeout fix; a cold/full build takes roughly 15 seconds.

### 2.3 Configuration reference

From `platformio.ini`:

| Key | Value | Why |
|---|---|---|
| `board_build.filesystem` | `littlefs` | Matches `StorageService` |
| `board_build.partitions` | `huge_app.csv` | 3 MB app partition, minimal SPIFFS |
| `monitor_speed` | `115200` | Matches `Serial.begin(115200)` |
| `monitor_rts` / `monitor_dtr` | `0` / `0` | Prevents the auto-reset pulse on monitor connect |
| `build_flags` | `-Isrc` | Enables `#include "core/…"` style includes |
| `lib_deps` | `olikraus/U8g2` | OLED driver |

There is a **single environment**: `[env:esp32doit-devkit-v1]`. No `native`,
`debug`, or `release` variants are defined.

---

## 3. Flashing and First Boot

```sh
pio run -t upload
pio device monitor
```

Expected console output:

```
OLED I2C: device at 0x3C        ← or 0x3D; FAILED = wiring/address/power
BLE_SERVICE_INIT_OK
READY
```

If `Storage init failed` or `Display init failed` appears, the firmware halts
and prints nothing further — it deliberately does not limp along.

Useful markers emitted during normal operation:

| Marker | Meaning |
|---|---|
| `READY` | `setup()` completed |
| `BLE_CLIENT_CONNECTED` / `BLE_CLIENT_DISCONNECTED` | Central attached / left |
| `BLE_RX: <line>` | Every complete inbound line |
| `BLE TX: <command>` | Request/PLAY/LYRICS transmissions from the Lyrics app |
| `LYRICS_ON` / `LYRICS_OFF` | Lyrics app activated / deactivated |
| `BLE: AUDIO_STARTED received`, `BLE: END received` | Handshake / playback end |
| `BLE: SONGS received <n> songs`, `BLE: TOTAL_SONGS=<n>` | Library replies |
| `BLE: LYRICS_END, <n> words parsed` | Lyric stream complete |
| `BLE: Handshake timeout`, `BLE: Song request timeout` | Failure paths |

---

## 4. Serial Debug Input

The whole system can be driven without hardware. Type into the monitor:

| Key | Event posted | Equivalent button |
|---|---|---|
| `+` | `ButtonIncrement` | Increment (GPIO 26) |
| `-` | `ButtonDecrement` | Decrement (GPIO 25) |
| `l` / `L` | `RotateLeft` | *(none — encoder alias)* |
| `r` / `R` | `RotateRight` | *(none — encoder alias)* |
| `e` / `E` | `Enter` | ENTER (GPIO 27) |
| `b` / `B` | `Back` | BACK (GPIO 14) |

Every application accepts both the `Button*` pair and the `Rotate*` pair for
navigation, so either spelling works. Note that `+`/`-` and `r`/`l` produce
*different* event types even though apps treat them identically — useful for
verifying that a new app handles both.

Characters are consumed every `loop()` iteration by
`InputService::PollSerialDebug()`; anything that is not in the table above is
silently discarded.

---

## 5. Testing

### 5.1 Current status: no runnable firmware test suite

`test/README.md` describes a PlatformIO native test suite using Unity. As of
2026-10-03:

- `test/` contains **only `README.md`** — no test source files.
- `platformio.ini` defines **no `native` environment**, so
  `pio test -e native` has nothing to build against.

`pio test` therefore does not exercise anything. The suite described in
`test/README.md` is a placeholder, not an implemented suite.

> The PC companion in `pc_client/` **does** have a runnable self-test — see
> [§6.3](#63-self-test-no-hardware-needed). It does not touch the firmware.

### 5.2 What is testable today

| Component | How it is validated |
|---|---|
| `EventBus` | Serial: post events with debug keys and observe app responses |
| `AppManager` navigation | Serial: `e` to launch, `b` to pop, watch `LYRICS_ON`/`LYRICS_OFF` |
| `DisplayService` ownership | Visual: background apps must not draw |
| `Button` debounce | Physical: single press = single event |
| `Oled` probe | Serial: `OLED I2C: device at 0x..` |
| `BleService` | `BLE_RX:`/`BLE TX:` logging plus a BLE central |
| `LyricsApp` state machine | BLE central replaying the protocol in [ble-protocol.md §4](ble-protocol.md#4-session-sequence) |
| `StopwatchApp` | Visual + `millis()` observation |
| PC companion (`pc_client/`) | `.venv/bin/python -m pc_client.selftest` — transport stubbed, see [§6.3](#63-self-test-no-hardware-needed) |

### 5.3 Adding a real test suite

1. Add a host environment to `platformio.ini`:

   ```ini
   [env:native]
   platform = native
   test_framework = unity
   build_flags = -Isrc
   ```

2. Create `test/test_core/test_core.cpp` with `UNITY_BEGIN()` / `RUN_TEST` /
   `UNITY_END()`.
3. Run `pio test -e native`.

Two obstacles must be solved first:

- **Arduino headers.** `core/app_base.h`, `event_bus.h`, and `app_manager.h`
  all include `<Arduino.h>`. A host build needs either a shim header or
  `ARDUINO`-guarded includes.
- **Globals with side effects.** `app_manager` and `event_bus` are constructed
  at static-init time, which is fine, but anything pulling in `main.cpp` will
  drag in the whole firmware. Tests should include only `core/` headers.

Hardware-dependent code (drivers, `DisplayService`, `BleService`) is not
host-testable without mocking the HAL and should stay covered by on-device
serial testing.

---

## 6. PC Companion Client

`pc_client/` is the PC half of the karaoke system: a small tkinter window that
scans for `BokaBaksho`, streams the song library and lyrics over BLE, and plays
the audio through `pygame.mixer`. The ESP32 never plays sound itself, so the
device cannot work without it.

### 6.1 Run from source

```sh
python3 -m venv .venv                 # once
.venv/bin/pip install -r requirements.txt
.venv/bin/python -m pc_client         # equivalent: python run_companion.py
```

`tkinter` is part of CPython but packaged separately by most distributions:

```sh
sudo dnf install python3-tkinter      # Fedora / RHEL
sudo apt install python3-tk           # Debian / Ubuntu
```

A working audio device is not required to start; the window opens and reports
`Audio unavailable` in the log if the mixer cannot be initialised.

### 6.2 Build a standalone executable

```sh
./build_client.sh                     # Linux / macOS -> dist/BokaBakshoCompanion
build_client.bat                      # Windows       -> dist\BokaBakshoCompanion.exe
```

Both scripts create `.venv` if it is missing, install `requirements.txt`, and
run PyInstaller `--onefile --windowed`. Verified 2026-10-03 on Linux: the build
succeeds, the binary is 26 MB, and it opens its window with no console.

Artifacts land in `build/`, `dist/`, and `*.spec`; all three are gitignored.

### 6.3 Self-test (no hardware needed)

```sh
.venv/bin/python -m pc_client.selftest
```

34 checks covering command parsing, library and audio-file resolution, the
`REQUEST_SONGS` guards, atomic `TOTAL_SONGS`+`SONGS`, lyric streaming, MTU
packing, adaptive batch splitting, command dispatch, and the disconnected-send
guard. Exit status 0 means every check passed.

**Verified 2026-10-03: 34/34 passed.**

### 6.4 Dependencies

| Package | Why |
|---|---|
| `bleak` | cross-platform BLE client (BlueZ / WinRT / CoreBluetooth) |
| `pygame-ce` | provides `pygame.mixer` for playback. **Not** upstream `pygame`: 2.6.1 ships no Python 3.14 wheel and falls back to a source build, which fails. `pygame-ce` 2.5.8 ships `cp314-manylinux_x86_64` and is import-compatible |
| `pyinstaller` | executable packaging — build only, not needed to run from source |
| `tkinter` | windowing; ships with CPython, installed separately by the distro |

### 6.5 Not yet verified

- **No BLE session has been run against real hardware.** The protocol logic is
  covered with the transport stubbed, and the packaged window has been observed
  opening, but no ESP32 has been attached to this client.
- **Windows and macOS are untested.** Both are supported by bleak, pygame-ce,
  and PyInstaller in principle; only Linux has been exercised.

---

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Storage init failed`, then silence | LittleFS missing/corrupt | `pio run -t uploadfs`, or reflash |
| `Display init failed` | OLED not ACKing on `0x3C`/`0x3D` | Check SDA/SCL/VCC/GND; run `i2cdetect` |
| `OLED I2C: FAILED (check wiring / address / power)` | Same as above | Same as above |
| `BLE_SERVICE_INIT_OK` but no client connects | Central not scanning for this service | Scan for service UUID `12345678-1234-5678-9abc-def012345678` |
| Lyrics app stuck on `Connect to PC` | No central, or central never replied | Start the client ([§6](#6-pc-companion-client)); see [ble-protocol.md §5](ble-protocol.md#5-writing-a-pc-companion) |
| `No device` immediately on selecting a song | BLE request timeout (5 s) | Client must reply within 5 s |
| `No lyrics` after loading | No `<mm:ss.ff>` token parsed | Check lyric format, [ble-protocol.md §3](ble-protocol.md#3-lyric-stream-format) |
| Black screen or garbage text | Dangling pointer in a queued event | Member storage — [conventions.md §4.3](conventions.md#43-member-storage-for-queued-event-payloads) |
| New app never appears in the menu | Not registered, or registered after `Begin()` | [conventions.md §5](conventions.md#5-adding-a-new-app) |
| Display shows the wrong app's screen | Ownership check rejected the sender | App must post with `sender = GetAppId()` while foreground |
| Upload fails with port error | Wrong serial port | `pio device list`, then `pio run -t upload --upload-port <port>` |
