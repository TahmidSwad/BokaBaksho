# Current State

Status of the BokaBaksho firmware as of **2026-10-03**, verified against the
source tree and a successful `pio run`.

---

## 1. Completed

### 1.1 Core framework

- **`EventBus`** — 16-slot ring buffer, 4 subscribers per `EventType`,
  `Subscribe` / `Post` / `Dispatch`. Wired with two subscriptions:
  `DisplayRequest → display_service`, `InputEvent → app_manager`.
- **`AppManager`** — registry of up to 6 apps, 8-deep navigation stack with the
  launcher permanently at the root, input routing with automatic `Back` pop,
  rollback when `OnActivate()` fails.
- **`IApp`** — full lifecycle contract (`Begin`, `OnActivate`, `OnDeactivate`,
  `Update`, `HandleInput`, `GetAppId`, `GetName`).

### 1.2 Drivers

- **`Oled`** — U8g2 SSD1306 over hardware I2C with a real presence probe
  (`0x3C` then `0x3D`), 8-bit address conversion, four fonts, line/rect/fill
  primitives. Failure is reported on serial and halts the firmware.
- **`Button`** — active-low `INPUT_PULLUP`, 20 ms debounce, edge-latched press,
  read-and-clear `WasPressed()`.

### 1.3 Services

- **`DisplayService`** — renders all eight `DisplayRequestType` variants and
  enforces foreground-only ownership.
- **`InputService`** — polls four buttons and the serial console, posts
  normalized `InputEvent`s with `sender = AppId::Count`.
- **`StorageService`** — LittleFS wrapper (`Begin`, `Exists`, `Open`, `Remove`,
  `Rename`, `FileSize`, `IsReady`). Mounted at boot; **no application uses it**.
- **`BleService`** — GATT server, TX notify / RX write, newline framing,
  single-line 256-byte reassembly, advertising restart on disconnect, registered
  command callback.
- **`AudioService`** — declared, constructed, `Begin()` called; otherwise an
  empty stub.

### 1.4 SystemUI launcher

- Animated **cat screensaver** on boot and on `BACK` from the menu: seven
  expressions, blinking, random left/right gaze, head bobbing, hold-time
  alternation between expression and neutral phases.
- Paginated app menu with wraparound navigation, ENTER to launch, `BACK`
  returning to the screensaver.
- Menu selection persists across app launch/return.

### 1.5 LyricsApp

- Song library streamed from the PC over BLE with a 5-row scrolling window,
  absolute/relative indexing, `^`/`v` scroll arrows, and an `n / total` counter.
- Lyrics streamed line-by-line (`LYRICS_DATA|` … `LYRICS_END`), parsed from
  `<mm:ss.ff>word` tokens into a 1000-word table.
- `PLAY` → `AUDIO_STARTED` handshake with a 10 s timeout, then timed display
  driven by `millis() - start_time_`.
- Pause/resume with pause-duration compensation, stop on deactivate, abort to
  the song list with `BACK`.
- Playlist auto-advance on `END`.
- Non-blocking failure screens for: no device, request timeout, no lyrics,
  handshake timeout.
- BLE disconnect during playback returns to the song list.

### 1.6 StopwatchApp (`AppId::Clock`, labelled **Timer**)

- Dual mode: stopwatch (preset 0, counts up) or timer (preset > 0, counts down).
- Preset in whole minutes, 0…65535, locked while running or finished.
- `ENTER` start/pause/re-arm, `BACK` reset (and exit only from the reset state).
- Flashing `TIME'S UP!` after expiry, 500 ms period, until acknowledged.
- Redraw throttled to 200 ms while running; `mm:ss` and `h:mm:ss` formats.

### 1.7 Build

- PlatformIO environment `esp32doit-devkit-v1`, LittleFS, `huge_app.csv`,
  `-Isrc`, U8g2 dependency.
- **Verified 2026-10-03:** builds successfully — RAM 23.9% (78,440 / 327,680),
  Flash 38.7% (1,216,249 / 3,145,728).

### 1.8 PC companion (`pc_client/`)

The PC side of the karaoke system — Python + tkinter + bleak + pygame-ce.
The ESP32 has no audio hardware, so the device is unusable without it.

- **GUI** (`gui.py`) — connect/disconnect, media-folder picker with Refresh,
  song list with the playing track highlighted, now-playing panel, scrolling
  log. State changes arrive from the BLE thread through a `queue.Queue` drained
  by `root.after(100, …)`; the Tk thread never touches the radio. The default
  folder is `~/Music/SongWithLyrics`, which is **read only** — a missing path
  yields `folder not found` and no filesystem writes.
- **BLE link** (`ble.py`) — dedicated asyncio thread, inbound line buffering
  across notifications, one worker thread per inbound command (so streaming
  lyrics cannot delay a `STOP`), outbound writes serialised so ordering is
  guaranteed. Writes are packed to the MTU and split adaptively when rejected.
- **Audio** (`audio.py`) — `pygame.mixer` state machine
  (IDLE / LOADED / PLAYING / PAUSED / STOPPED) plus an end-of-song monitor that
  sends `END`.
- **Protocol** (`protocol.py`) — UUIDs, framing, command parsing/building.
- **Self-test** (`selftest.py`) — 36 checks with the transport stubbed.

**Verified 2026-10-03:** `python -m pc_client.selftest` → 36/36 passed;
`pio run` succeeds; the PyInstaller build succeeds and its window opens.
**Not verified:** any BLE session against real hardware — no ESP32 has been
attached to this client. See [build-and-test.md §6.5](build-and-test.md#65-not-yet-verified).

---

## 2. Partially Implemented / Reserved

| Item | Status |
|---|---|
| **`AudioService`** | Stub only — no hardware control, no implementation. |
| **`AppId::Music`** | Reserved in the enum; no class, no registration. |
| **`AppId::Alarm`** | Reserved in the enum; no class, no registration. |
| **`AppId::Settings`** | Reserved in the enum; no class, no registration. |
| **`EventType::AudioEvent`** | Declared; zero subscribers, never posted. |
| **`EventType::System`** | Declared; a struct is built in `BleService::ProcessLine()` but **never posted**, zero subscribers. |
| **`BleService::OnEvent()`** | Implemented as an empty body; the service is never subscribed to the `EventBus`. |
| **`BleService::Stop()`** | Implemented; never called. Advertising runs for the whole session. |
| **`TIME_SYNC` / `TIME_ACK`** | Documented in `ble_service.h`; **not implemented** (no sender, empty receiver branch). |
| **`config::kLyricsPathA/B`** | Defined in `config.h`; **never referenced**. |
| **StorageService consumers** | Mounts fatally at boot, but no code reads or writes files. |
| **Test suite** | `test/README.md` describes a Unity/native suite; no test files and no `native` environment exist. |
| **PC companion** (`pc_client/`) | Implemented and self-tested (36/36), packaged successfully — but **never run against a real ESP32**; Windows/macOS builds untested. |

---

## 3. Known Issues / Limitations

### 3.1 Behavioural

- **`LoadFailed` never auto-returns.**
  `LyricsApp::Update()` has `case State::LoadFailed: break;`
  (`src/apps/lyrics/lyrics_app.cpp:565-566`). There is no timer and no
  timeout constant, so an error screen stays until the user presses `BACK`.
  In `LoadFailed`, `Enter` and the navigation buttons are *swallowed*
  (`HandleInput` returns `true` for them), so `BACK` is the only exit.
  The change-log entry of 2026-09-15 claiming a 1500 ms auto-return
  (`kLoadFailedTimeoutMs`) **no longer matches the code** — that constant does
  not exist.

- **`total_songs_` is trusted from the client.**
  It is only corrected downward when a `SONGS|` reply contains fewer than 5
  names (`lyrics_app.cpp:79-84`). If a client sends exactly 5 names and never
  sends `TOTAL_SONGS|`, `total_songs_` stays 0: decrementing the selection wraps
  to index 255 (the cursor disappears; `ENTER` is guarded so it does not
  crash), and playlist auto-advance computes
  `(selected_index_ + 1) % total_songs_` (`lyrics_app.cpp:576`) — a modulo by
  zero, which is undefined behaviour.

- **The song-list scroll window never advances.**
  `LyricsApp::buffer_offset_` is assigned exactly once — `buffer_offset_ = 0`
  in `EnterWaitingSongs()` (`lyrics_app.cpp:261`) — and is never updated to the
  offset that a `REQUEST_SONGS` actually asked for. `OnSongListReceived()` fills
  `song_buffer_` from the reply but leaves `buffer_offset_` at 0, so the buffer
  and the offset disagree as soon as the window moves.
  With more than 5 songs, once `selected_index_` reaches 5,
  `buf_idx = selected_index_ - buffer_offset_` (`lyrics_app.cpp:224`) equals 5
  and the guard `buf_idx >= buffer_count_` (`lyrics_app.cpp:225`) makes
  `LoadSelectedLyric()` return immediately, while `ShowFileList()` computes
  `selected = selected_index_ - buffer_offset_` (`lyrics_app.cpp:405`) past the
  visible rows so the `>` cursor is not drawn.
  **Symptom: scrolling past the fifth song loses the cursor and ENTER does
  nothing.** The `^` scroll arrow can never appear
  (`has_more_above = buffer_offset_ > 0`, `lyrics_app.cpp:409`).
  This **cannot be worked around from the PC side** — no reply can make
  `selected_index_ - 0` land inside a 5-entry buffer. Fix: record the requested
  offset in `buffer_offset_` when the reply arrives.

- **No automatic recovery from `LoadFailed`.** A late `SONGS` reply arriving
  while in `LoadFailed` is ignored (`OnSongListReceived` only acts on
  `WaitingSongs` and `FileList`).

- **Word display blanks after 5 s** (`kWordDisplayTimeoutMs`). Long silences
  between timed words leave the screen empty rather than holding the last word.

- **Stopwatch drift.** Timing is `millis()` delta-based with no RTC or NTP;
  drift accumulates over long runs.

- **Handshake requires 10 s of patience** (`kHandshakeTimeoutMs`). A slow PC
  client produces a `No device / connected` failure screen. There is no retry
  affordance — the user must press `BACK` and select the song again.

- **Queue overflow drops events.** `EventBus::Post()` returns `false` when the
  16-slot queue is full and the event is discarded; most callers ignore the
  return value, so the loss is silent.

- **Single BLE client**, no pairing, no authentication, no retransmission.

- **No OTA.** Firmware updates require a USB connection.

- **Render cost.** Every `DisplayRequest` performs a full framebuffer clear and
  a full I2C flush; there are no partial updates.

### 3.2 Code-level

- **Stale input-mapping comments.** `src/common/config.h:16-18` and
  `src/services/input_service.h:28-30` still say
  *"Increment button → RotateRight / Decrement → RotateLeft"*. The hardware
  buttons actually post `ButtonIncrement` / `ButtonDecrement`
  (see `InputService::Poll()`).

- **BLE connection-interval setup calls the same setter twice.**
  `src/services/ble_service.cpp:70-71` calls `setMinPreferred(0x06)` and then
  `setMinPreferred(0x12)` with comments claiming "min" and "max"; the second
  call overwrites the first and no maximum setter is used.

- **Registration happens before `EventBus` subscription.**
  `src/main.cpp:45-51` calls `lyrics_app.Begin()` (which posts a
  `DisplayRequest`) before `display_service` is subscribed. The event is
  dispatched on the first `loop()` and dropped by the ownership check because
  the foreground is then `SystemUi`. Harmless — `OnActivate()` re-issues the
  screen — but it means the boot-time status draw is a no-op.

- **`Stop()`/`OnEvent()` dead paths in `BleService`** (see §2).

- **`ShowSongList()` uses a function-local `static char counter_buf[8]`** for
  the `n / total` counter. Not re-entrancy safe; acceptable only because
  rendering is single-threaded.

- **`AppId::Clock` vs. label `"Timer"`.** `StopwatchApp` is registered under
  `Clock` but presents as `Timer`; `GetName()` and `GetAppId()` disagree in
  intent, which is confusing when reading logs or the registry.

- **Windows build helpers are machine-specific.** `build_pio.bat` and
  `scripts/do_build.ps1` hard-code
  `c:\Users\Tahmidur Rahman\.platformio\...`; `scripts/run_build.ps1` is a stub
  that prints `test`.

### 3.3 Documentation drift (resolved by this documentation pass)

These were true of the previous docs and are corrected here:

| Old claim | Reality |
|---|---|
| Lyric files live at `data/lyrics/Tumi.txt`, `Closer.txt` | **No `data/` directory exists.** Lyrics are streamed from the PC over BLE. The constants in `config.h` are dead. |
| `pio run -t uploadfs` fixes "Load failed" | There are no lyrics on the device to upload; `Load failed` is now a BLE status screen. |
| `AppId::Clock` is unimplemented | `StopwatchApp` is registered under `AppId::Clock`. |
| The launcher is "a scrollable menu" | It boots into an animated cat screensaver; the menu is a second screen. |
| BLE protocol = `PLAY/STOP/PAUSE/RESUME/TIME_SYNC` | The real protocol adds `REQUEST_SONGS`, `LYRICS`, `LYRICS_DATA`, `LYRICS_END`, `SONGS`, `TOTAL_SONGS`, `END`; `TIME_SYNC` is not implemented. |
| Error states auto-return after 1.5 s | No auto-return exists (§3.1). |
| `core/system.h` declares all global singletons | It declares only `app_manager` and `event_bus`; services declare their own `extern`. |
| "No comments unless asked" | The codebase carries banner and explanatory comments throughout. |

---

## 4. Pending Tasks

- Implement `AudioService` for on-device playback (hardware TBD).
- Implement `Music`, `Alarm`, and `Settings` apps.
- Restore an auto-return (or an explicit dismissal) for `LoadFailed`.
- Guard `total_songs_ == 0` before the auto-advance modulo.
- Add word highlighting / scrolling instead of a single centred word.
- Add a progress indicator for lyric playback position.
- Implement `TIME_SYNC`/`TIME_ACK` or remove them from the protocol docs.
- Build the `native` test environment and the `EventBus`/`AppManager` unit
  tests described in `test/README.md`.
- Replace the machine-specific Windows build helpers with portable scripts.
- Set `buffer_offset_` from the requested offset in `OnSongListReceived`, so
  the song-list window can actually scroll past the first 5 songs.
- Exercise the PC companion (`pc_client/`) against a real ESP32 — the client is
  implemented and self-tested but has never had a device attached.

---

## 5. In-Code TODOs

| Location | Note |
|---|---|
| `src/services/ble_service.cpp:146` | `(void)event;` — a `System` event is constructed but never posted; placeholder for future expansion. |
| `src/services/ble_service.cpp:165-169` | `OnEvent()` empty — the service is not subscribed to the `EventBus`. |
| `src/main.cpp:94-101` | Empty handler branches for `STOPPED`, `PAUSED`, `RESUMED`, `TIME_ACK` — received and ignored. |
| `src/common/config.h:45-46` | `kLyricsPathA` / `kLyricsPathB` — unused legacy lyric paths. |
