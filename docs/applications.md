# Applications

This document specifies the behaviour of every application in BokaBaksho:
what it shows, how it reacts to input, and how it moves between states.

For the lifecycle contract that all apps share, see
[architecture.md §5](architecture.md#5-application-lifecycle-iapp).
For render primitives, see
[api-reference.md §2.5](api-reference.md#25-displayrequest--srccommonevent_typesh).

---

## 1. Registered Applications

| Launcher label | Class | `AppId` | Source |
|---|---|---|---|
| *(launcher root, never listed)* | `SystemUi` | `SystemUi` | `src/system_ui/` |
| `Lyrics` | `LyricsApp` | `Lyrics` | `src/apps/lyrics/` |
| `Timer` | `StopwatchApp` | `Clock` | `src/apps/clock/` |

`AppId::Music`, `AppId::Alarm`, and `AppId::Settings` are reserved but have no
class and are never registered, so they never appear in the launcher menu.

---

## 2. Launcher — `SystemUi`

The launcher has two screens: an **animated cat screensaver** and an
**app menu**. It is the permanent root of the navigation stack.

### 2.1 Screensaver

`Begin()` draws the screensaver directly (the stack's `Boot()` does not call
`OnActivate()`), so the device boots into the cat.

Any button press wakes the launcher into the menu. `BACK` from the menu
returns to the screensaver with the cat in its **Sleepy** expression.

#### Expression model

The cat alternates between an *expression* phase and a *neutral* phase,
switched by `PickNextExpression()` when the current hold expires:

| From | To | Hold duration | Head offset |
|---|---|---|---|
| neutral | expression | 6–12 s | −2 px (all reachable expressions) |
| expression | neutral | 15–25 s | 0 px |

Expressions are chosen with `random(6)`:

| `expr` | Expression |
|---|---|
| 0 | `Happy` |
| 1 | `Alert` |
| 2 | `Sad` |
| 3 | `Neutral` *(held as an "expression" — only differs by the −2 px head offset)* |
| 4 | `Angry` |
| 5 | `Blink` |

`Sleepy` (6) is **never** produced by the randomiser. It is reachable only by
pressing `BACK` from the menu.

#### Animation timeline (all `millis()` driven)

| Behaviour | Timing |
|---|---|
| Eye blink duration | 180 ms |
| Neutral-mode blink | every 2.5–6 s (`next_neutral_blink_`) |
| Expression-mode blink | every 1–5 s |
| Blink while looking | every 1.5–3 s |
| Initial blink | 1–2 s after `Begin()` |
| Look-left/right chance | 1 in 3 each evaluation |
| Look direction | `random(2)` → left (−1) or right (+1) |
| Look duration | 1–3 s |
| Idle before next look attempt | 6–10 s |

During a look the cat's head rises 2 px (`cat_y_offset_ = -2`) and the eyes
shift horizontally by `paw_offset × 2` px (−2, 0, or +2).

`Update()` early-returns when the screensaver is not active, so the animation
consumes no display bandwidth while the menu is showing.

### 2.2 Menu

`OnActivate()` disables the screensaver and renders the menu.

**Input**

| Event | Effect |
|---|---|
| `ButtonIncrement` / `RotateRight` | `selection_ = (selection_ + 1) % count` |
| `ButtonDecrement` / `RotateLeft` | `selection_` decremented, wrapping to `count - 1` |
| `Enter` | `app_manager.LaunchApp(GetAppAt(selection_)->GetAppId())` |
| `Back` | return to the screensaver with `Sleepy` (root is never popped) |
| any, while screensaver is showing | wake into the menu |

`HandleInput()` returns `false` when no app is registered (`count == 0`),
which lets a `Back` reach `AppManager::GoBack()` — a no-op at the root.

**Render.** `ShowMenu()` copies each registered app's `GetName()` into the
member array `menu_items_[5]` (pointer-lifetime safe) and posts
`ShowAppMenu`.

### 2.3 State

| Member | Meaning |
|---|---|
| `screensaver_active_` | `true` between `Begin()`/`Back` and the next wake |
| `selection_` | highlighted menu index |
| `menu_items_[6]` | row pointers for the current menu |
| `base_state_` / `draw_state_` | expression model vs. what is currently drawn (differs only mid-blink) |
| `cat_y_offset_`, `look_dir_` | animation offsets |
| `in_expression_` | phase flag |
| `is_blinking_`, `blink_end_time_`, `next_blink_time_`, `next_neutral_blink_` | blink scheduler |
| `is_looking_`, `look_end_time_`, `next_look_time_` | look scheduler |
| `hold_start_`, `hold_duration_`, `sleepy_mode_` | expression scheduler |

`OnDeactivate()` is intentionally empty — the launcher keeps its selection so
returning from an app lands on the same menu entry.

---

## 3. Lyrics App

A karaoke display that is synchronised to audio playing on a connected PC.
The ESP32 holds no audio and no lyric files: the song list and the timestamped
lyrics are **streamed over BLE from the PC companion**, and the PC starts
playback on request.

### 3.1 Flow

```
 1. PC connects over BLE.
 2. ESP32 sends REQUEST_SONGS|0|5      -> PC replies TOTAL_SONGS|n, SONGS|a|b|c
 3. User scrolls and presses ENTER     -> ESP32 sends LYRICS|<song>
 4. PC streams LYRICS_DATA|<line> …    then LYRICS_END
 5. ESP32 sends PLAY|<song>
 6. PC replies AUDIO_STARTED           -> lyric timing starts from zero
 7. On END the app auto-advances to the next song (playlist behaviour)
```

### 3.2 States

```
                    ┌──────────────┐
        ┌──────────▶│ WaitingSongs │◀──────────────┐
        │           └──────┬───────┘               │
        │                  │ SONGS received        │ total_songs_ == 0
        │                  ▼                       │
        │           ┌──────────────┐               │
        │     ┌────▶│  FileList    │───────────────┤
        │     │     └──┬───────┬───┘               │
        │     │ BACK   │ ENTER │ song request      │
        │     │(return │       │  timeout          │
        │     │ to app)│       ▼                   │
        │     │     ┌──┴───────┴───┐   no device   │
        │     │     │   Loading    │──────────┐    │
        │     │     └──────┬───────┘          │    │
        │     │            │ LYRICS_END +     │    │
        │     │            │ words > 0        │    │
        │     │            ▼                  │    │
        │     │     ┌────────────────┐        │    │
        │     │     │ WaitingHandshake│──10s──┤    │
        │     │     └──────┬─────────┘ timeout│    │
        │     │            │ AUDIO_STARTED    │    │
        │     │            ▼                  ▼    │
        │     │     ┌──────────────┐   ┌──────────┐│
        │     │     │   Playing    │   │ LoadFailed││
        │     │     └──┬───────┬───┘   └────┬─────┘│
        │     │  ENTER │       │ BACK       │BACK  │
        │     │        ▼       │            │      │
        │     │  ┌──────────┐  │            │      │
        │     └──│  Paused  │  │            │      │
        │        └────┬─────┘  │            │      │
        │             │ BACK   │            │      │
        └─────────────┴────────┴────────────┴──────┘
                       ReturnToFileList()
```

| State | Screen | Entered from |
|---|---|---|
| `WaitingSongs` | `Connect to PC` | `Begin()`, `OnActivate()` when `total_songs_ == 0`, `ReturnToFileList()` when there are no songs |
| `FileList` | scrollable song list | `OnSongListReceived()`, `EnterFileList()` |
| `Loading` | `Loading...` | `LoadSelectedLyric()` |
| `WaitingHandshake` | `Loading...` | `Update()` once lyrics are complete |
| `Playing` | song title, then lyrics | `Update()` on `AUDIO_STARTED` |
| `Paused` | `PAUSED` | ENTER while playing |
| `LoadFailed` | error message | several failure paths |

### 3.3 Input by state

| State | `Back` | `Enter` | Increment / Decrement |
|---|---|---|---|
| `WaitingSongs` | returns `false` → **exits the app** | consumed, no effect | consumed, no effect |
| `FileList` | returns `false` → **exits the app** | `LoadSelectedLyric()` | move selection, wrapping over `total_songs_`; scrolls the BLE window when the selection leaves it |
| `Loading` | `ReturnToFileList()` | returns `false` → dropped | returns `false` → dropped |
| `WaitingHandshake` | `ReturnToFileList()` | returns `false` → dropped | returns `false` → dropped |
| `LoadFailed` | `ReturnToFileList()` | **consumed, no effect** | **consumed, no effect** |
| `Playing` | `ReturnToFileList()` | `PausePlayback()` | returns `false` → dropped |
| `Paused` | `ReturnToFileList()` | `ResumePlayback()` | returns `false` → dropped |

Returning `false` for a non-`Back` event makes `AppManager::RouteInput()`
return `false`, which simply drops the event.

### 3.4 Scroll window

The app keeps a 5-row window (`kWindowSize`) of song names in
`song_buffer_[5][32]`, described by `buffer_offset_` and `buffer_count_`.

- Moving the selection outside `[buffer_offset_, buffer_offset_ + buffer_count_)`
  triggers `RequestSongs(offset)` with
  `offset = selected_index_ < 2 ? 0 : selected_index_ - 2`, centring the
  selection in the new window.
- `ShowFileList()` publishes `has_more_above = buffer_offset_ > 0` and
  `has_more_below = buffer_offset_ + buffer_count_ < total_songs_`, which the
  renderer turns into `^` / `v` arrows.
- `selected` is passed as `selected_index_ - buffer_offset_`, i.e. a
  **window-relative** index.
- A short reply may shrink `total_songs_` to `buffer_offset_ + added` when the
  PC reports fewer songs than claimed.

### 3.5 Timing

| Phase | Duration | Behaviour |
|---|---|---|
| Song list reply | `kSongRequestTimeoutMs` = 5 s from the `REQUEST_SONGS` transmission | `LoadFailed` with `No device` |
| Lyric reply | `kSongRequestTimeoutMs` = 5 s from the `LYRICS\|` transmission (re-armed by `ENTER`) | `LoadFailed` with `No device` |
| Post-`PLAY` handshake | `kHandshakeTimeoutMs` = 10 s | `LoadFailed` with the two-line `No device / connected` screen |
| Song title before lyrics | `kTitleTimeoutMs` = 1.5 s | `Update()` returns early until `title_until_` passes |
| Word visibility | `kWordDisplayTimeoutMs` = 5 s | screen blanks if no new word arrives |

Lyric timing is computed as `millis() - start_time_`. `ResumePlayback()`
compensates for the pause by shifting `start_time_` forward by the paused
duration.

### 3.6 Word selection

`UpdateCurrentWord(elapsed)` advances `current_word_` while the **next**
word's timestamp is already due, marking `new_word_`. It also re-arms a word
that has not been shown yet. `ShowCurrentWord()` renders with `Large` font for
words of ≤ 8 characters and `Medium` for longer ones.

Words longer than `kMaxWordLength - 1` (31) characters are truncated during
parsing; the parser stops after `kMaxWords` (1000).

### 3.7 Playlist auto-advance

When the PC sends `END` while `Playing`, the app stops playback, moves
`selected_index_` to `(selected_index_ + 1) % total_songs_`, refetches the
window if the new selection is outside it, and immediately starts loading the
next song.

### 3.8 Failure screens

| Trigger | Screen |
|---|---|
| No BLE connection when selecting a song | `No device` |
| Song/lyrics request exceeds 5 s | `No device` |
| `LYRICS_END` with zero words parsed | `No lyrics` |
| Handshake exceeds 10 s | two lines: `No device` / `connected` (no cursor, `selected = 255`) |

There is **no automatic return** from `LoadFailed` — see
[current-state.md](current-state.md#3-known-issues--limitations).

### 3.9 BLE messages sent

| Message | When |
|---|---|
| `REQUEST_SONGS\|<offset>\|5` | entering `WaitingSongs`, scrolling out of the window, `EnterFileList()` with an empty window, auto-advance |
| `LYRICS\|<song_name>` | ENTER on the song list |
| `PLAY\|<song_name>` | immediately after `LYRICS_END` with words present |
| `PAUSE` | ENTER while playing |
| `RESUME` | ENTER while paused |
| `STOP` | `OnDeactivate()`, `ReturnToFileList()`, `END` handling, BLE disconnect |

`STOP` is sent unconditionally by `StopPlayback()`, including when BLE is
already disconnected — `SendCommand()` simply returns `false`.

---

## 4. Timer / Stopwatch — `StopwatchApp`

A single app with two modes driven by the preset:

- `timer_set_min_ == 0` → **STOPWATCH** (counts up from zero)
- `timer_set_min_ > 0` → **TIMER** (counts down from the preset)

### 4.1 Input

| Event | Effect |
|---|---|
| Increment | +1 minute preset (max 65535). **Locked while running or finished.** |
| Decrement | −1 minute preset (min 0). **Locked while running or finished.** |
| `Enter` | if `finished_` → re-arm at the preset (not running); else if running → pause; else → start/resume |
| `Back` | if `IsReset()` → returns `false`, **exits to the launcher**; otherwise full reset |

`IsReset()` is `timer_set_min_ == 0 && elapsed_ms_ == 0 && !running_ && !finished_`.
Because `OnActivate()` calls `ResetAll()`, entering the app always lands in the
reset state and `Back` exits immediately on first press.

Changing the preset while stopped also flips `mode_` and recomputes
`remaining_ms_`.

### 4.2 Timing

| Constant | Value |
|---|---|
| `kMinuteMs` | 60 000 ms |
| `kDrawIntervalMs` | 200 ms — minimum redraw interval while running |
| `kBlinkMs` | 500 ms — flash period after the timer expires |

`Update()` computes a `millis()` delta each pass and accumulates
(`elapsed_ms_ += delta`) or subtracts (`remaining_ms_ -= delta`). When
`delta >= remaining_ms_` the timer finishes: `remaining_ms_ = 0`,
`finished_ = true`, `running_ = false`.

Because timing is delta-based, any stall in `loop()` is accounted for on the
next pass — but drift still accumulates across very long runs (no RTC, no NTP).

### 4.3 Screens

| Mode / condition | Top label | Time | Bottom status |
|---|---|---|---|
| Stopwatch stopped | `STOPWATCH` | `mm:ss` | `READY` |
| Stopwatch running | `STOPWATCH` | `mm:ss` | `RUNNING` |
| Timer stopped | `TIMER` | `mm:ss` | `READY` |
| Timer running | `TIMER` | `mm:ss` | `RUNNING` |
| Finished | `TIME'S UP!` | `00:00` | `ENTER: restart` |

While finished the whole `big_time` field is hidden on alternate 500 ms
intervals (`blink = finished_ && !blink_on_`), producing a flashing time.

`FormatTime()` switches to `h:mm:ss` once the value passes one hour.

---

## 5. Screen Layouts

Rendered by `DisplayService`; coordinates assume a 128 × 64 framebuffer.

### 5.1 `ShowAppMenu` — launcher menu

```
              ┌────────────────────┐
              │                    │
              │      Lyrics        │   Large font, centred both axes
              │                    │
              │                    │
              │    ▣ □ □ □         │   4×4 dots, 8 px pitch, centred,
              └────────────────────┘   selected dot filled
```

### 5.2 `ShowSongList` — lyrics song list

```
   ┌──────────────────────────────┐
   │ ▲            (^ centred)     │  shown when has_more_above
   │ > Tumi.txt                   │  x=0 cursor, x=8 text
   │   Closer.txt                 │
   │   ...                        │
   │ ▼                    2 / 17  │  arrows centred; counter right-aligned
   └──────────────────────────────┘  on the last row baseline
```

Rows are top-anchored at `2 + ascent` and advance by the `Medium` line height.
The `n / total` counter uses `Small` font in a static 8-byte buffer.

### 5.3 `ShowLines` — generic block

Vertically centred block. When `selected < line_count`, a `>` cursor is drawn
at x = 0 and all text is indented to x = 10; otherwise text uses the requested
alignment with no indent.

### 5.4 `ShowWord` / `ShowLine`

`ShowWord` centres the text both horizontally and vertically, ignoring
`alignment` for horizontal placement. `ShowLine` centres vertically but honours
`alignment` horizontally.

### 5.5 `ShowBigTime`

```
   ┌──────────────────────────────┐
   │         STOPWATCH            │  Medium, baseline y = 12
   │                              │
   │          00:42               │  LargeBold, vertically centred
   │                              │
   │          RUNNING             │  Small, baseline y = height − 9
   └──────────────────────────────┘
```

### 5.6 `ShowScreensaver` — the cat

Vector geometry (all coordinates shifted by `cat_y_offset`):

- **Head** — polyline from (44,41) up the left side, over two ear risers
  (52,11)→(76,11), and down the right side to (84,41).
- **Paws** — 14 × 14 outlined squares at (38,41) and (76,41), joined by a
  horizontal line from (52,46) to (76,46).
- **Eyes** — drawn at y = 24 + offset, x shifted by `paw_offset × 2`:

  | `cat_state` | Expression | Drawing |
  |---|---|---|
  | 0 | Happy | two chevrons `^ ^` |
  | 1 | Blink | two flat lines `- -` |
  | 2 | Alert | two 7×7 boxes `[] []` |
  | 3 | Neutral | two 7×7 boxes `[] []` |
  | 4 | Sad | two inverted chevrons |
  | 5 | Sleepy | two flat lines `- -` |
  | 6 | Angry | two crosses `× ×` |

- **Mouth** — drawn from `cat_base_state` at y = 35 + offset, so the mouth does
  not change during a blink:

  | `cat_base_state` | Mouth |
  |---|---|
  | 0 Happy | right + bottom + left edges (open top) |
  | 2 Alert | 6 × 4 open box, 2 px lower |
  | 3 Neutral | straight line |
  | 4 Sad | right + top + left edges (open bottom) |
  | 6 Angry | right + top + left edges, 1 px lower |
  | 1, 5 (Blink, Sleepy) | falls through to the neutral line |
