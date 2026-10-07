# Change Log

Newest entries first. All dates are the date the change was made.

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
  side. **Not fixed** — firmware defects remain deferred.
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

- **Project renamed from *BoomBox2* to *Boka_Baksho*.** All documentation now
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
- **`test/README.md`** renamed the project to *Boka_Baksho* and now states
  plainly that no test files and no `native` environment exist.
- **Verified:** `pio run` succeeds — RAM 23.9%, Flash 38.7%.

---

## 2026-09-15 — Lyrics UI: selection indicator + non-blocking error states

> **Superseded in part.** The 1500 ms auto-return described below is **not**
> present in the code as of 2026-10-03 — `LyricsApp`'s `LoadFailed` state has
> no timer and waits for `BACK`. See
> [current-state.md §3.1](current-state.md#31-behavioural). The entry is kept
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
