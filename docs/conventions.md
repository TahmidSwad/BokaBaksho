# Conventions & Extension Guide

Coding standards, hard constraints, and step-by-step instructions for adding
new components to BokaBaksho.

---

## 1. Language and Toolchain

| Item | Value |
|---|---|
| Language | C++ (Arduino-flavoured dialect) |
| Standard library | **None** in project code — no `<vector>`, `<string>`, `<memory>`, no Arduino `String` |
| Build | PlatformIO, `espressif32` platform, `esp32doit-devkit-v1`, Arduino framework |
| Include path | `-Isrc`, so headers are included project-relative |

---

## 2. Naming

| Element | Convention | Example |
|---|---|---|
| Classes / structs | `PascalCase` | `AppManager`, `DisplayRequest` |
| Public methods | `PascalCase` | `RegisterApp()`, `GetForeground()` |
| Private members | `snake_case_` trailing underscore | `selection_`, `word_count_` |
| Local variables | `snake_case` | `buffer_offset`, `line_height` |
| Enums | `enum class`, `PascalCase` values | `InputType::ButtonIncrement` |
| Constants | `kPascalCase` | `kMaxWords`, `kEventQueueSize` |
| Globals | `snake_case` | `app_manager`, `event_bus`, `ble_service` |
| Files | `snake_case` | `app_manager.cpp`, `lyrics_app.h` |
| Directories | `snake_case` | `system_ui/`, `services/` |
| Namespaces | `PascalCase` or `snake_case` matching config | `config::pins`, `config::display` |
| Include guards | `FILE_NAME_H` | `APP_MANAGER_H` |

---

## 3. Includes

- Project headers use **quoted project-relative paths** rooted at `src/`:

  ```cpp
  #include "core/system.h"
  #include "common/event_types.h"
  #include "services/ble_service.h"
  ```

- System and Arduino/library headers use angle brackets:

  ```cpp
  #include <Arduino.h>
  #include <LittleFS.h>
  #include <U8g2lib.h>
  ```

- Every file includes what it uses directly; headers rely on include guards and
  do not include each other circularly.
- `core/system.h` is the convenience entry point for anything that needs
  `app_manager` or `event_bus`.

---

## 4. Constraints (non-negotiable)

### 4.1 Static allocation only

No `new`, `delete`, `malloc`, `free`, `std::vector`, or `std::string`
anywhere in `src/`. All tables, buffers, and queues are compile-time sized.

**Rationale:** ESP32 has limited RAM, no memory protection, and heap
fragmentation produces unpredictable runtime failures. Deterministic memory
use is a feature. See decision **D1**
([decisions.md](decisions.md#d1-static-allocation-only-no-dynamic-memory)).

**Sizes that must be respected:**

| Structure | Size | Defined in |
|---|---|---|
| App registry | `AppId::Count` (6) | `app_manager.h` |
| Navigation stack | 8 | `app_manager.h` `kMaxDepth` |
| Event queue | 16 | `event_bus.h` `kEventQueueSize` |
| Subscribers per event type | 4 | `event_bus.h` `kMaxSubscribersPerType` |
| BLE receive line | 256 | `ble_service.h` `kRxBufferSize` |
| Lyric words | 1000 | `lyrics_app.h` `kMaxWords` |
| Song scroll window | 5 × 32 | `lyrics_app.h` |

Growing any of these requires editing the constant — there is no automatic
resizing, and exceeding a limit is a silent truncation or a dropped event, not
an exception.

### 4.2 Single foreground owner

Only the app at the top of the navigation stack may draw, enforced by
`DisplayService::OnEvent()` comparing `event.sender` against
`app_manager.GetForeground()->GetAppId()`.

- Never draw from a background app "just this once".
- Never bypass the ownership check by calling `oled` directly from an app.
- There is no resource manager to acquire or release — the stack *is* the
  ownership record. See **D2** / **D10**.

### 4.3 Member storage for queued event payloads

Any pointer posted through the `EventBus` must remain valid until `Dispatch()`
processes the event. Use member variables or string literals; never stack
locals.

```cpp
// WRONG — lines dies when ShowList() returns
void MyApp::ShowList() {
  const char* lines[3] = {"a", "b", "c"};   // stack local
  event.display_request.lines = lines;
  event_bus.Post(event);
}

// RIGHT — member outlives the queued event
const char* visible_lines_[kVisibleLines];  // in the header
void MyApp::ShowList() {
  visible_lines_[0] = buffer_[0];
  event.display_request.lines = visible_lines_;
  event_bus.Post(event);
}
```

See [architecture.md §3.1](architecture.md#31-the-pointer-lifetime-constraint-critical).

### 4.4 Fatal boot failures

Storage and display initialisation failures halt the firmware
(`while (true) delay(1000);`) rather than continuing in a degraded state.
Preserve this behaviour for new subsystems whose absence makes the product
meaningless.

### 4.5 Comments

The codebase uses section banner comments (`// ===== TITLE =====`) and brief
explanatory comments where behaviour is non-obvious. Do not add narrative
comment blocks that duplicate what the code already says; put explanations in
`docs/`.

---

## 5. Adding a New App

1. **Add an `AppId` value** in `src/common/event_types.h`, *before* `Count`:

   ```cpp
   enum class AppId : uint8_t {
     SystemUi, Lyrics, Music, Alarm, Settings, Clock,
     Music,          // <-- example addition
     Count
   };
   ```

   Registry size and `SystemUi::kMaxMenuItems` both derive from `Count`, so
   they grow automatically. The nav stack depth (8) does not — check it if you
   expect deep nesting.

2. **Create the class** in `src/apps/<name>/<name>_app.h` and `.cpp`
   implementing `IApp`:

   ```cpp
   class MusicApp : public IApp {
   public:
     bool Begin() override;
     bool OnActivate() override;
     void OnDeactivate() override;
     void Update() override;
     bool HandleInput(const InputEvent& event) override;
     AppId GetAppId() const override { return AppId::Music; }
     const char* GetName() const override { return "Music"; }
   private:
     // state, helpers
   };
   extern MusicApp music_app;   // header
   ```

3. **Define the global** in the `.cpp`: `MusicApp music_app;`

4. **Register and initialise in `src/main.cpp`**, in this order:

   ```cpp
   app_manager.RegisterApp(&lyrics_app);
   app_manager.RegisterApp(&stopwatch_app);
   app_manager.RegisterApp(&music_app);   // registry order == menu order

   lyrics_app.Begin();
   stopwatch_app.Begin();
   music_app.Begin();

   event_bus.Subscribe(EventType::DisplayRequest, &display_service);
   event_bus.Subscribe(EventType::InputEvent, &app_manager);
   ```

   Registration happens *before* the `EventBus` subscriptions, so anything an
   app posts from `Begin()` is dispatched on the first `loop()` and will be
   dropped by the ownership check. Re-issue your initial screen from
   `OnActivate()`.

5. **Draw only through `DisplayRequest` events**, with `sender = GetAppId()`.
6. **Return `false` from `HandleInput()` for `Back`** to let the launcher take
   over; handle `Back` explicitly if you have an intermediate screen to unwind.
7. **Build and verify:** `pio run` — see
   [build-and-test.md](build-and-test.md#2-build-commands).

---

## 6. Adding a New Service

1. **Create** `src/services/<name>_service.h` / `.cpp` with a class, an
   `extern` declaration, and a `snake_case` global defined in the `.cpp`.
2. **Implement `IEventSubscriber`** only if the service reacts to events:

   ```cpp
   class FooService : public IEventSubscriber {
   public:
     bool Begin();
     void OnEvent(const Event& event) override;
   };
   extern FooService foo_service;
   ```

3. **Subscribe in `main.cpp`** if it should receive events. Watch the limit of
   **4 subscribers per event type** — `Subscribe()` returns `false` when full
   and the failure is easy to miss.
4. **Call `foo_service.Begin()`** in `setup()` in dependency order (storage →
   display → input → audio → BLE).
5. **Do not reach into other layers.** A service may use drivers; apps may use
   services. Drivers must not call services or apps.
6. **Keep hardware/RTOS contexts away from the display.** If a service is
   invoked from a callback outside `loop()`, set flags and let the main loop
   act on them — the pattern used by `BleService` and `LyricsApp`.

---

## 7. Adding a New Display Request Type

1. Add the enum value to `DisplayRequestType` in `src/common/ui_types.h`.
2. Add payload fields to `DisplayRequest` in `src/common/event_types.h` —
   remember that pointer fields are shallow-copied.
3. Add a renderer method to `DisplayService` (declare in the header, implement
   in the `.cpp`) and a `case` in `OnEvent()`.
4. Document the layout in [applications.md §5](applications.md#5-screen-layouts).

---

## 8. Error-Handling Style

- Functions that can fail return `bool`, `nullptr`, or a sentinel; there are no
  exceptions and no error objects.
- Callers either halt (boot-time, fatal) or show a non-blocking status screen
  and let the user navigate away.
- **Never block the main loop.** No `delay()` outside of a fatal halt, no
  busy-waiting on BLE or user input. Timers are `millis()`-based state checks
  inside `Update()`.
- Log meaningful transitions with `Serial.print`/`println` using a stable
  prefix (`BLE:`, `BLE TX:`, `LYRICS_ON`, `OLED I2C:`) so the console remains
  greppable.

---

## 9. Testing Expectations

- Any change must compile: `pio run`.
- Behavioural changes must be verifiable over the serial console using the
  debug keys (`l r e b + -`), so new apps should not require hardware-only
  paths to reach their screens.
- See [build-and-test.md](build-and-test.md) for the test suite's status.

---

## 10. Documentation Maintenance

When you change behaviour, update the affected document in the same change:

| Change | Update |
|---|---|
| New module, class, or public method | `docs/api-reference.md` |
| New app or changed input/screen behaviour | `docs/applications.md` |
| New or changed BLE command | `docs/ble-protocol.md` |
| Pin, address, or peripheral change | `docs/hardware.md` |
| New subsystem or data-flow change | `docs/architecture.md` |
| New constraint or pattern | `docs/conventions.md` |
| Status, bug, or limitation | `docs/current-state.md` |
| Deliberate design choice with trade-offs | `docs/decisions.md` |
| Anything user-visible | `docs/change-log.md` (dated entry) |
| New or changed build step | `docs/build-and-test.md` |
| Persistent project-wide rule | `README.md` |
