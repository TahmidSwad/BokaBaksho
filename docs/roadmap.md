# Roadmap

Planned and pending work, ordered by priority. Status detail for each item is
in [current-state.md](current-state.md).

---

## 1. Correctness (do first)

| # | Item | Reference |
|---|---|---|
| 1.1 | Give `LoadFailed` an auto-return timer, or an explicit dismissal, so an error screen cannot strand the user | [current-state §3.1](current-state.md#31-behavioural) |
| ~~1.2~~ | **Resolved 2026-10-03.** `LoadSelectedLyric()` now re-arms `song_request_start_` when it sends `LYRICS\|`, so the 5 s lyrics timeout starts at `ENTER` instead of at the song-list request. The member is still shared, but every request now arms it | — |
| 1.3 | Guard `total_songs_ == 0` before the auto-advance modulo | [current-state §3.1](current-state.md#31-behavioural) |
| 1.4 | Make `OnSongListReceived` recover from `LoadFailed` when a late reply arrives | [current-state §3.1](current-state.md#31-behavioural) |
| 1.5 | Fix or remove the stale "Increment → RotateRight" comments in `config.h` and `input_service.h` | [current-state §3.2](current-state.md#32-code-level) |
| 1.6 | Fix the BLE connection-interval setup (`setMinPreferred` called twice) | [current-state §3.2](current-state.md#32-code-level) |
| 1.7 | Decide the fate of `kLyricsPathA/B` — delete, or restore a local-lyrics mode | [current-state §2](current-state.md#2-partially-implemented--reserved) |
| 1.8 | Record the requested offset in `buffer_offset_` — the song-list window never advances, so scrolling past the 5th song loses the cursor and ENTER does nothing | [current-state §3.1](current-state.md#31-behavioural) |

---

## 2. Applications

| # | Item |
|---|---|
| 2.1 | **Music app** (`AppId::Music`) — library browsing and playback control beyond karaoke |
| 2.2 | **Alarm app** (`AppId::Alarm`) |
| 2.3 | **Settings app** (`AppId::Settings`) — brightness, screensaver timeout, BLE name |
| 2.4 | Word highlighting or scrolling lyrics instead of a single centred word |
| 2.5 | A progress indicator showing position within the current song |
| 2.6 | A visible way to skip tracks rather than relying on `END` auto-advance |
| 2.7 | Retry affordance for the 10 s playback handshake |

---

## 3. Platform

| # | Item |
|---|---|
| 3.1 | **`AudioService`** — real playback. Hardware TBD: I2S DAC, PCM5102/NS4168 amplifier module, or keep offloading to the PC. |
| 3.2 | Implement `TIME_SYNC` / `TIME_ACK` for PC↔device clock alignment, **or** remove them from the protocol documentation |
| 3.3 | Post the `EventType::System` event built in `BleService::ProcessLine()`, and give `System`/`AudioEvent` real subscribers — or delete the unused event types |
| 3.4 | Subscribe `BleService` to the `EventBus` and implement `OnEvent()`, or drop the `IEventSubscriber` interface from it |
| 3.5 | Call `BleService::Stop()` from a low-power path |
| 3.6 | Handle `EventBus::Post()` failures instead of ignoring the return value |
| 3.7 | Sleep / power-management mode (the screensaver currently keeps the OLED refreshing) |

---

## 4. Testing and tooling

| # | Item |
|---|---|
| 4.1 | Add a `[env:native]` environment to `platformio.ini` |
| 4.2 | Write the `EventBus` and `AppManager` unit tests promised by `test/README.md` |
| 4.3 | Provide an Arduino shim so `core/` headers compile on the host |
| 4.4 | Replace the machine-specific `build_pio.bat` / `scripts/do_build.ps1` with portable scripts, and remove or implement `scripts/run_build.ps1` |
| 4.5 | Run `pc_client/` against a real ESP32 over BLE — the reference client requested here now exists and passes its 36-check self-test, but has never been attached to hardware; also exercise the Windows and macOS builds |

---

## 5. Release engineering

| # | Item |
|---|---|
| 5.1 | OTA firmware updates (currently USB-only) |
| 5.2 | Version stamping in the firmware and in the BLE advertised payload |
| 5.3 | A CI job that at minimum runs `pio run` on every push |
