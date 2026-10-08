# BLE Protocol

BokaBaksho exposes a GATT server that a PC companion uses to supply the song
library and lyrics, and to control audio playback. The device never plays audio
itself.

Implemented in `src/services/ble_service.h` / `.cpp`; dispatched in
`src/main.cpp::onBleCommand()`.

---

## 1. Transport

| Property | Value |
|---|---|
| Advertised device name | `BokaBaksho` |
| Service UUID | `12345678-1234-5678-9abc-def012345678` |
| TX characteristic | `12345678-1234-5678-9abc-def012345679` |
| TX properties | **NOTIFY** (ESP32 → PC), with a `BLE2902` CCCD descriptor |
| RX characteristic | `12345678-1234-5678-9abc-def012345680` |
| RX properties | **WRITE** (PC → ESP32) |
| Requested MTU | 517 |
| Scan response | enabled |
| Connection interval hints | `setMinPreferred(0x06)` / `setMaxPreferred(0x12)` (7.5 ms … 22.5 ms) |
| Advertising | started in `Begin()`; restarted automatically in `onDisconnect` |
| Client limit | one at a time |

Serial markers: `BLE_SERVICE_INIT_OK`, `BLE_CLIENT_CONNECTED`,
`BLE_CLIENT_DISCONNECTED`, `BLE_SERVICE_STOPPED`. Every received line is also
echoed as `BLE_RX: <line>`.

### 1.1 Framing

Both directions use **newline-terminated plain text**. `\r` and `\n` both
terminate a line on receive; `SendCommand()` appends exactly one `\n`.

On the ESP32, bytes accumulate in `rx_buffer_[256]`. A line longer than
255 characters is silently truncated at the buffer limit; the terminator still
flushes it. A disconnect resets `rx_len_` to 0 so a partially received frame is
discarded.

There is no checksum, sequence number, acknowledgement, or retransmission.
Commands that do not fit in a single notification are not supported.

---

## 2. Command Reference

### 2.1 ESP32 → PC (TX notifications)

| Command | Sent by | Meaning |
|---|---|---|
| `REQUEST_SONGS\|<offset>\|<count>` | `LyricsApp::RequestSongs()` | Ask for `count` song names starting at index `offset`. The firmware always requests `count = 5`. |
| `LYRICS\|<song_name>` | `LyricsApp::LoadSelectedLyric()` | Ask for the full timestamped lyric stream of this song. |
| `PLAY\|<song_name>` | `LyricsApp::SendPlayCommand()` | Start playback. Sent right after `LYRICS_END` is processed. |
| `PAUSE` | `LyricsApp::PausePlayback()` | Pause playback. |
| `RESUME` | `LyricsApp::ResumePlayback()` | Resume playback. |
| `STOP` | `LyricsApp::StopPlayback()` | Stop playback. Sent on deactivate, on return to the list, on `END`, and on BLE disconnect. |

All of these are sent only while a client is connected;
`SendCommand()` returns `false` otherwise.

### 2.2 PC → ESP32 (RX writes)

| Command | Routed to | Meaning |
|---|---|---|
| `TOTAL_SONGS\|<n>` | `lyrics_app.OnTotalSongs(n)` | Absolute library size. `n` is parsed with `atoi`. |
| `SONGS\|<name>\|<name>\|…` | `lyrics_app.OnSongListReceived(rest)` | Up to 5 pipe-separated names filling the current window. |
| `LYRICS_DATA\|<line>` | `lyrics_app.OnLyricsData(rest)` | One lyric line in the format of §3. Ignored unless the app is in `Loading`. |
| `LYRICS_END` | `lyrics_app.OnLyricsEnd()` | Terminator for the lyric stream. Ignored unless the app is `Loading` and awaiting lyrics. |
| `AUDIO_STARTED` | `lyrics_app.OnAudioStarted()` | Acknowledges `PLAY`; lyric timing starts from zero. Only honoured in `WaitingHandshake`. |
| `END` | `lyrics_app.OnPlaybackEnded()` | Playback finished. Only honoured in `Playing`; triggers playlist auto-advance. |
| `STOPPED` | *(none)* | **Accepted and ignored** — empty branch in `onBleCommand`. |
| `PAUSED` | *(none)* | **Accepted and ignored.** |
| `RESUMED` | *(none)* | **Accepted and ignored.** |
| `TIME_ACK\|<ms>` | *(none)* | **Accepted and ignored.** |

Anything else, including an empty line, is discarded.

### 2.3 Commands documented in code but not implemented

`src/services/ble_service.h` still lists `TIME_SYNC|<ms_since_start>` as an
ESP32 → PC command. **No code path ever sends it**, and the corresponding
`TIME_ACK` handler is empty. The time-synchronisation feature is not
implemented; the ESP32 and the PC keep independent clocks.

---

## 3. Lyric Stream Format

Each `LYRICS_DATA|` payload carries one line of a timestamped lyric file:

```
[Section] <00:05.30>word1 <00:05.80>word2 <00:06.10>word3
```

Parsing rules (`LyricsApp::ParseLine` / `ParseWordTimestamp`):

- A leading `[...]` section header is skipped if present.
- A timestamp token is exactly 10 characters: `<mm:ss.ff>`
  — `<`, two digits, `:`, two digits, `.`, two digits, `>`.
- `mm` and `ss` must be digits; `ss` must be `< 60`. `ff` is hundredths of a
  second, multiplied by 10 to give milliseconds.
- Value = `mm × 60 000 + ss × 1000 + ff × 10`.
- The word following a timestamp runs until the next space, `\n`, `\r`, or
  end of string, and is truncated to 31 characters.
- Characters outside `<...>` tokens are skipped.
- Parsing stops at `kMaxWords` (1000) entries.

Because the timestamps are absolute from the start of the song, the client
must send lyrics in ascending time order. Words that arrive out of order are
stored but never selected by `UpdateCurrentWord()`.

### 3.1 Worked example

```
Lyrics_DATA|[Chorus] <00:01.00>Hello <00:01.50>world
```

| Word | Timestamp |
|---|---|
| `Hello` | 1 000 ms |
| `world` | 1 500 ms |

---

## 4. Session Sequence

### 4.1 Connecting and listing songs

```
PC                                ESP32
 │  ◄── advertising "BokaBaksho" ──│
 │──── connect ───────────────────▶│  BLE_CLIENT_CONNECTED
 │  ◄───── REQUEST_SONGS|0|5 ─────│  (sent from EnterWaitingSongs)
 │──── TOTAL_SONGS|17 ───────────▶│
 │──── SONGS|Tumi|Closer|… ──────▶│  → FileList
```

If the app is already in `FileList` when a `SONGS` reply arrives, it simply
redraws the list.

### 4.2 Loading and playing a song

```
PC                                ESP32
 │  ◄────── LYRICS|Tumi ──────────│  (ENTER on a song)
 │──── LYRICS_DATA|… ────────────▶│  (many)
 │──── LYRICS_END ───────────────▶│  → WaitingHandshake
 │  ◄──────── PLAY|Tumi ──────────│
 │──── AUDIO_STARTED ────────────▶│  → Playing, t = 0
 │        … lyrics render …       │
 │──── END ──────────────────────▶│  → auto-advance to the next song
```

### 4.3 Timeouts

| Wait | Duration | On expiry |
|---|---|---|
| Song list reply | 5 s (`kSongRequestTimeoutMs`) measured from when `REQUEST_SONGS` was transmitted | `LoadFailed`, screen shows `No device` |
| Lyric reply | 5 s (`kSongRequestTimeoutMs`) measured from when `LYRICS\|` was transmitted — i.e. from `ENTER` | `LoadFailed`, screen shows `No device` |
| `AUDIO_STARTED` after `PLAY` | 10 s (`kHandshakeTimeoutMs`) | `LoadFailed`, screen shows `No device` / `connected` |
| `LoadFailed` on screen | 1.5 s (`kLoadFailedTimeoutMs`) | `ReturnToFileList()` — the error screen clears itself without user input |

Both 5 s windows share the same `LyricsApp::song_request_start_` member;
`LoadSelectedLyric()` re-arms it when it sends `LYRICS|`. Before 2026-10-03 it
was not re-armed, so the lyric timeout was effectively measured from the
song-list request and any list displayed for more than 5 s failed instantly on
`ENTER`.

A `SONGS` reply landing during `LoadFailed` clears the screen immediately
instead of waiting out the 1.5 s — so a reply that arrives *after* the 5 s
deadline is still used rather than treated as a failed exchange.

A `Back` press during `Loading` or `WaitingHandshake` aborts immediately.

---

## 5. Writing a PC Companion

Requirements derived from the firmware's behaviour:

1. **Connect** to service `12345678-1234-5678-9abc-def012345678`, enable
   notifications on the TX characteristic, and subscribe to notifications
   before expecting anything.
2. **Reply promptly.** The 5 s request timeout starts when the ESP32 transmits,
   not when you receive it.
3. **Send `TOTAL_SONGS` before `SONGS`**, or at least in the same write burst,
   so the window size and the absolute count agree.
4. **Respect the window.** The firmware keeps 5 names and asks for
   `REQUEST_SONGS|<offset>|5`. Reply with exactly the names in that range.
5. **Stream all lyrics before `LYRICS_END`.** `LYRICS_END` with zero parsed
   words puts the app into the `No lyrics` failure state.
6. **Send `AUDIO_STARTED` only after playback actually begins** — lyric timing
   starts at the moment it is received.
7. **Send `END` when the track finishes** if you want playlist auto-advance.
8. **Newline-terminate every command.** Multiple commands may be concatenated
   in a single write; the firmware splits on `\n`/`\r`.
9. **Keep individual lines under 255 characters.**
10. **Do not rely on `TIME_SYNC`** — it is not implemented.

### 5.1 Failure modes to handle

| Symptom | Likely cause |
|---|---|
| ESP32 shows `No device` after selecting a song | Client disconnected, or `LYRICS\|` never answered within 5 s |
| ESP32 shows `No lyrics` | `LYRICS_END` arrived but no `<mm:ss.ff>` token parsed |
| ESP32 shows `No device / connected` | `PLAY` sent but `AUDIO_STARTED` never arrived within 10 s |
| Lyrics render but audio is out of sync | Independent clocks — there is no `TIME_SYNC` |
| ESP32 returns to the list mid-song | BLE disconnect detected during `Playing`/`Paused` |

### 5.2 Reference implementation: `pc_client/`

The repository ships a working companion in [`pc_client/`](../pc_client/)
(Python, tkinter + bleak + pygame-ce). How it meets §5:

| Requirement | Where it is met |
|---|---|
| 1 — connect and subscribe to TX | `ble.BLEAudioClient._connect()` |
| 2 — reply promptly | every inbound command is dispatched to its own worker thread, so a slow operation never delays a `STOP` |
| 3 — `TOTAL_SONGS` before `SONGS` | `_push_library()` puts both in **one** write, so they cannot be reordered |
| 4 — respect the 5-row window | `_handle_request_songs()` clamps `count` to 5 and slices `songs[offset:offset + count]` |
| 5 — all lyrics before `LYRICS_END` | `_handle_lyrics()` streams every line, then sends `LYRICS_END` |
| 6 — `AUDIO_STARTED` after playback starts | `_handle_play()` sends it only once `play()` has returned |
| 7 — `END` when the track finishes | `_end_monitor()` polls `pygame.mixer.music.get_busy()` |
| 8 — newline-terminate every command | `send()` / `send_lines()` append `\n` |
| 9 — lines under 255 characters | `protocol.MAX_LINE_BYTES = 200`; an over-long line is logged |
| 10 — no `TIME_SYNC` | not implemented on either side; no such constant exists in the client |

Three deliberate departures from a naive client:

- **Writes are packed to the MTU.** Rather than one command per GATT write the
  client packs as many newline-terminated commands as fit into
  `DEFAULT_PAYLOAD = 500` bytes, and halves the batch when the link rejects it.
  Streaming a whole lyric file becomes a few round trips instead of one per
  line (60 lines → 4 writes in `pc_client.selftest`). BlueZ always reports
  `mtu_size == 23`, so the real limit is discovered by retry rather than
  by query.
- **An empty `SONGS|` is never sent.** `OnSongListReceived` with `added == 0`
  overwrites `total_songs_` with 0, so the client stays silent when the
  requested offset is past the end of the library.
- **The library is pushed proactively after connecting, but only if the device
  has not already asked.** The firmware requests songs on a state transition
  only, so a device sitting at *Connect to PC* before the PC attached would
  otherwise wait forever. A push is withheld when `REQUEST_SONGS` has already
  arrived.

**Not yet verified over the air.** The protocol logic is covered by
`pc_client.selftest` with the transport stubbed, and the packaged window has
been confirmed to open; no ESP32 has yet been attached to a build of this
client. See [build-and-test.md §6](build-and-test.md#6-pc-companion-client).

---

## 6. Known Limitations

- **Single client.** Advertising restarts on disconnect, but only one
  connection is handled at a time.
- **No authentication or pairing.** Any central that connects can control the
  device and receive the library.
- **No reliability layer.** Lost notifications are not retransmitted; a dropped
  `AUDIO_STARTED` costs the user the full 10 s timeout.
- **No time synchronisation** despite the protocol stubs (§2.3).
- **`BleService::OnEvent()` is empty and the service is never subscribed to the
  `EventBus`**, so there is no path for an app to request BLE traffic through
  the event system — apps call `ble_service.SendCommand()` directly.
- **`BleService::Stop()` is never called**, so advertising runs for the whole
  session.
