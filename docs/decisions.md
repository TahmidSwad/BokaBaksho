# Architectural Decision Records

Each record states the decision, why it was made, and what was rejected.
Decisions are numbered permanently; new decisions are appended.

---

## D1: Static allocation only (no dynamic memory)

**Chosen:** All data structures use compile-time-sized static arrays. No
`new`, `malloc`, `std::vector`, or heap allocation. Only C-strings
(`char[]`, `const char*`) and fixed arrays.

**Why:** ESP32 has limited RAM and no memory protection. Dynamic allocation
risks fragmentation and unpredictable failures. Static allocation makes memory
usage deterministic and eliminates leaks.

**Rejected:** `std::vector`, Arduino `String`, `new`/`delete`.

**Consequences:** Every capacity is a named constant that must be chosen up
front (`kEventQueueSize = 16`, `kMaxWords = 1000`, `kWindowSize = 5`, …).
Exceeding a limit is a silent truncation or a dropped event, never an
exception. See [conventions.md §4.1](conventions.md#41-static-allocation-only).

---

## D2: Navigation stack as the ownership mechanism

**Chosen:** Display ownership is implicit via the navigation stack. Only the
app at the top of the stack may draw, enforced by `DisplayService` checking
`event.sender == app_manager.GetForeground()->GetAppId()`.

**Why:** Simple, zero overhead. The nav stack already tracks which app is
visible, so a second source of truth would be redundant.

**Rejected:** An explicit `ResourceManager` with acquire/release semantics —
overkill for a single shared display.

---

## D3: Event bus with typed subscriber lists

**Chosen:** A single `EventBus` with a ring-buffer queue and per-`EventType`
subscriber arrays (max 4 subscribers per type, 16 events in the queue).

**Why:** Decouples publishers from consumers. Apps post events without knowing
who handles them. The ring buffer prevents unbounded growth.

**Rejected:** Direct function calls between components (tight coupling); an
observer pattern with dynamic registration (requires allocation).

**Consequences:** Pointers inside an event are shallow-copied, so payloads must
live in member variables (see
[architecture.md §3.1](architecture.md#31-the-pointer-lifetime-constraint-critical)),
and a full queue silently discards events.

---

## D4: SystemUi as the nav stack root

**Chosen:** The launcher is always at the bottom of the navigation stack and
can never be popped. Launching an app pushes on top; `Back` pops back to it.

**Why:** Guarantees there is always a foreground app and simplifies the `Back`
logic — an unconsumed `Back` in any app returns to the launcher.

**Rejected:** Treating the launcher as a special case outside the stack.

**Note:** `AppManager::Boot()` does **not** call `OnActivate()` on the root.
The launcher therefore draws its initial screen from `Begin()`, and only
subsequent returns (via `GoBack()`) invoke `OnActivate()`.

---

## D5: `IApp` interface for all applications

**Chosen:** Every application implements `Begin()`, `OnActivate()`,
`OnDeactivate()`, `Update()`, `HandleInput()`, `GetAppId()`, `GetName()`.

**Why:** Uniform treatment by `AppManager` — apps can be pushed and popped
without the manager knowing implementation details. `OnActivate()` returning
`false` gives every launch a clean rollback path.

**Rejected:** Inheriting from a base class with default behaviour; the
interface is intentionally minimal.

---

## D6: BLE as a text-based protocol

**Chosen:** Plain newline-terminated text commands over BLE GATT (TX notify /
RX write).

**Why:** Simple to parse, easy to debug in the serial monitor, and easy to
generate from a Python client. No binary serialization complexity.

**Rejected:** Binary protocol with length-prefixed frames — unnecessary
complexity for this command count.

**Scope correction (2026-10-03):** The original record listed
`TIME_SYNC|<ms>` as an implemented command. It is not — no code path sends it
and the `TIME_ACK` handler is empty. The implemented vocabulary is specified in
[ble-protocol.md](ble-protocol.md).

---

## D7: Separate InputService for button polling

**Chosen:** `InputService` owns all four `Button` objects, polls them every
loop iteration, and posts normalized `InputEvent` structs to the `EventBus`.
It also provides serial debug input (`l r e b + -`).

**Why:** Centralizes hardware input. Apps receive abstract `InputEvent`s, not
raw pin reads. Serial debug enables testing with no hardware attached.

**Rejected:** Each app polling its own buttons (scattered hardware access, no
normalization).

**Consequence:** Six `InputType` values exist. The `ButtonIncrement` /
`ButtonDecrement` pair is what the hardware emits; `RotateLeft` / `RotateRight`
are encoder aliases emitted only by serial but accepted everywhere, retained
for compatibility and for testing the alias path.

---

## D8: OLED I2C address probing

**Chosen:** At boot, `Oled::Begin()` probes `0x3C` first, then `0x3D` (common
clone fallback), logs the result, and returns `false` if neither ACKs.

**Why:** U8g2's `begin()` always reports success, so the driver must verify
presence itself. Some SSD1306 clones use `0x3D`. Probing at boot avoids
runtime failures; `DisplayService::Begin()` propagates the failure and the
system halts gracefully.

**Rejected:** Hardcoding one address — the probe costs ~2 ms at boot and no
runtime time.

---

## D9: Lyrics timestamp format

**Chosen:** `<mm:ss.ff>word` (e.g. `<01:23.45>hello`), parsed by
`LyricsApp::ParseWordTimestamp()`.

**Why:** Human-readable and easy to author manually. Hundredths-of-a-second
precision is sufficient for karaoke sync.

**Rejected:** Millisecond-only timestamps (`<83450>hello`) — less readable;
frame-based timestamps — unnecessary complexity.

---

## D10: No ResourceManager — display exclusivity via the nav stack

**Chosen:** `DisplayService` checks the sender against the foreground app
before rendering. No separate resource tracking.

**Why:** With a single shared display, the nav stack already records which app
is visible. A `ResourceManager` would duplicate it.

**Rejected:** Explicit lock/unlock — adds a subsystem and API surface for no
benefit. (This is the same conclusion as D2, recorded separately because it was
originally argued as a resource-management question.)

---

## D11: Song library and lyrics streamed from the PC, not stored in flash

**Chosen:** The device holds no lyric files and no music. The PC companion
sends the song list (`TOTAL_SONGS` / `SONGS`) and streams timestamped lyrics
(`LYRICS_DATA` / `LYRICS_END`) over BLE on demand.

**Why:** The ESP32 has no audio output of its own; playback already happens on
the PC. Storing lyrics locally would duplicate the PC's library, require a
filesystem update path for every new song, and still not solve playback.
Streaming keeps the device stateless: `total_songs_`, a 5-row name window, and
one 1000-word table.

**Rejected:** Keeping `data/lyrics/*.txt` on LittleFS and having the device
read them directly (the original design, still referenced by the dead
`kLyricsPathA/B` constants) — forces a reflash for every library change and
leaves the device without a source of truth for the song list.

**Consequences:** The device cannot function without a connected PC, and a
library change on the PC requires no device action at all. The 5-row window
plus `REQUEST_SONGS|<offset>|5` keeps memory bounded regardless of library
size.

---

## D12: BLE callbacks set flags; the main loop acts on them

**Chosen:** `BleService` invokes one registered `BleCommandCallback` with each
complete line. `main.cpp::onBleCommand()` routes by prefix to `LyricsApp`
methods that only parse into member buffers or set `volatile` flags. `Update()`
observes those flags on the next loop pass.

**Why:** BLE callbacks arrive on the ESP32 BLE stack's own FreeRTOS task.
Touching the OLED or posting display events from that context would race with
the render path. Flags are the smallest correct handoff, and they need no
allocation.

**Rejected:** Posting BLE data as `EventBus` events — the bus is not
thread-safe, and it would still need a place to put arbitrarily long lyric
lines. A typed per-command subscriber list — four subsystems is far more
machinery than one callback.

**Consequences:** Responses are never instantaneous (they wait for the next
`loop()`), which is irrelevant at these timescales. The `volatile` qualifiers
on `audio_started_`, `playback_ended_`, and `lyrics_received_` are load-bearing
and must not be removed.

---

## D13: Screensaver as the launcher's entry screen

**Chosen:** `SystemUi` boots into an animated cat screensaver. Any button
reveals the app menu; `BACK` from the menu returns to the screensaver with the
cat in a Sleepy expression.

**Why:** The device is a pocket object that sits idle most of the time. A static
menu burns OLED pixels and current indefinitely, and the OLED is the single
most fragile component in the build. A low-duty-cycle animation with random
expression changes (6–12 s hold) alternating with long neutral phases (15–25 s)
keeps the display alive without the same static-image risk.

**Rejected:** Booting straight into the menu — no idle screen, and the
navigation stack would have no natural "home" state distinct from the menu.

**Consequences:** Two input layers: the first press always wakes rather than
selecting, and `BACK` at the menu root means "sleep", not "exit". The
launcher's `OnDeactivate()` is intentionally empty so the menu selection
survives a round trip into an app.

---

## D14: State machines inside apps, driven by `Update()`

**Chosen:** Each app owns an explicit `enum class State` and a private
`Enter*()` transition helper. `Update()` runs the per-state logic and the
timeout checks; `HandleInput()` dispatches by state.

**Why:** The main loop must never block, so every wait — BLE replies, the
playback handshake, error timeouts — has to be a non-blocking time check in
`Update()`. Centralising transitions in `Enter*()` helpers keeps state
invariants (flag clearing, timers, initial screen) in one place instead of at
every call site.

**Rejected:** Blocking `delay()`/`while` waits (freezes the display and input);
callback-driven transitions (moves state changes into the BLE task).

**Consequences:** Some states are entered by direct assignment
(`state_ = State::LoadFailed`) rather than an `Enter*()` helper, which is where
the current `LoadFailed` no-auto-return bug lives — see
[current-state.md §3.1](current-state.md#31-behavioural).
