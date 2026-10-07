# Boka_Baksho

A lightweight, OS-style multi-application firmware for a portable ESP32 device,
built around a 128×64 SSD1306 OLED and four physical buttons.

Boka_Baksho provides a scrollable app launcher, an event-driven core with an
explicit application lifecycle, a foreground-exclusive display model, and a BLE
bridge to a companion PC that performs audio playback. The device currently
ships with a karaoke **Lyrics** app and a **Timer/Stopwatch** app.

> The project was previously named *BoomBox2*. All documentation now uses
> **Boka_Baksho**. The BLE advertised device name is `BokaBaksho`.

---

## At a Glance

| | |
|---|---|
| **Target** | `esp32doit-devkit-v1` (ESP32, Arduino framework) |
| **Build system** | PlatformIO 6.x |
| **Display** | 128×64 SSD1306 OLED, hardware I2C (U8g2) |
| **Input** | 4 active-low buttons: Increment, Decrement, ENTER, BACK |
| **Connectivity** | BLE GATT server (device name `BokaBaksho`) |
| **Storage** | LittleFS (mounted at boot; currently unused by apps) |
| **Memory model** | Static allocation only — no `new`, `malloc`, or STL containers |
| **Registered apps** | `Lyrics`, `Timer` (+ the `SystemUi` launcher root) |

Latest measured build (2026-10-03):

```
RAM:   23.9%  (78,440 / 327,680 bytes)
Flash: 38.7%  (1,216,249 / 3,145,728 bytes)
```

---

## Architecture

```
┌───────────────────────────────────────────────────────────┐
│  App Layer     LyricsApp · StopwatchApp (Timer)           │
│                each owns a state machine + its own UI     │
├───────────────────────────────────────────────────────────┤
│  SystemUI      Launcher: animated cat screensaver,        │
│                then a paginated app menu                  │
├───────────────────────────────────────────────────────────┤
│  Core          AppManager   registry + navigation stack   │
│                EventBus     ring-buffer pub/sub queue     │
│                IApp         application contract          │
├───────────────────────────────────────────────────────────┤
│  Services      DisplayService   foreground-only rendering │
│                InputService     button + serial polling   │
│                StorageService   LittleFS wrapper          │
│                BleService       GATT text protocol        │
│                AudioService     stub (unimplemented)      │
├───────────────────────────────────────────────────────────┤
│  Drivers       Oled    U8g2 SSD1306 over hardware I2C     │
│                Button  debounced active-low polling       │
├───────────────────────────────────────────────────────────┤
│  Common        config · event_types · input_types · ui_types │
└───────────────────────────────────────────────────────────┘
```

Three ideas hold the whole system together:

1. **Navigation stack as ownership.** `SystemUi` is permanently the root of an
   8-deep stack, so there is always exactly one foreground app. Launching pushes,
   BACK pops. Only the foreground app may draw — enforced by `DisplayService`,
   not by a separate resource manager.

2. **Everything flows through the EventBus.** Apps post `DisplayRequest`
   events; buttons post `InputEvent` events. Publishers never know who consumes
   them. The queue is a fixed 16-slot ring buffer.

3. **Static allocation.** Every array, buffer, and table is sized at compile
   time. Memory use is deterministic and fragmentation is impossible.

Full detail: [`docs/architecture.md`](docs/architecture.md).

---

## Controls

| Hardware | Launcher screensaver | Launcher menu | Lyrics app | Timer app |
|---|---|---|---|---|
| **Increment** (GPIO 26) | wake → menu | next app | next song | +1 minute preset |
| **Decrement** (GPIO 25) | wake → menu | previous app | previous song | −1 minute preset |
| **ENTER** (GPIO 27) | wake → menu | launch app | select / pause / resume | start / pause / re-arm |
| **BACK** (GPIO 14) | (ignored) | return to screensaver | back to song list | reset; exit when already reset |

Serial debug input works with no hardware attached (`pio device monitor`,
115200 baud):

| Key | Event posted |
|---|---|
| `+` / `-` | `ButtonIncrement` / `ButtonDecrement` |
| `l` / `r` | `RotateLeft` / `RotateRight` |
| `e` | `Enter` |
| `b` | `Back` |

---

## Project Layout

```
Boka_Baksho/
├── README.md                 this file
├── platformio.ini            board, filesystem, dependencies, build flags
├── docs/                     full documentation set (see docs/index.md)
├── pc_client/                PC companion: GUI, BLE client, audio, self-test
├── run_companion.py          entry point (python run_companion.py)
├── requirements.txt          companion dependencies (bleak, pygame-ce, pyinstaller)
├── build_client.sh           Linux/macOS: build dist/BokaBakshoCompanion
├── build_client.bat          Windows:     build dist\BokaBakshoCompanion.exe
├── scripts/                  Windows/PowerShell build helpers
├── build_pio.bat             Windows build shortcut
├── test/                     test suite placeholder (see docs/build-and-test.md)
└── src/
    ├── main.cpp              boot sequence, wiring, main loop
    ├── common/               config.h, event_types.h, input_types.h, ui_types.h
    ├── core/                 app_base.h, app_manager.*, event_bus.*, system.h
    ├── drivers/              oled/, button/
    ├── services/             display, input, storage, ble, audio (stub)
    ├── system_ui/            launcher app
    └── apps/                 lyrics/, clock/ (Timer + Stopwatch)
```

---

## Quick Start

### Prerequisites

- PlatformIO Core ≥ 6.x (`pip install platformio`, or the VS Code extension)
- An ESP32 DevKit v1 board, SSD1306 OLED, and four momentary buttons to GND

### Build and flash

```sh
pio run                      # compile
pio run -t upload            # flash firmware over USB
pio device monitor           # 115200 baud: logs + serial debug input
```

The bootloader and monitor are configured with `monitor_rts = 0` /
`monitor_dtr = 0` so the auto-reset circuit does not fight an external adapter.

### First boot diagnostics

On the serial monitor you should see:

```
OLED I2C: device at 0x3C     (or 0x3D; "FAILED" means wiring/address/power)
BLE_SERVICE_INIT_OK
READY
```

Storage and display failures are fatal: the firmware halts and retries
`delay(1000)` forever rather than running blind.

---

## PC Companion

The ESP32 has no audio hardware of its own, so **the PC companion is not
optional**: it holds the song library, streams the lyrics, and plays the sound.
It is a small tkinter window that scans for `BokaBaksho` and speaks the
protocol in [`docs/ble-protocol.md`](docs/ble-protocol.md).

```
┌────────────────────────────────────────────────────────────────────────────────┐
│ BokaBaksho Companion                                    ● Disconnected        │
│ [Connect] [Disconnect]  Folder [ ~/Music/SongWithLyrics ]  [Browse] [Refresh] │
│ Songs                      │ Now playing                                      │
│  Tumi                      │  —                                               │
│  Closer                    │  Stopped                                         │
│  …                         │  Device BokaBaksho                               │
│ Log                                                        [Clear]            │
│ [22:41:03] Connected — subscribed to TX notifications.                        │
└────────────────────────────────────────────────────────────────────────────────┘
```

The media folder defaults to `~/Music/SongWithLyrics`. It needs one
`<Song>.txt` lyric file per song, plus a matching audio file with the same
basename in `.mp3`, `.wav`, `.ogg`, `.m4a`, or `.flac`. The list of `.txt`
files *is* the library.

**The app never creates this folder.** If it is missing, the window shows
`folder not found`, the log names the path, and nothing is written to disk —
use **Browse…** to point at a different one.

### Run from source

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python -m pc_client          # or: python run_companion.py
```

`tkinter` ships with CPython but is packaged separately by most distros:
`sudo dnf install python3-tkinter` (Fedora/RHEL) or
`sudo apt install python3-tk` (Debian/Ubuntu).

### Build an executable

```sh
./build_client.sh          # -> dist/BokaBakshoCompanion
build_client.bat           # -> dist\BokaBakshoCompanion.exe
```

### Self-test (no hardware required)

```sh
.venv/bin/python -m pc_client.selftest
```

**Verified 2026-10-03:** self-test 36/36 passed, `pio run` succeeds, the
PyInstaller build succeeds and its window opens. **Not verified:** an actual
BLE session against an ESP32 — no hardware has been attached to this client.
Details: [`docs/build-and-test.md` §6](docs/build-and-test.md#6-pc-companion-client).

---

## Documentation

The complete documentation set lives in [`docs/`](docs/index.md):

| Document | Contents |
|---|---|
| [Architecture](docs/architecture.md) | Layers, data flow, navigation stack, app lifecycle |
| [Hardware](docs/hardware.md) | Pin map, display, buttons, BLE radio, fonts |
| [API Reference](docs/api-reference.md) | Every module, class, method, and shared type |
| [Applications](docs/applications.md) | Launcher, Lyrics, and Timer behaviour and state machines |
| [BLE Protocol](docs/ble-protocol.md) | GATT layout and the full command vocabulary |
| [Conventions](docs/conventions.md) | Coding standards, constraints, how to extend the system |
| [Build & Test](docs/build-and-test.md) | PlatformIO setup, flashing, serial debugging, testing |
| [Current State](docs/current-state.md) | What works, what does not, known issues |
| [Roadmap](docs/roadmap.md) | Planned and pending work |
| [Decisions](docs/decisions.md) | Architectural decision records |
| [Change Log](docs/change-log.md) | Dated history of notable changes |
