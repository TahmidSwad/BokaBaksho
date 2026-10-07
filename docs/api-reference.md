# API Reference

Complete reference for every module in BokaBaksho: classes, methods, globals,
and shared types. Companion documents: [architecture.md](architecture.md) for
how the pieces fit together, [conventions.md](conventions.md) for the rules that
govern them.

**Naming conventions used below**

| Prefix | Meaning |
|---|---|
| `Begin()` | One-time initialisation, returns `false` on failure |
| `On*()` | Event or lifecycle callback |
| `Get*()` | Pure query |
| `Is*()` / `Has*()` | Boolean query |
| trailing `_` | Private member variable |

---

## 1. Core

### 1.1 `IApp` — application contract

*File: `src/core/app_base.h`*

Abstract base for every application, including the launcher.

| Method | Signature | Contract |
|---|---|---|
| `Begin` | `virtual bool Begin() = 0` | One-time init during `setup()`, after registration. |
| `OnActivate` | `virtual bool OnActivate() = 0` | The app is now the top of the stack. Return `false` to abort the launch; `AppManager` will roll back. |
| `OnDeactivate` | `virtual void OnDeactivate() = 0` | The app left the top of the stack. Release resources and stop playback here. |
| `Update` | `virtual void Update() {}` | Called every `loop()` while the app is foreground. Default is a no-op. |
| `HandleInput` | `virtual bool HandleInput(const InputEvent&) = 0` | Return `true` if consumed. Returning `false` for `InputType::Back` pops the stack. |
| `GetAppId` | `virtual AppId GetAppId() const = 0` | Constant identifier; also the display-ownership key. |
| `GetName` | `virtual const char* GetName() const = 0` | Launcher label. Return `""` for the launcher itself. |

Destructor is `virtual = default`.

> **Rule:** `GetAppId()` must be stable and unique per class —
> `AppManager::RegisterApp()` rejects a second registration with the same id.

---

### 1.2 `AppManager` — registry and navigation stack

*File: `src/core/app_manager.h`, `src/core/app_manager.cpp`*
*Global: `app_manager` (also re-declared in `core/system.h`)*
*Implements: `IEventSubscriber` (subscribed to `EventType::InputEvent`)*

**Constants**

| Name | Value |
|---|---|
| `kMaxApps` | `AppId::Count` = 6 |
| `kMaxDepth` | 8 |

**Registry**

| Method | Signature | Behaviour |
|---|---|---|
| `RegisterApp` | `bool RegisterApp(IApp* app)` | Appends to `apps_`. Returns `false` for `nullptr`, a full registry, or a duplicate `GetAppId()`. |
| `GetAppCount` | `uint8_t GetAppCount() const` | Number of registered apps. |
| `GetAppAt` | `IApp* GetAppAt(uint8_t index) const` | Registry entry by index, or `nullptr` out of range. Registration order = launcher menu order. |

**Navigation**

| Method | Signature | Behaviour |
|---|---|---|
| `Boot` | `bool Boot(IApp* system_ui)` | Sets `stack_[0]`, `depth_ = 1`. Fails on `nullptr` or if already booted. **Does not call `OnActivate()`.** |
| `LaunchApp` | `bool LaunchApp(AppId id)` | Finds the app by id; returns `true` without side effects if it is already on top; returns `false` if the stack is full or the id is unregistered. Otherwise deactivates the current top, calls `OnActivate()`, and pushes. On `OnActivate()` failure it reactivates the old top and returns `false`. |
| `GoBack` | `void GoBack()` | No-op when `depth_ <= 1`. Otherwise pops, deactivates the old top, activates the new top. |

**Queries and pump**

| Method | Signature |
|---|---|
| `IsForeground` | `bool IsForeground(AppId id) const` — true only when `id` is on top |
| `GetForeground` | `IApp* GetForeground() const` — top of stack, or `nullptr` pre-boot |
| `Update` | `void Update()` — calls `Update()` on the foreground app only |
| `OnEvent` | `void OnEvent(const Event&) override` — forwards `EventType::InputEvent` to `RouteInput()` |

`RouteInput()` and `IsRegistered()` are private.

---

### 1.3 `EventBus` — ring-buffer pub/sub

*File: `src/core/event_bus.h`, `src/core/event_bus.cpp`*
*Global: `event_bus` (declared in `core/system.h`)*

**Constants**

| Name | Value |
|---|---|
| `kMaxSubscribersPerType` | 4 |
| `kEventQueueSize` | 16 |

| Method | Signature | Behaviour |
|---|---|---|
| `Subscribe` | `bool Subscribe(EventType, IEventSubscriber*)` | Returns `false` for `nullptr` or when the per-type slot array is full. There is no `Unsubscribe`. |
| `Post` | `bool Post(const Event&)` | Copies the struct into the ring buffer. Returns `false` (event discarded) when full. Pointer fields are **not** deep-copied. |
| `Dispatch` | `void Dispatch()` | Drains the queue completely, calling every subscriber of the event's type in registration order. Re-entrant `Post()` from within a subscriber is safe only because the queue has spare capacity — it will fail if full. |

`IsQueueEmpty()` and `IsQueueFull()` are private.

> **See:** [architecture.md §3.1](architecture.md#31-the-pointer-lifetime-constraint-critical)
> for the pointer-lifetime rule that all `Post()` callers must follow.

---

### 1.4 `IEventSubscriber`

*File: `src/core/event_bus.h`*

```cpp
class IEventSubscriber {
public:
  virtual ~IEventSubscriber() = default;
  virtual void OnEvent(const Event& event) = 0;
};
```

Implemented by `AppManager`, `DisplayService`, `BleService`, and `AudioService`.
Only the first two are actually subscribed.

---

### 1.5 `core/system.h`

*File: `src/core/system.h`*

Aggregation header that pulls in `app_manager.h` and `event_bus.h` and declares
the two core globals:

```cpp
extern AppManager app_manager;
extern EventBus   event_bus;
```

Services and drivers declare their own `extern` in their own headers — this
file does **not** aggregate them. `app_manager` itself is *defined* in
`app_manager.cpp`; `event_bus` is *defined* in `main.cpp`.

---

## 2. Shared Types

### 2.1 `AppId` — `src/common/event_types.h`

```cpp
enum class AppId : uint8_t { SystemUi, Lyrics, Music, Alarm, Settings, Clock, Count };
```

`Count` is both the sentinel and the registry size. Only `SystemUi`, `Lyrics`,
and `Clock` are registered by the current firmware.

### 2.2 `EventType` — `src/common/event_types.h`

```cpp
enum class EventType : uint8_t { InputEvent, DisplayRequest, AudioEvent, System, Count };
```

| Value | Subscribers today | Posted by |
|---|---|---|
| `InputEvent` | `app_manager` | `InputService` |
| `DisplayRequest` | `display_service` | `SystemUi`, `LyricsApp`, `StopwatchApp` |
| `AudioEvent` | none | nobody |
| `System` | none | constructed in `BleService::ProcessLine()` but never posted |

### 2.3 `InputType` / `InputEvent` — `src/common/input_types.h`

```cpp
enum class InputType : uint8_t {
  RotateLeft, RotateRight, Enter, Back,
  ButtonIncrement, ButtonDecrement
};
struct InputEvent { InputType type; };
```

Two families exist deliberately:

- **`ButtonIncrement` / `ButtonDecrement`** — produced by the physical
  increment/decrement buttons and by serial `+` / `-`. This is the primary
  navigation pair.
- **`RotateLeft` / `RotateRight`** — produced only by serial `l` / `r`.
  They model an encoder rotation and are accepted by every app as an
  alias, but nothing physical emits them today.

`Enter` and `Back` are produced by the ENTER and BACK buttons and by serial
`e` / `b`.

There is no repeat/long-press event; a held button emits exactly one press.

### 2.4 UI types — `src/common/ui_types.h`

```cpp
enum class TextSize  { Small, Medium, Large };
enum class TextAlign { Left, Center, Right };

enum class DisplayRequestType {
  None, ShowWord, ShowLine, ShowLines, ShowSongList,
  ShowAppMenu, ShowBigTime, ShowScreensaver, ClearDisplay
};
```

### 2.5 `DisplayRequest` — `src/common/event_types.h`

Payload of a `DisplayRequest` event. All pointer members must outlive the queued
event (see [architecture.md §3.1](architecture.md#31-the-pointer-lifetime-constraint-critical)).

| Field | Type | Used by |
|---|---|---|
| `type` | `DisplayRequestType` | selector |
| `text` | `const char*` | `ShowWord`, `ShowLine` |
| `lines` | `const char**` | `ShowLines`, `ShowSongList`, `ShowAppMenu` |
| `line_count` | `uint8_t` | `ShowLines`, `ShowSongList`, `ShowAppMenu` |
| `selected` | `uint8_t` | `ShowLines`, `ShowSongList`, `ShowAppMenu`. ≥ `line_count` disables the cursor. |
| `top_label` | `const char*` | `ShowBigTime` |
| `big_time` | `const char*` | `ShowBigTime` |
| `bottom_status` | `const char*` | `ShowBigTime` |
| `blink` | `bool` | `ShowBigTime` — hides the time while `true` |
| `text_size` | `TextSize` | `ShowWord`, `ShowLine`, `ShowLines` |
| `alignment` | `TextAlign` | `ShowWord`, `ShowLine`, `ShowLines`, `ShowBigTime` |
| `total_count` | `uint8_t` | `ShowSongList` — the `n / total` counter |
| `has_more_above` | `bool` | `ShowSongList` — draws `^` |
| `has_more_below` | `bool` | `ShowSongList` — draws `v` |
| `cat_state` | `uint8_t` | `ShowScreensaver` — current eye expression |
| `cat_base_state` | `uint8_t` | `ShowScreensaver` — mouth expression |
| `cat_y_offset` | `int8_t` | `ShowScreensaver` — vertical head shift (0 or −2) |
| `paw_offset` | `int8_t` | `ShowScreensaver` — look direction (−1, 0, +1) |

### 2.6 `Event` — `src/common/event_types.h`

```cpp
struct Event {
  EventType type = EventType::System;
  AppId     sender = AppId::Count;
  InputEvent        input;
  DisplayRequest    display_request;
};
```

A flat union-like container: only the member matching `type` is meaningful.
`sender` is the ownership key — `AppId::Count` means "from the system, not an
app", and is what `InputService` sets. Because `Event` carries both payloads it
is relatively large; the queue holds 16 of them.

### 2.7 `config` — `src/common/config.h`

| Symbol | Value |
|---|---|
| `config::pins::ButtonIncrement` | 26 |
| `config::pins::ButtonDecrement` | 25 |
| `config::pins::ButtonEnter` | 27 |
| `config::pins::ButtonBack` | 14 |
| `config::pins::OledSda` | 21 |
| `config::pins::OledScl` | 22 |
| `config::display::I2cAddr` | `0x3C` |
| `config::display::I2cAddrFallback` | `0x3D` |
| `config::kLyricsPathA` | `"/lyrics/Tumi.txt"` — **unused** |
| `config::kLyricsPathB` | `"/lyrics/Closer.txt"` — **unused** |

---

## 3. Drivers

### 3.1 `Oled` — `src/drivers/oled/oled.h`, `.cpp`

*Global: `oled`* — **not** an `IEventSubscriber`; driven only by `DisplayService`.

```cpp
enum class Font { Small, Medium, Large, LargeBold };
```

| Method | Signature | Notes |
|---|---|---|
| `Begin` | `bool Begin()` | Probes `0x3C` then `0x3D`; returns `false` (and the firmware halts) if neither ACKs. |
| `Width` / `Height` | `uint16_t …() const` | 128 / 64 |
| `Clear` | `void Clear()` | `u8g2.clearBuffer()` |
| `Update` | `void Update()` | `u8g2.sendBuffer()` — flushes the whole framebuffer |
| `SetFont` | `void SetFont(Font)` | Selects one of four faces |
| `DrawText` | `void DrawText(int16_t x, int16_t y, const char*)` | Baseline at `y` |
| `GetTextWidth` | `int16_t GetTextWidth(const char*)` | — |
| `GetTextHeight` | `int16_t GetTextHeight()` | ascent − descent |
| `GetAscent` | `int16_t GetAscent()` | baseline offset |
| `DrawLine` | `void DrawLine(x1, y1, x2, y2)` | — |
| `DrawRect` | `void DrawRect(x, y, w, h)` | Outline (`drawFrame`) |
| `FillRect` | `void FillRect(x, y, w, h)` | Solid (`drawBox`) |
| `SetDrawColor` | `void SetDrawColor(uint8_t)` | 1 = set, 0 = clear, 2 = XOR |
| `ProbeAddress` | `bool ProbeAddress(uint8_t)` *(private)* | I2C ACK check |

The U8g2 instance is file-static in `oled.cpp`.

### 3.2 `Button` — `src/drivers/button/button.h`, `.cpp`

No global instance; owned by `InputService`.

| Method | Signature | Notes |
|---|---|---|
| `Begin` | `void Begin(uint8_t pin)` | `INPUT_PULLUP`; seeds state from the current level |
| `Poll` | `void Poll()` | 20 ms debounce; latches a press edge |
| `WasPressed` | `bool WasPressed()` | Read-and-clear latch |

Private state: `pin_`, `raw_`, `debounced_`, `latched_`, `hold_until_`.
See [hardware.md §5](hardware.md#5-buttons) for the algorithm.

---

## 4. Services

### 4.1 `DisplayService` — `src/services/display_service.h`, `.cpp`

*Global: `display_service`*
*Implements: `IEventSubscriber`, subscribed to `EventType::DisplayRequest`*

| Method | Notes |
|---|---|
| `bool Begin()` | Delegates to `oled.Begin()`; sets `initialized_`. |
| `void OnEvent(const Event&)` | Drops non-`DisplayRequest` events, drops anything when `!initialized_`, and **drops any event whose `sender` is not the foreground app**. Otherwise dispatches on `req.type`. |

Renderers (all private, all take `const DisplayRequest&`):
`ShowWord`, `ShowLine`, `ShowLines`, `ShowSongList`, `ShowAppMenu`,
`ShowBigTime`, `ShowScreensaver`, `ClearDisplay`.

Helpers: `SetFont(TextSize)`, `CalculateX(text, align)`,
`CalculateCenteredY()`.

Layout details for each renderer are in
[applications.md §5](applications.md#5-screen-layouts).

> `ShowSongList` uses a function-local `static char counter_buf[8]` for the
> `n / total` counter. It is not re-entrancy safe, but rendering is single
> threaded.

### 4.2 `InputService` — `src/services/input_service.h`, `.cpp`

*Global: `input_service`* — **not** an `IEventSubscriber`; it polls and posts.

| Method | Notes |
|---|---|
| `bool Begin()` | Configures all four button pins. Always returns `true`. |
| `void Poll()` | Polls the four buttons; posts an `InputEvent` on each press edge. |
| `void PollSerialDebug()` | Consumes available serial bytes, posting the mapped event. |

Private: `PostInput(InputType)` builds an `Event` with
`type = InputEvent`, `sender = AppId::Count` and posts it.

Pin → event mapping:

| Button | Pin | Event |
|---|---|---|
| Increment | 26 | `ButtonIncrement` |
| Decrement | 25 | `ButtonDecrement` |
| ENTER | 27 | `Enter` |
| BACK | 14 | `Back` |

### 4.3 `StorageService` — `src/services/storage_service.h`, `.cpp`

*Global: `storage_service`* — thin LittleFS wrapper, **not** an
`IEventSubscriber`.

| Method | Signature |
|---|---|
| `Begin` | `bool Begin()` — `LittleFS.begin()`; idempotent |
| `Exists` | `bool Exists(const char* path) const` |
| `Open` | `File Open(const char* path, const char* mode) const` |
| `Remove` | `bool Remove(const char* path)` |
| `Rename` | `bool Rename(const char* old, const char* neu)` |
| `FileSize` | `size_t FileSize(const char* path) const` |
| `IsReady` | `bool IsReady() const` |

Every method short-circuits to a failure value when `!initialized_` or when a
pointer argument is `nullptr`. Currently only `Begin()` is called (from
`setup()`); no application uses the filesystem.

### 4.4 `BleService` — `src/services/ble_service.h`, `.cpp`

*Global: `ble_service`*
*Implements: `IEventSubscriber` — **never subscribed**, so `OnEvent()` never runs*

```cpp
using BleCommandCallback = void (*)(const char* command);
```

| Method | Signature | Behaviour |
|---|---|---|
| `Begin` | `bool Begin()` | Creates the GATT server, TX/RX characteristics, starts advertising. Always returns `true`. |
| `Stop` | `void Stop()` | Stops advertising and clears `initialized_`. Never called by the firmware. |
| `SendCommand` | `bool SendCommand(const char*)` / `bool SendCommand(const String&)` | Appends `\n`, sets the TX value, notifies. Returns `false` when disconnected. |
| `IsConnected` | `bool IsConnected() const` | — |
| `SetCommandCallback` | `void SetCommandCallback(BleCommandCallback)` | Single slot; overwritten on repeated calls. |
| `OnEvent` | `void OnEvent(const Event&) override` | Empty body. |

Internal (private, invoked by friend callback classes):
`HandleReceived(const uint8_t*, size_t)`, `ProcessLine(const char*)`.

State: `initialized_`, `connected_`, `command_callback_`, `tx_char_`,
`rx_char_`, `rx_buffer_[256]`, `rx_len_`.

`BleServerCallbacks` and `BleRxCallbacks` are file-local classes declared as
friends so they can reach the private members of the singleton.

### 4.5 `AudioService` — `src/services/audio_service.h`, `.cpp`

*Global: `audio_service`* — **stub**.

```cpp
class AudioService : public IEventSubscriber {
public:
  bool Begin() { return true; }
  void OnEvent(const Event&) override {}
};
```

`.cpp` contains only the global definition. Not subscribed; `Begin()` is called
from `setup()`. Audio playback is performed entirely by the PC companion over
BLE.

---

## 5. Applications

Detailed behaviour, screens, and state machines:
[applications.md](applications.md). Summary API:

### 5.1 `SystemUi` — `src/system_ui/system_ui.h`, `.cpp`

*Global: `system_ui`*

`GetAppId()` → `AppId::SystemUi`; `GetName()` → `""` (never appears in its own
menu).

Implements the full `IApp` interface. Public surface beyond `IApp` is nil;
everything else is private, including `PickNextExpression()`,
`ShowScreensaver()`, and `ShowMenu()`.

### 5.2 `LyricsApp` — `src/apps/lyrics/lyrics_app.h`, `.cpp`

*Global: `lyrics_app`*

`GetAppId()` → `AppId::Lyrics`; `GetName()` → `"Lyrics"`.

**BLE entry points called from `main.cpp::onBleCommand`:**

| Method | Triggered by |
|---|---|
| `void OnAudioStarted()` | `AUDIO_STARTED` |
| `void OnPlaybackEnded()` | `END` |
| `void OnLyricsEnd()` | `LYRICS_END` |
| `void OnSongListReceived(const char*)` | `SONGS\|…` (payload after the pipe) |
| `void OnTotalSongs(uint8_t)` | `TOTAL_SONGS\|<n>` |
| `void OnLyricsData(const char*)` | `LYRICS_DATA\|…` (one lyric line) |

**Constants**

| Name | Value | Meaning |
|---|---|---|
| `kMaxWords` | 1000 | Words held per song |
| `kMaxWordLength` | 32 | Bytes per word, including NUL |
| `kTitleTimeoutMs` | 1500 | How long the song title holds before lyrics render |
| `kWordDisplayTimeoutMs` | 5000 | Blank the screen after a word has been shown this long |
| `kHandshakeTimeoutMs` | 10000 | Wait for `AUDIO_STARTED` |
| `kSongRequestTimeoutMs` | 5000 | Wait for a BLE response |
| `kWindowSize` | 5 | Song-list rows kept in the scroll buffer |
| `kSongNameLen` | 32 | Bytes per buffered song name |

**Storage**

- `words_[1000]` — `{uint32_t timestamp; char word[32];}`
- `song_buffer_[5][32]` — the visible scroll window
- `visible_lines_[5]` — `const char*` row pointers passed to the display
  (member storage, required by the pointer-lifetime rule)
- `selected_index_`, `buffer_offset_`, `buffer_count_`, `total_songs_`

Private state machine, request helpers, transitions, playback control, display
helpers, and the parser are described in
[applications.md §3](applications.md#3-lyrics-app).

### 5.3 `StopwatchApp` — `src/apps/clock/stopwatch_app.h`, `.cpp`

*Global: `stopwatch_app`*

`GetAppId()` → `AppId::Clock`; `GetName()` → `"Timer"`.

**Constants**

| Name | Value |
|---|---|
| `kMinuteMs` | 60 000 |
| `kBlinkMs` | 500 (finish flash period) |
| `kDrawIntervalMs` | 200 (min redraw interval while running) |

**State:** `mode_` (`STOPWATCH` / `TIMER`), `running_`, `finished_`,
`blink_on_`, `timer_set_min_` (0…65535), `elapsed_ms_`, `remaining_ms_`,
`last_tick_ms_`, `last_draw_ms_`, `last_blink_ms_`, `time_buf_[12]`.

**Private methods:** `Start()`, `ResetAll()`, `IsReset()`, `Draw()`,
`FormatTime(buf, len, ms)`.

`FormatTime` emits `mm:ss`, switching to `h:mm:ss` once the hour is non-zero.

---

## 6. Program Entry — `src/main.cpp`

| Symbol | Role |
|---|---|
| `EventBus event_bus;` | Definition of the global bus (declaration is in `core/system.h`) |
| `void onBleCommand(const char*)` | The single BLE command dispatcher |
| `setup()` | Boot sequence — see [architecture.md §4.3](architecture.md#43-boot-sequence) |
| `loop()` | Four-step pump |

`onBleCommand` routes by prefix:

| Command | Action |
|---|---|
| `AUDIO_STARTED` | `lyrics_app.OnAudioStarted()` |
| `END` | `lyrics_app.OnPlaybackEnded()` |
| `LYRICS_END` | `lyrics_app.OnLyricsEnd()` |
| `SONGS\|…` | `lyrics_app.OnSongListReceived(cmd + 6)` |
| `TOTAL_SONGS\|…` | `atoi` then `lyrics_app.OnTotalSongs(n)` |
| `LYRICS_DATA\|…` | `lyrics_app.OnLyricsData(cmd + 12)` |
| `STOPPED`, `PAUSED`, `RESUMED`, `TIME_ACK\|…` | **empty branches — accepted and ignored** |
| anything else | ignored |

---

## 7. PC Companion — `pc_client/`

Python (3.10+, verified on 3.14) desktop client. It is not part of the
PlatformIO build; see
[build-and-test.md §6](build-and-test.md#6-pc-companion-client). The rules in
[conventions.md](conventions.md) govern the C++ firmware and do **not** apply
here — this code allocates freely and runs on the PC.

### 7.1 `protocol.py` — constants and framing

| Symbol | Value / role |
|---|---|
| `DEVICE_NAME` | `"BokaBaksho"` — the name `BleakScanner` searches for |
| `SERVICE_UUID`, `TX_UUID`, `RX_UUID` | mirrors `src/services/ble_service.h` |
| `WINDOW_SIZE` | `5` — the firmware's `kWindowSize` |
| `MAX_LINE_BYTES` | `200` — under the firmware's 256-byte `rx_buffer_` |
| `AUDIO_EXTENSIONS` | `.mp3 .wav .ogg .m4a .flac` |
| `split_command(line)` | → `(NAME_UPPER, rest)` |
| `parse_request_songs(rest)` | `"<offset>\|<count>"` → `(int, int)`, or `None` if malformed |
| `song_name_from(command)` | `PLAY\|name` / `LYRICS\|name` → `name` |
| `build_total_songs(n)` / `build_songs(names)` / `build_lyrics_data(line)` | outbound builders |

### 7.2 `audio.AudioPlayer`

`pygame.mixer` wrapper. `state` is one of `IDLE`, `LOADED`, `PLAYING`,
`PAUSED`, `STOPPED`.

| Member | Behaviour |
|---|---|
| `available` | `False` when the mixer could not be initialised (no audio device); every other method then no-ops |
| `load(path)` / `play_loaded()` / `play(path)` | buffer a track, then start it; return `bool` |
| `pause()` / `resume()` / `stop()` | state transitions; return `bool` |
| `is_playing` | `state == PLAYING` |
| `busy` | `pygame.mixer.music.get_busy()` — what the `END` monitor polls |
| `elapsed` | seconds since `started_at` |
| `shutdown()` | stop playback and close the mixer |

### 7.3 `ble.BLEAudioClient`

Owns a daemon thread running an asyncio loop; bleak lives there and nowhere
else. Inbound commands are dispatched to short-lived worker threads, so
streaming lyrics can never delay a `STOP`. Audio state changes are serialised
by `command_lock`; outbound writes by `send_lock` (acquired **inside**
`command_lock`, never around it).

| Member | Behaviour |
|---|---|
| `start()` | spawn the asyncio thread |
| `connect()` / `disconnect()` / `shutdown()` | link lifecycle, submitted to the loop with `run_coroutine_threadsafe` |
| `set_media_dir(path)` / `refresh_library()` | GUI-facing; `refresh_library()` re-pushes the library if connected |
| `send(msg, timeout)` / `send_lines(lines, timeout)` | newline-terminate, pack to `DEFAULT_PAYLOAD = 500` bytes, halve the batch when the link rejects it; refuse entirely when disconnected |
| `_on_notify()` | appends to a carry-over buffer and splits on `\n`, so a notification boundary never truncates a command |
| `_dispatch(line)` | routes `PLAY`, `PAUSE`, `RESUME`, `STOP`, `REQUEST_SONGS`, `LYRICS`, `END`; ignores PC-only and unknown commands |
| `_handle_request_songs(rest)` | clamps `count` to 5, slices `songs[offset:offset + count]`, stays **silent** when `offset` is out of range |
| `_handle_lyrics(name)` | streams `LYRICS_DATA\|…` for every non-empty line, then `LYRICS_END` |
| `_handle_play(name)` | resolve → `load()` → `play()` → `AUDIO_STARTED` → start the `END` monitor |
| `_push_library()` | `TOTAL_SONGS\|n` and `SONGS\|…` in **one** write |
| `_end_monitor(token)` | polls `busy`; on expiry sends `END` |
| `_songs()` / `_find_audio_file()` / `_find_lyrics_file()` | media-folder resolution, case-insensitive fallback |
| `log(msg)` / `push(kind, value)` | **the only** paths to the GUI — both enqueue |

### 7.4 `gui.CompanionApp`

| Member | Behaviour |
|---|---|
| `run(client, player, events)` | module entry — builds `Tk` and enters `mainloop()` |
| `DEFAULT_FOLDER` | `~/Music/SongWithLyrics` — the default media directory, **read only** |
| `_poll_events()` | `root.after(100, …)`; drains the queue, then reschedules itself |
| `_connect()` / `_disconnect()` | button handlers; visual state is restored by the `connection` event, not set locally |
| `_set_media_dir(path)` / `_load_song_list()` | points at a folder and rebuilds the listbox from `*.txt`. **Never creates the directory** — a missing one shows `folder not found` in the count label and one log line |
| `_set_song()` / `_set_playing()` / `_highlight_song()` | now-playing panel and the listbox selection |
| `_on_close()` | `WM_DELETE_WINDOW` → shut down the client, the player, then the window |

### 7.5 `main.py` and `selftest.py`

| Entry point | Role |
|---|---|
| `python -m pc_client` / `run_companion.py` | `main.run()` — builds player, client, GUI, and reports a missing dependency as a message box |
| `python -m pc_client.selftest` | 36 transport-stubbed checks; exit status 0 means all passed |
