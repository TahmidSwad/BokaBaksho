# Roadmap

Planned and pending work, ordered by priority. Status detail for each item is
in [current-state.md](current-state.md).

---

## 1. Correctness (do first)

| # | Item | Reference |
|---|---|---|
| ~~1.1~~ | **Resolved 2026-10-08.** `Update()` now reads `load_failed_start_` (stamped at every transition into `LoadFailed`) and calls `ReturnToFileList()` after `kLoadFailedTimeoutMs` (1.5 s). `EnterWaitingSongs()` clears `song_request_start_` so a fresh wait cannot inherit a stale deadline | — |
| ~~1.2~~ | **Resolved 2026-10-03.** `LoadSelectedLyric()` now re-arms `song_request_start_` when it sends `LYRICS\|`, so the 5 s lyrics timeout starts at `ENTER` instead of at the song-list request. The member is still shared, but every request now arms it | — |
| ~~1.3~~ | **Resolved 2026-10-08.** `OnSongListReceived()` falls back to `total_songs_ = buffer_offset_ + added` when no `TOTAL_SONGS\|` has been seen, so the total is never 0 while a list is shown; the auto-advance modulo additionally refuses `total_songs_ == 0` and returns to the list instead | — |
| ~~1.4~~ | **Resolved 2026-10-08.** `OnSongListReceived()` captures `waiting_for_songs_` before clearing it — the only `LoadFailed` transition guarded by that flag is the song-list timeout — and calls `EnterFileList()` when a reply lands during `LoadFailed`. The list appears immediately instead of waiting out the 1.5 s auto-return and discarding a round trip already paid for | — |
| ~~1.5~~ | **Resolved 2026-10-08.** `config.h` and `input_service.h` now name the `InputType` each pin actually posts, and say that `RotateLeft`/`RotateRight` are serial-debug keys only | — |
| ~~1.6~~ | **Resolved 2026-10-08.** Line 71 now calls `setMaxPreferred(0x12)` instead of a second `setMinPreferred()`. The advertised range is the intended 7.5 ms … 22.5 ms, not 22.5 ms … 80 ms (the `BLEAdvertising()` default `max_interval = 0x40`) | — |
| 1.7 | Decide the fate of `kLyricsPathA/B` — delete, or restore a local-lyrics mode | [current-state §2](current-state.md#2-partially-implemented--reserved) |
| ~~1.8~~ | **Resolved 2026-10-08.** `RequestSongs()` records the offset it asked for in `requested_offset_`, and `OnSongListReceived()` commits it to `buffer_offset_` when (and only when) the reply arrives — so buffer and offset always describe the same window, and a timed-out request never claims an offset its stale buffer does not have | — |
| ~~1.9~~ | **Resolved 2026-10-08.** Auto-advance no longer loads a song whose window has not arrived yet: it sets `pending_load_`, and `OnSongListReceived()` loads once the reply lands. Previously it indexed past `buffer_count_` and did nothing, leaving `Playing` with `playing_ == false` permanently — bites as soon as the library exceeds 5 songs | — |

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
