# Change Log

Newest entries first. All dates are the date the change was made.

---

## 2026-10-08 — BLE connection interval + stale input comments

Two small firmware defects from
[current-state.md §3.2](current-state.md#32-code-level).

- **Connection interval (roadmap 1.6).** `BleService::Begin()` called
  `setMinPreferred(0x06)` and then `setMinPreferred(0x12)`, with comments
  labelling them "min" and "max". Both write the *same* field —
  `esp_ble_adv_data_t::min_interval` — so the second overwrote the first and
  `max_interval` was never touched, staying at the `BLEAdvertising()`
  constructor default of `0x40`. The device was advertising a preferred slave
  connection interval range of **22.5 … 80 ms** instead of the intended
  **7.5 … 22.5 ms** (units of 1.25 ms). Line 71 now calls
  `setMaxPreferred(0x12)`. Both fields reach the air because
  `BLEAdvertising::start()` passes `&m_advData` to
  `esp_ble_gap_config_adv_data()`.
- **Stale input comments (roadmap 1.5).** `config.h` and `input_service.h`
  claimed "Increment button → RotateRight". `InputService::Poll()` posts
  `ButtonIncrement` / `ButtonDecrement`; `RotateLeft` / `RotateRight` come only
  from `PollSerialDebug()`'s serial `l` / `r` keys. Both comments now name the
  `InputType` each pin produces.
- **Verified:** `pio run` SUCCESS — RAM 23.9% (78,440), Flash 38.7%
  (1,216,545), **+16 bytes**. **Not yet flashed** — no on-device retest, and
  the connection-interval effect cannot be confirmed without a sniffer or a
  host that reports the negotiated interval.
- **Docs:** `hardware.md` §5, §6; `ble-protocol.md` §1, §6;
  `current-state.md` §1.7, §3.2; `roadmap.md` 1.5, 1.6.

---

## 2026-10-08 — LyricsApp: five correctness fixes

Five defects fixed together: four from
[current-state.md §3.1](current-state.md#31-behavioural), plus one found while
fixing them (roadmap 1.9). The scroll and auto-advance defects only bite once
the library exceeds 5 songs; the auto-return bites on any failed request.

- **The song-list window now scrolls (roadmap 1.8).** `buffer_offset_` was
  written exactly once (`= 0`) and never again, so buffer and offset disagreed
  as soon as the window moved: past the 5th song the cursor disappeared,
  `ENTER` did nothing, and the `^` arrow could never appear. `RequestSongs()`
  records the offset it asked for in a new `requested_offset_` member, and
  `OnSongListReceived()` copies it into `buffer_offset_` **when the reply
  arrives** — committing them together means a timed-out request can never
  claim an offset its stale buffer does not have.
- **`LoadFailed` auto-returns (roadmap 1.1).** `load_failed_start_` was stamped
  at all six transitions into `LoadFailed` but never read, so an error screen
  was permanent while `Enter` and the navigation buttons were swallowed —
  `BACK` was the only exit. `Update()` now reads it and calls
  `ReturnToFileList()` after `kLoadFailedTimeoutMs` (1500 ms), a constant
  documented since 2026-09-15 that had never actually existed.
  `EnterWaitingSongs()` also clears `song_request_start_`, otherwise a fresh
  wait inherited the previous request's deadline, re-entered `LoadFailed`
  immediately, and turned the auto-return into a flicker loop.
- **No more modulo by zero (roadmap 1.3).** `OnSongListReceived()` corrected
  `total_songs_` only when a reply held fewer than 5 names, so a client sending
  exactly 5 names without `TOTAL_SONGS|` left it at 0 — making
  `total_songs_ - 1` underflow to 255 while scrolling and making auto-advance
  compute `% 0` (undefined behaviour). It now falls back to
  `buffer_offset_ + added`, a lower bound the window actually proves, and
  auto-advance additionally refuses `total_songs_ == 0` and returns to the list.
- **Auto-advance waits for its window (roadmap 1.9).** When the next song fell
  outside the 5-row window the old code fired `REQUEST_SONGS` and then called
  `LoadSelectedLyric()` *immediately*, which indexed past `buffer_count_` and
  returned without doing anything — leaving `Playing` with `playing_ == false`,
  so `Update()` hit `if (!playing_) return;` forever while the arriving `SONGS`
  reply was ignored. Playback therefore stopped silently at the window
  boundary. A new `pending_load_` flag defers the load to
  `OnSongListReceived()`, falling back to `ReturnToFileList()` if the fresh
  window still misses the target; `EnterWaitingSongs()` and `EnterFileList()`
  clear it so an abandoned request can never start a song when a late reply
  lands.
- **A late reply recovers from `LoadFailed` (roadmap 1.4).** The song-list
  timeout is the only transition into `LoadFailed` guarded by
  `waiting_for_songs_`, so `OnSongListReceived()` now captures that flag before
  clearing it; when it is still set, the reply calls `EnterFileList()`
  immediately. Previously the reply was dropped and the "No device" screen held
  for the rest of `kLoadFailedTimeoutMs` — about a device that had just spoken
  — discarding a round trip already paid for. Lyric data and `AUDIO_STARTED`
  arriving late are still ignored, deliberately.
- **Verified:** `pio run` SUCCESS — RAM 23.9% (78,440), Flash 38.7%
  (1,216,601), **+352 bytes**. **Not yet flashed** — there has been no
  on-device retest.
- **Docs:** `current-state.md` §1.5, §1.7, §3.1, §3.3, §4; `roadmap.md`
  1.1/1.3/1.4/1.8/1.9; `applications.md` §3.2–§3.8; `api-reference.md` §5.2;
  `ble-protocol.md` §4.3; `decisions.md` D14.

---

## 2026-10-08 — Project renamed from *Boka_Baksho* to *BokaBaksho*

- **Renamed the project** so that the project, the repository, the local
  checkout, the GUI window title, and the BLE advertised name all agree on
  **`BokaBaksho`** — no space, no underscore.
- **Scope:** 28 occurrences in 21 tracked files — the README title and prose,
  every `docs/` heading and intro line, the Python module docstrings, the
  `selftest.py` banner, build-script comments, `requirements.txt`, and the
  `src/services/ble_service.h` header comment. **No code was renamed:**
  nothing used `Boka_Baksho` as an identifier — no imports, no module names,
  no symbols.
- **`platformio.ini`** gained `[platformio] name = BokaBaksho`, so the
  firmware project name no longer falls back to the directory name.
- **Unchanged by design:** the BLE advertised name `BokaBaksho`, the window
  title `BokaBaksho Companion`, the executable `BokaBakshoCompanion`, the
  Python package `pc_client`, the board env `esp32doit-devkit-v1`, and the
  media folder `~/Music/SongWithLyrics`.
- **Renamed the local checkout** to
  `~/Desktop/Projects/HobbyProjects/BokaBaksho` and **the GitHub repository**
  to `TahmidSwad/BokaBaksho`, with `origin` re-pointed at the new URL
  (GitHub redirects the old one, but the remote no longer depends on it).
- **Regenerated after the directory move** — all of these embed absolute
  paths: `.venv` (entry-point scripts carry absolute shebangs; `.venv/bin/pip`
  failed with `bad interpreter` until it was recreated), `.pio/`, and
  `.vscode/{c_cpp_properties,launch}.json`; `dist/BokaBakshoCompanion` was
  rebuilt. `README.md` and `build-and-test.md` §6.1 now invoke pip as
  `.venv/bin/python -m pip`, which survives a directory move.
- **Docs:** `README.md` records both renames; the `docs/index.md` naming
  convention line collapsed into a single spelling; the historical entries
  above rewritten to the current spelling. One intentional holdout remains —
  `README.md:11` still names `*Boka_Baksho*`, because that line *is* the
  rename history.
- **Verified after the move:** `pio run` SUCCESS — RAM 23.9% (78,440), Flash
  38.7% (1,216,249), byte-identical to the pre-rename build;
  `python -m pc_client.selftest` → 36/36; link check 98 links, 0 broken;
  `pio project config` reports `name = BokaBaksho`; `git ls-remote origin`
  reaches the renamed repository; `dist/BokaBakshoCompanion` rebuilt at the
  new path.

---

## 2026-10-04 — PC companion: default media folder, and it never creates one

- **Changed** `gui.DEFAULT_FOLDER` from `~/BokaBaksho` to
  `~/Music/SongWithLyrics` (the user's actual library).
- **Bug.** `_set_media_dir()` called `os.makedirs(path, exist_ok=True)` and ran
  at startup, so the first launch created an **empty** `~/BokaBaksho`. An empty
  folder has no `.txt` files, so `_songs()` returned `[]` and the device was
  never sent a `SONGS` write — on the ESP32 side that reads as a failed library
  hand-off, indistinguishable from a BLE fault. (Reproduced: `~/BokaBaksho`
  existed, contained nothing, and the user's real library had been reached only
  by pressing **Browse…**.)
- **Fix:** removed the `os.makedirs` call; the app now only reads. A missing
  path shows `folder not found` in the song-count label plus one log line,
  `_songs()` logs `Media folder does not exist: …`, and `_push_library()`
  sends nothing — it still never writes an empty `SONGS|`.
- **Tests:** two new self-test checks — the default resolves to
  `~/Music/SongWithLyrics`, and no `pc_client` module contains
  `os.makedirs` / `os.mkdir` / `.mkdir(` (`selftest.py` itself is skipped: it
  holds the needle strings and legitimately uses `tempfile.mkdtemp` for its own
  fixture in the system temp directory). **34 → 36 checks.**
- **Verified:** `python -m pc_client.selftest` → 36/36; `pio run` succeeds
  (unchanged firmware); GUI run against the real folder lists `2 songs`, and
  pointing it at a nonexistent path left it nonexistent on disk and produced no
  `SONGS` write; `./build_client.sh` rebuilt `dist/BokaBakshoCompanion`, whose
  embedded `pc_client.gui` constants contain `~/Music/SongWithLyrics` and no
  `~/BokaBaksho`; the packaged window opened and created no directory.
- **Cleanup:** removed the empty `~/BokaBaksho` the old code had created.
- **Docs:** `README.md` (ASCII sketch + media-folder paragraph),
  `build-and-test.md` §6.1, `api-reference.md` §7.4 (`DEFAULT_FOLDER` row and
  the `_set_media_dir` row), `current-state.md` §1.8, and the check count in
  `README.md`, `build-and-test.md`, `api-reference.md`, `current-state.md`,
  `roadmap.md` §4.5, and this file.

---

## 2026-10-03 — Fix: lyric timeout measured from the song list, not from `ENTER`

- **Bug.** `LyricsApp::song_request_start_` had exactly one writer,
  `RequestSongs()`. `LoadSelectedLyric()` never armed it, yet the `Loading`
  state used it as its own 5 s timeout — so the lyric timeout was measured
  from the last `REQUEST_SONGS` rather than from `ENTER`.
  With a library of ≤5 songs, or without scrolling out of the 5-row window,
  `RequestSongs()` fires only once, at first open. After that first 5 s window
  closed, **every** subsequent `ENTER` failed instantly with `LoadFailed` /
  `No device`, and re-opening the app did not re-arm it (`OnActivate()` takes
  `EnterFileList()` whenever `total_songs_ > 0`). Because `OnLyricsData()`
  ignores everything outside `State::Loading`, the client's lyric reply was
  discarded even when it arrived in time.
- **Reproduced on hardware:** selection failed consistently unless `ENTER` was
  pressed within 5 s of the list appearing, and succeeded when it was.
- **Fix:** one line in `LoadSelectedLyric()` —
  `song_request_start_ = millis();` after `LYRICS|` is sent. The 5 s window now
  starts at `ENTER`, which also covers playlist auto-advance (it reaches
  `LoadSelectedLyric()` too).
- **Verified:** `pio run` succeeds — RAM 23.9% (78,440), Flash 38.7%
  (1,216,249).
- **Not yet verified:** the on-device retest. The fix is built but **not yet
  flashed and confirmed** by the user.
- **Docs:** roadmap 1.2 resolved; the bullet removed from
  [current-state.md §3.1](current-state.md#31-behavioural) and its §4 pending
  list; `ble-protocol.md` §4.3 and `applications.md` §3.5 now describe the two
  5 s windows separately; `lyrics_app.cpp` line-number citations in
  `current-state.md` updated for the inserted lines.

---

## 2026-10-03 — PC companion client (`pc_client/`)

- **Added** a Python/tkinter PC companion in `pc_client/` — the PC half of the
  karaoke system, which the device needs because the ESP32 has no audio
  hardware. Modules: `protocol.py` (UUIDs, framing, builders), `audio.py`
  (`pygame.mixer` state machine plus an end-of-song `END` monitor), `ble.py`
  (dedicated asyncio thread, one worker thread per inbound command,
  serialised writes), `gui.py` (minimal window), `main.py` (wiring), and
  `selftest.py` (36 transport-stubbed checks).
- **Added** `run_companion.py` (entry point), `requirements.txt`,
  `build_client.sh` and `build_client.bat` (PyInstaller `--onefile
  --windowed`), and gitignored `.venv/`, `build/`, `dist/`, `*.spec`.
- **Deliberate differences from the reference implementation the GUI was based
  on:** lyric lines are packed into MTU-sized writes instead of one command per
  round trip; `TOTAL_SONGS` and `SONGS` are sent in a single write so they
  cannot be reordered; an empty `SONGS|` is never sent (the firmware would fold
  `total_songs_` down to 0); the unused `AUDIO_READY` round trip was dropped;
  the library is pushed after connecting only if the device has not already
  sent `REQUEST_SONGS`.
- **Recorded a fourth firmware defect** in
  [current-state.md §3.1](current-state.md#31-behavioural): `buffer_offset_` is
  assigned exactly once and never updated to the requested offset, so the
  song-list window cannot scroll past its first 5 entries — the cursor
  disappears and `ENTER` does nothing. It cannot be worked around from the PC
  side. **Not fixed** — firmware defects remain deferred. *(Fixed
  2026-10-08 — see the entry at the top of this file.)*
- **Documentation:** new [§6 in `build-and-test.md`](build-and-test.md#6-pc-companion-client)
  (run, build, self-test, dependencies, what is *not* verified),
  [§5.2 in `ble-protocol.md`](ble-protocol.md#52-reference-implementation-pc_client)
  mapping the reference client against the requirement list, a PC Companion
  section in `README.md`, and roadmap items 1.8 and 4.5.
- **Verified:** `python -m pc_client.selftest` → 36/36; `pio run` succeeds
  (RAM 23.9%, Flash 38.7%); PyInstaller build succeeds and the packaged window
  opens. **Not verified:** any BLE session against real hardware.

---

## 2026-10-03 — Documentation overhaul and project rename

- **Project renamed from *BoomBox2* to *BokaBaksho*.** All documentation now
  uses the new name. The BLE advertised device name was already `BokaBaksho`
  and is unchanged; the stale `// BoomBox2 lyric synchronization system`
  comment in `src/services/ble_service.h` was corrected.
- **Rewrote `AGENTS.md` as an agent-only working-instructions file.** All of
  its project knowledge was moved out into proper documentation:
  conventions and extension rules → `docs/conventions.md`, build/flash/test
  instructions → `docs/build-and-test.md`, architecture →
  `docs/architecture.md`, file/module inventory → `docs/api-reference.md`.
  What remains in `AGENTS.md` is only the content that cannot live in project
  documentation: session bootstrapping, the obligation to update docs after a
  change, the build-verification step, and the directive never to overstate
  implementation status. It now links to `docs/` instead of restating it.
- **Added** `docs/index.md` (documentation index), `docs/hardware.md`
  (pins, display, fonts, buttons, BLE radio, electrical summary),
  `docs/api-reference.md` (every module, class, method, and shared type),
  `docs/applications.md` (per-app screens, controls, and state machines),
  `docs/ble-protocol.md` (complete GATT and command specification),
  `docs/conventions.md` (coding standards, constraints, how to extend),
  `docs/build-and-test.md` (build, flash, serial debug, test status),
  `docs/roadmap.md` (prioritised pending work).
- **Rewrote** `README.md`, `docs/architecture.md`, `docs/current-state.md`.
- **Extended** `docs/decisions.md` with D11–D14 (PC-streamed lyrics library,
  flag-based BLE handoff, screensaver entry screen, `Update()`-driven state
  machines) and corrected the scope of D6 (`TIME_SYNC` is not implemented).
- **Documentation corrected against the source.** The previous docs described
  a LittleFS-based lyrics flow that no longer exists; the BLE protocol section
  was missing `REQUEST_SONGS`, `LYRICS`, `LYRICS_DATA`, `LYRICS_END`, `SONGS`,
  `TOTAL_SONGS`, and `END`; the launcher's cat screensaver and the
  `AppId::Clock` → `Timer` registration were undocumented.
- **Recorded verified issues** in `docs/current-state.md`, including: no
  auto-return from `LoadFailed`, the stale `song_request_start_` timeout, the
  `total_songs_ == 0` modulo-by-zero path, dead `kLyricsPathA/B` constants,
  the double `setMinPreferred()` call, and the non-existent test suite.
- **`test/README.md`** renamed the project to *BokaBaksho* and now states
  plainly that no test files and no `native` environment exist.
- **Verified:** `pio run` succeeds — RAM 23.9%, Flash 38.7%.

---

## 2026-09-15 — Lyrics UI: selection indicator + non-blocking error states

> **Superseded in part, then reinstated.** The 1500 ms auto-return described
> below was **absent** from the code from 2026-09-16 until 2026-10-08 —
> `LyricsApp`'s `LoadFailed` state had no timer and waited for `BACK`. The
> constant `kLoadFailedTimeoutMs = 1500` and the auto-return were restored on
> 2026-10-08 (roadmap 1.1), so the claim below is true again. The entry is kept
> as a record of the change as made.

- **`DisplayService::ShowLines`** now renders a `>` cursor before the selected
  line when `req.selected < req.line_count`. Lines are left-aligned with a
  10 px indent for the cursor.
- **`LyricsApp::ShowFileList`** passes `selected = selected_file_index_ -
  list_scroll_` so the current song is visually marked.
- **LoadFailed state** added: replaces a blocking `delay(1000)` in
  `LoadSelectedLyric` and `Update` with a non-blocking timer
  (`kLoadFailedTimeoutMs = 1500ms`). Any button press during LoadFailed
  returns to the file list immediately.
- **Handshake timeout** also made non-blocking (was `delay(1000)`, now uses the
  `LoadFailed` state).
- **`LoadLyrics`** gained serial logging: filename being parsed, word count,
  and a warning when zero words are found.

---

## 2026-09-15 — Refactor input: ButtonIncrement/ButtonDecrement as primary navigation

- **`InputService`** now maps hardware increment → `ButtonIncrement`,
  decrement → `ButtonDecrement` (was `RotateRight`/`RotateLeft`). Serial debug:
  `+`/`-` post `ButtonIncrement`/`ButtonDecrement`; `l`/`r` post
  `RotateLeft`/`RotateRight`.
- **`SystemUi`** and **`LyricsApp`** handle both pairs for navigation.
- **Bug fix:** `HandlePlaybackInput` and the Loading/WaitingHandshake states
  now return `false` for unconsumed events instead of `true`, allowing the
  `AppManager` to handle them (e.g. `Back` fallback to the launcher).

---

## 2026-09-15 — Fix dangling pointer in `LyricsApp::ShowFileList()`

- **Bug:** `ShowFileList()` allocated a local `const char* lines[6]` on the
  stack and stored its address in a `DisplayRequest`. When the function
  returned, the stack memory was reclaimed while the queued event still held
  the pointer. `DisplayService::ShowLines()` then dereferenced this dangling
  pointer, causing undefined behaviour (crash/reboot, black screen, or garbage
  display).
- **Fix:** Promoted `lines` to a member variable `visible_lines_[kVisibleLines]`
  so the pointer remains valid for the lifetime of the app.
- **Files:** `src/apps/lyrics/lyrics_app.h`, `src/apps/lyrics/lyrics_app.cpp`.

---

## 2026-09-15 — Project context established

- Created `AGENTS.md`, `docs/architecture.md`, `docs/decisions.md`
  (D1–D10), `docs/current-state.md`, and `docs/change-log.md`.
- Superseded on 2026-10-03 by the documentation overhaul; `AGENTS.md` was
  removed.
