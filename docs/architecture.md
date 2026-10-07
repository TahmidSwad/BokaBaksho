# BokaBaksho Architecture

## 1. Overview

BokaBaksho is a layered, event-driven firmware framework for a portable ESP32
device. It provides a scrollable app launcher, a uniform application lifecycle,
exclusive display ownership, and a clean separation between hardware drivers,
system services, and user-facing applications.

Two properties define the design:

- **Static allocation.** Every buffer and table is sized at compile time. There
  is no `new`, `malloc`, `std::vector`, or Arduino `String` in project code.
- **Single foreground owner.** Exactly one application is visible and receives
  input at any moment, determined by the navigation stack.

```
┌─────────────────────────────────────────────────────────────┐
│  App Layer     LyricsApp, StopwatchApp                      │
│                (each owns a state machine + its own UI)     │
├─────────────────────────────────────────────────────────────┤
│  SystemUI      Launcher: cat screensaver → app menu         │
├─────────────────────────────────────────────────────────────┤
│  Core          AppManager   (registry + navigation stack)   │
│                EventBus     (ring-buffer pub/sub)           │
│                IApp         (application contract)          │
├─────────────────────────────────────────────────────────────┤
│  Services      DisplayService   (foreground-only draw)      │
│                InputService     (buttons + serial debug)    │
│                StorageService   (LittleFS wrapper)          │
│                BleService       (GATT text protocol)        │
│                AudioService     (stub, unimplemented)       │
├─────────────────────────────────────────────────────────────┤
│  Drivers       Oled   (U8g2 SSD1306, hardware I2C)          │
│                Button (debounced active-low polling)        │
├─────────────────────────────────────────────────────────────┤
│  Common        config, event_types, input_types, ui_types   │
└─────────────────────────────────────────────────────────────┘
```

All long-lived objects are statically allocated globals with `extern`
declarations in their headers. `core/system.h` aggregates the two core
singletons (`app_manager`, `event_bus`); each service and driver declares its
own `extern` next to its class.

---

## 2. Data Flow

### 2.1 Input path

```
Hardware buttons ──> Button::Poll() ──> InputService::Poll()
                                              │
                                              ▼
                                      EventBus.Post(InputEvent)
                                              │
                                              ▼
                                     AppManager::OnEvent()
                                              │
                                              ▼
                                     RouteInput() ──> foreground IApp::HandleInput()
```

`InputService::PollSerialDebug()` offers an identical path driven by keyboard
characters, so the entire system is testable with no hardware attached.

### 2.2 Display path

```
App ──> EventBus.Post(DisplayRequest) ──> DisplayService::OnEvent()
                                                │
                                                ▼
                                        Ownership check:
                            app_manager.GetForeground()->GetAppId()
                                          == event.sender ?
                                                │
                                    yes ────────┤
                                                ▼
                                        render on the OLED
```

If the sender is not the foreground app the event is **silently dropped**.
There is no queueing, no error, and no retry — this is the whole of the display
exclusivity mechanism (see decision **D2** / **D10**).

### 2.3 BLE path

```
BLE GATT RX characteristic ──> BleService::HandleReceived()
                                        │  (accumulates into rx_buffer_[256])
                                        ▼
                                 BleService::ProcessLine()
                                        │
                          ┌─────────────┴─────────────┐
                          ▼                           ▼
              command_callback_(line)        (System event, currently
                          │                   constructed but not posted)
                          ▼
                main.cpp::onBleCommand()
                          │
                          ▼
             LyricsApp::On*() handlers  (set flags / parse data)
```

Inbound BLE data never posts to the `EventBus`. It invokes a single registered
C-function callback, which dispatches to `lyrics_app`. The app then sets flags
that its own `Update()` loop observes. This keeps BLE callback context
(the ESP32 BLE stack task) out of the rendering path.

### 2.4 The main loop

`loop()` in `src/main.cpp` is four lines and is the only pump in the system:

```cpp
void loop() {
  input_service.Poll();            // hardware  -> InputEvents
  input_service.PollSerialDebug(); // serial    -> InputEvents
  event_bus.Dispatch();            // drain the queue: render + route input
  app_manager.Update();            // pump the foreground app's Update()
}
```

Ordering matters: inputs posted this iteration are dispatched in the same
iteration, and the foreground app is updated immediately afterwards, so app
state changes are reflected on screen on the next pass.

---

## 3. EventBus Internals

`EventBus` (`src/core/event_bus.h`, `src/core/event_bus.cpp`) is a fixed-size
ring buffer with per-type subscriber lists.

```
EventBus
  ├── subscribers_[EventType::Count][4]   typed subscriber arrays
  ├── subscriber_count_[EventType::Count] live count per type
  ├── queue_[16]                          ring buffer of Event structs
  ├── queue_head_ / queue_tail_ / queue_full_
  └── Dispatch()                          drains queue, calls subscribers
```

| Constant | Value | Meaning |
|---|---|---|
| `kEventQueueSize` | 16 | Events that may be pending at once |
| `kMaxSubscribersPerType` | 4 | Subscribers per `EventType` |

Current subscriptions, wired in `setup()`:

| Event type | Subscribers |
|---|---|
| `EventType::DisplayRequest` | `display_service` |
| `EventType::InputEvent` | `app_manager` |
| `EventType::AudioEvent` | *(none)* |
| `EventType::System` | *(none)* |

`Post()` returns `false` when the queue is full and the event is **discarded**.
Most callers ignore this return value.

### 3.1 The pointer-lifetime constraint (critical)

`Post()` copies the `Event` struct by value into the queue, but any **pointer
field** inside it — `DisplayRequest::text`, `::lines`, `::top_label`,
`::big_time`, `::bottom_status` — is shallow-copied. The pointed-to data must
remain valid until `Dispatch()` runs.

> **Rule:** data referenced by a queued event must live in a **member variable**
> (or be a string literal). Never pass the address of a stack local.

This was the root cause of a black-screen bug fixed on 2026-09-15: a local
`const char* lines[6]` went out of scope before the event was dispatched, and
`DisplayService` dereferenced a dangling pointer. See `LyricsApp::visible_lines_`
and `SystemUi::menu_items_` for the correct pattern.

Because `Dispatch()` runs on the next `loop()` pass, the window is normally one
iteration — but the queue can hold up to 16 events, so the guarantee must be
treated as "valid for the lifetime of the posting object", not "valid for a few
milliseconds".

---

## 4. Navigation Stack

`AppManager` (`src/core/app_manager.h`, `src/core/app_manager.cpp`) maintains
two statically sized structures:

| Structure | Size | Contents |
|---|---|---|
| `apps_[6]` | `AppId::Count` | Registry of launchable apps, in registration order |
| `stack_[8]` | `kMaxDepth` | Navigation stack; `stack_[0]` is always `SystemUi` |

### 4.1 Operations

| Operation | Effect |
|---|---|
| `RegisterApp(app)` | Adds to the registry. Rejects `nullptr`, duplicates, and overflow. |
| `Boot(system_ui)` | Sets `stack_[0] = system_ui`, `depth_ = 1`. **Does not call `OnActivate()`** — the launcher draws itself from `Begin()`. |
| `LaunchApp(id)` | Looks up the app by id; no-ops if already on top; deactivates the current top, calls `target->OnActivate()`, and pushes. If `OnActivate()` returns `false`, the previous top is reactivated (rollback) and the push is abandoned. |
| `GoBack()` | Pops the top, deactivates it, reactivates the new top. Returns immediately when `depth_ <= 1` — the root can never be popped. |
| `GetForeground()` | `stack_[depth_ - 1]`, or `nullptr` before `Boot()`. |
| `Update()` | Calls `Update()` on the foreground app only. |

### 4.2 Input routing

```cpp
bool AppManager::RouteInput(const InputEvent& event) {
  IApp* top = GetForeground();
  if (top == nullptr) return false;
  if (top->HandleInput(event)) return true;   // consumed by the app

  if (event.type == InputType::Back) {        // unconsumed Back
    GoBack();                                 //   -> pop to the launcher
    return true;
  }
  return false;
}
```

Returning `false` from `HandleInput()` for a `Back` event is therefore the
standard way for an app to say "I want to exit". Returning `false` for other
event types has no effect beyond dropping the event.

### 4.3 Boot sequence

`setup()` in `src/main.cpp` runs in this exact order:

| # | Step | Failure behaviour |
|---|---|---|
| 1 | `Serial.begin(115200)` | — |
| 2 | `storage_service.Begin()` — mount LittleFS | **halts** (`while(true) delay(1000)`) |
| 3 | `display_service.Begin()` — probe + init OLED | **halts** |
| 4 | `input_service.Begin()` — configure 4 button pins | — |
| 5 | `audio_service.Begin()` — no-op | — |
| 6 | `ble_service.Begin()` — GATT server + advertising | — |
| 7 | `ble_service.SetCommandCallback(onBleCommand)` | — |
| 8 | `app_manager.RegisterApp(&lyrics_app)`, `(&stopwatch_app)` | — |
| 9 | `lyrics_app.Begin()`, `stopwatch_app.Begin()` | — |
| 10 | `event_bus.Subscribe(DisplayRequest → display_service)` | — |
| 11 | `event_bus.Subscribe(InputEvent → app_manager)` | — |
| 12 | `system_ui.Begin()` — draws the initial screensaver | — |
| 13 | `app_manager.Boot(&system_ui)` | **halts** on failure |
| 14 | prints `READY` | — |

Note that event subscriptions (step 10–11) happen *after* the app `Begin()`
calls (step 9). Events posted during step 9 are still queued and dispatched on
the first `loop()`, at which point the foreground is `SystemUi` — so any
`DisplayRequest` posted by `lyrics_app.Begin()` is dropped by the ownership
check. This is harmless: `LyricsApp::OnActivate()` re-issues its status screen
when the app is actually opened.

---

## 5. Application Lifecycle (`IApp`)

Every application — including the launcher — implements `IApp`
(`src/core/app_base.h`):

| Method | Return | When called |
|---|---|---|
| `Begin()` | `bool` | Once, during `setup()`, after registration |
| `OnActivate()` | `bool` | App becomes the top of the stack. Returning `false` aborts the launch and rolls back. |
| `OnDeactivate()` | `void` | App leaves the top of the stack |
| `Update()` | `void` | Every `loop()` iteration while the app is on top (has a default no-op body) |
| `HandleInput(e)` | `bool` | Input routed to this app. `true` = consumed. `false` for `Back` pops the stack. |
| `GetAppId()` | `AppId` | Constant identifier, also used for display ownership |
| `GetName()` | `const char*` | Label shown in the launcher menu |

Apps never touch the `Oled` driver directly and never draw synchronously. They
build a `DisplayRequest` and post it to the `EventBus`.

### 5.1 Application identifiers

```cpp
enum class AppId : uint8_t { SystemUi, Lyrics, Music, Alarm, Settings, Clock, Count };
```

| Value | Registered? | Class | Launcher label |
|---|---|---|---|
| `SystemUi` | booted as root, never registered | `SystemUi` | *(not shown — `GetName()` returns `""`)* |
| `Lyrics` | yes | `LyricsApp` | `Lyrics` |
| `Music` | no — reserved | — | — |
| `Alarm` | no — reserved | — | — |
| `Settings` | no — reserved | — | — |
| `Clock` | yes | `StopwatchApp` | `Timer` |

The registry size is `AppId::Count` (6) even though only two apps are
registered; unimplemented ids simply never enter the launcher menu.

---

## 6. Display Request Model

Apps describe *what* to draw; `DisplayService` decides *how*. The request type
lives in `common/ui_types.h` and the payload in `common/event_types.h`.

| `DisplayRequestType` | Payload used | Rendered by |
|---|---|---|
| `ShowWord` | `text`, `text_size`, `alignment` | Centred single word/phrase, screen cleared first |
| `ShowLine` | `text`, `text_size`, `alignment` | Centred single line (vertical centring) |
| `ShowLines` | `lines[]`, `line_count`, `selected` | Vertically centred block; `>` cursor at x=0 with 10 px indent when `selected < line_count` |
| `ShowSongList` | `lines[]`, `line_count`, `selected`, `total_count`, `has_more_above/below` | Top-anchored list with `>` cursor, `^`/`v` scroll arrows, and an `n / total` counter in the bottom-right |
| `ShowAppMenu` | `lines[]`, `line_count`, `selected` | Selected item centred in Large font, pagination dots along the bottom |
| `ShowBigTime` | `top_label`, `big_time`, `bottom_status`, `blink`, `alignment` | Small label at top, LargeBold time centred, small status at bottom; `blink` hides the time |
| `ShowScreensaver` | `cat_state`, `cat_base_state`, `cat_y_offset`, `paw_offset` | Vector-drawn animated cat face |
| `ClearDisplay` | *(none)* | Clears and flushes |
| `None` | *(none)* | Ignored |

Font sizes map to U8g2 faces as described in
[hardware.md](hardware.md#4-fonts).

---

## 7. BLE Protocol Summary

`BleService` runs a GATT server. Full specification:
[ble-protocol.md](ble-protocol.md).

- **Service** `12345678-1234-5678-9abc-def012345678`
- **TX characteristic** `...45679` (NOTIFY) — ESP32 → PC
- **RX characteristic** `...45680` (WRITE) — PC → ESP32
- **Framing:** newline-terminated plain text, both directions
- **Advertised name:** `BokaBaksho`, MTU requested 517

Incoming bytes accumulate in `rx_buffer_[256]` and are split on `\n` or `\r`.
A line that does not fit is silently truncated at 255 characters.

The PC half of this protocol lives in `pc_client/` (Python): a tkinter GUI, a
bleak client running on its own asyncio thread, and `pygame.mixer` for
playback. It is the only implementation of the PC side.
See [ble-protocol.md §5.2](ble-protocol.md#52-reference-implementation-pc_client)
and [build-and-test.md §6](build-and-test.md#6-pc-companion-client).

---

## 8. Filesystem

LittleFS is mounted unconditionally at boot and a failure is fatal. No
application currently reads or writes files:

```
LittleFS
└── (no project-managed files present in the repository)
```

`config.h` still defines `kLyricsPathA` and `kLyricsPathB`
(`/lyrics/Tumi.txt`, `/lyrics/Closer.txt`), but nothing references them —
lyrics are now streamed from the PC over BLE rather than read from flash.
See [current-state.md](current-state.md#3-known-issues--limitations).

Historically the repository shipped `data/lyrics/*.txt`, uploaded with
`pio run -t uploadfs`. That directory no longer exists.

---

## 9. Concurrency Model

There is no RTOS threading in application code: `setup()` and `loop()` run on
the Arduino loop task. The one exception is the ESP32 BLE stack, which invokes
`BLEServerCallbacks` and `BLECharacteristicCallbacks` from its own task.

Consequences handled in the code:

- `LyricsApp` marks its cross-context flags `volatile`
  (`audio_started_`, `playback_ended_`, `lyrics_received_`) so the loop task
  observes writes made by the BLE task.
- BLE callbacks only set flags, copy into member buffers, or call `Serial`.
  They never post display events and never touch the OLED.
- `BleService::HandleReceived()` performs byte-wise accumulation with no
  locking; a client disconnect resets `rx_len_` to 0 to avoid a torn frame.

---

## 10. Extending the System

Step-by-step instructions for adding an app or a service, including the
registration order pitfalls, are in
[conventions.md](conventions.md#5-adding-a-new-app) and
[conventions.md](conventions.md#6-adding-a-new-service).
