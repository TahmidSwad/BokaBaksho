"""BLE link between the PC and the Boka_Baksho ESP32.

Threading model (unchanged in spirit from the original reference client):

* A dedicated daemon thread hosts the asyncio event loop. Bleak runs there.
* Inbound notifications are line-buffered and dispatched to short-lived
  worker threads, so a slow operation (streaming lyrics) never blocks the
  loop or delays a ``STOP``.
* Audio state mutations are serialised by ``command_lock``.
* Every outbound write is serialised by ``send_lock`` and awaited
  synchronously, which guarantees ordering — e.g. ``TOTAL_SONGS`` always
  lands before the ``SONGS`` batch that follows it.
* The GUI is never touched directly; everything goes through a
  ``queue.Queue`` drained by the Tk main loop.
"""

import asyncio
import concurrent.futures
import os
import queue
import threading
import time

from bleak import BleakClient, BleakScanner

from . import audio, protocol

#: Seconds to wait for a REQUEST_SONGS before pushing the library proactively.
PUSH_GRACE_S = 0.8

#: Fallback write size when the backend cannot report a real MTU.
#: (BlueZ always reports 23; the ESP32 requests 517.)
DEFAULT_PAYLOAD = 500

#: How often the end-of-song monitor polls the mixer.
END_POLL_S = 0.2


class BLEAudioClient:
    def __init__(self, player, events, media_dir):
        self.player = player
        self.events = events
        self.media_dir = media_dir

        self.loop = None
        self.thread = None
        self.client = None
        self.device = None
        self.address = None

        self.connected = False
        self.running = False

        self.command_lock = threading.Lock()
        self.send_lock = threading.Lock()

        self._rx_buffer = ""
        self._seen_request_songs = False
        self._end_token = 0
        self._suppress_end = False

    # ==================================================================
    # GUI-facing API (called from the Tk thread)
    # ==================================================================

    def start(self):
        if self.thread and self.thread.is_alive():
            return
        self.running = True
        self.thread = threading.Thread(target=self._thread_main, daemon=True)
        self.thread.start()

    def connect(self):
        if not self.loop:
            self.log("BLE event loop is not running.")
            return
        if self.connected:
            self.log("Already connected.")
            return
        self._seen_request_songs = False
        asyncio.run_coroutine_threadsafe(self._connect(), self.loop)

    def disconnect(self):
        if self.loop:
            asyncio.run_coroutine_threadsafe(self._disconnect(), self.loop)

    def shutdown(self):
        self.running = False
        self._stop_end_monitor()
        if self.loop:
            try:
                asyncio.run_coroutine_threadsafe(
                    self._disconnect(), self.loop
                ).result(3.0)
            except Exception:
                pass
            self.loop.call_soon_threadsafe(self.loop.stop)

    def set_media_dir(self, media_dir):
        self.media_dir = media_dir

    def refresh_library(self):
        """Re-send the library — used by the GUI's refresh button."""
        if not self.connected:
            self.log("Not connected — library not re-sent.")
            return
        self._push_library()

    # ==================================================================
    # asyncio plumbing
    # ==================================================================

    def _thread_main(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        try:
            self.loop.run_until_complete(self._idle())
        except Exception as exc:
            self.log(f"BLE thread error: {exc}")
        finally:
            self.loop.close()

    async def _idle(self):
        while self.running:
            await asyncio.sleep(0.2)

    # ==================================================================
    # Connect / disconnect
    # ==================================================================

    async def _connect(self):
        self.log(f"Scanning for '{protocol.DEVICE_NAME}'...")
        try:
            device = await BleakScanner.find_device_by_name(
                protocol.DEVICE_NAME, timeout=10.0
            )
        except Exception as exc:
            self.log(f"Scan error: {exc}")
            self.push("connection", False)
            return

        if device is None:
            self.log("Device not found.")
            self.push("connection", False)
            return

        self.device = device
        self.address = getattr(device, "address", None)
        self.log(f"Found {device.name} [{self.address}] — connecting...")

        try:
            self.client = BleakClient(
                device, disconnected_callback=self._on_disconnect
            )
            await self.client.connect()
            if not self.client.is_connected:
                raise RuntimeError("connect() returned a disconnected client")
        except Exception as exc:
            self.log(f"Connection error: {exc}")
            self.client = None
            self.device = None
            self.connected = False
            self.push("connection", False)
            return

        self._suppress_end = False
        self._rx_buffer = ""
        self._seen_request_songs = False
        self.connected = True

        await self.client.start_notify(protocol.TX_UUID, self._on_notify)
        self.log("Connected — subscribed to TX notifications.")
        self.push("connection", True)
        self.push("device", self.address or "")

        # The firmware only asks for the library when it transitions into
        # the song list. If the user opened the Lyrics app before we
        # connected it will never ask, so push proactively — but only if it
        # does not ask us first (answering an unprompted push while it is
        # mid-scroll would be redundant).
        threading.Thread(
            target=self._maybe_push_later, daemon=True
        ).start()

    def _maybe_push_later(self):
        deadline = time.monotonic() + PUSH_GRACE_S
        while time.monotonic() < deadline and self.running:
            if self._seen_request_songs or not self.connected:
                return
            time.sleep(0.05)
        if self.connected and not self._seen_request_songs and self.running:
            self._push_library()

    async def _disconnect(self):
        self._suppress_end = True
        self._stop_end_monitor()
        self._stop_playback()

        if not self.client:
            self.connected = False
            self.push("connection", False)
            return

        try:
            if self.client.is_connected:
                try:
                    await self.client.stop_notify(protocol.TX_UUID)
                except Exception:
                    pass
                await self.client.disconnect()
        except Exception as exc:
            self.log(f"Disconnect error: {exc}")
        finally:
            self.connected = False
            self.client = None
            self.device = None
            self.address = None
            self.log("Disconnected.")
            self.push("device", "")
            self.push("connection", False)

    def _on_disconnect(self, client):
        """Called by bleak when the link drops unexpectedly."""
        self.log("ESP32 disconnected unexpectedly.")
        self._suppress_end = True
        self._stop_end_monitor()
        self._stop_playback()
        self.connected = False
        self.client = None
        self.device = None
        self.address = None
        self.push("device", "")
        self.push("connection", False)

    def _stop_playback(self):
        with self.command_lock:
            if self.player.state in (
                audio.PLAYING,
                audio.PAUSED,
                audio.LOADED,
            ):
                self.player.stop()
                self.push("playing", False)
                self.push("song", "")
                self.log("Playback stopped (link lost).")

    # ==================================================================
    # Receive path
    # ==================================================================

    def _on_notify(self, characteristic, data):
        try:
            self._rx_buffer += bytes(data).decode("utf-8", "replace")
        except Exception as exc:
            self.log(f"Decode error: {exc}")
            return
        while "\n" in self._rx_buffer:
            line, self._rx_buffer = self._rx_buffer.split("\n", 1)
            line = line.strip("\r").strip()
            if line:
                self._dispatch(line)

    def _dispatch(self, line):
        self.log(f"<- {line[:160]}")
        name, rest = protocol.split_command(line)

        if name == "PLAY":
            song = rest.strip()
            if not song:
                self.log("PLAY with empty song name — ignored.")
                return
            self._spawn(self._handle_play, song)
        elif name == "PAUSE":
            self._spawn(self._handle_pause)
        elif name == "RESUME":
            self._spawn(self._handle_resume)
        elif name == "STOP":
            self._spawn(self._handle_stop)
        elif name == "REQUEST_SONGS":
            self._spawn(self._handle_request_songs, rest)
        elif name == "LYRICS":
            self._spawn(self._handle_lyrics, rest.strip())
        elif name == "END":
            self._spawn(self._handle_end)
        elif name in ("TIME_ACK", "TIME_SYNC"):
            self.log(f"{name} received (not implemented by firmware).")
        elif name in ("AUDIO_STARTED", "LYRICS_END", "TOTAL_SONGS", "SONGS",
                      "LYRICS_DATA"):
            self.log(f"Ignoring PC-only command echoed back: {name}")
        else:
            self.log(f"Unknown command: {line[:80]}")

    @staticmethod
    def _spawn(target, *args):
        threading.Thread(target=target, args=args, daemon=True).start()

    # ==================================================================
    # Command handlers (worker threads)
    # ==================================================================

    def _handle_request_songs(self, rest):
        parsed = protocol.parse_request_songs(rest)
        if parsed is None:
            self.log(f"Malformed REQUEST_SONGS: {rest}")
            return
        offset, count = parsed
        self._seen_request_songs = True

        count = max(1, min(count, protocol.WINDOW_SIZE))
        songs = self._songs()

        if offset >= len(songs):
            # An empty SONGS reply would make the firmware overwrite its
            # song total with 0 (LyricsApp::OnSongListReceived), so stay
            # silent instead of "helpfully" sending nothing.
            self.log(
                f"REQUEST_SONGS offset {offset} >= {len(songs)} — no reply."
            )
            return

        batch = songs[offset:offset + count]
        self.send_lines([protocol.build_songs(batch)])
        self.log(f"Sent songs[{offset}:{offset + len(batch)}] of {len(songs)}")

    def _handle_lyrics(self, song_name):
        path = self._find_lyrics_file(song_name)
        if path is None:
            self.log(f"No lyric file for '{song_name}' — LYRICS_END only.")
            self.send("LYRICS_END")
            return

        self.log(f"Streaming lyrics: {os.path.basename(path)}")
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                lines = [
                    protocol.build_lyrics_data(raw.strip())
                    for raw in handle
                    if raw.strip()
                ]
        except Exception as exc:
            self.log(f"Lyrics read error: {exc}")
            self.send("LYRICS_END")
            return

        ok = self.send_lines(lines)
        self.send("LYRICS_END")
        self.log(
            f"LYRICS_END sent ({len(lines)} lines, {'ok' if ok else 'errors'})."
        )

    def _handle_play(self, song_name):
        with self.command_lock:
            self.log(f"PLAY requested: {song_name}")
            path = self._find_audio_file(song_name)
            if path is None:
                self.log(
                    f"No audio file for '{song_name}' "
                    f"(looked for {', '.join(protocol.AUDIO_EXTENSIONS)})."
                )
                return
            if not self.player.load(path):
                return
            if not self.player.play_loaded():
                return

            # Sent only after playback has actually started — the firmware
            # begins lyric timing the moment it arrives.
            if not self.send("AUDIO_STARTED"):
                self.player.stop()
                return

            self.log(f"Playing: {os.path.basename(path)}")
            self.push("song", song_name)
            self.push("playing", True)
            self._start_end_monitor()

    def _handle_pause(self):
        with self.command_lock:
            if self.player.pause():
                self.send("PAUSED")
                self.push("playing", False)
                self.log("Paused.")

    def _handle_resume(self):
        with self.command_lock:
            if self.player.resume():
                self.send("RESUMED")
                self.push("playing", True)
                self.log("Resumed.")

    def _handle_stop(self):
        with self.command_lock:
            self._stop_end_monitor()
            if self.player.state != audio.IDLE:
                self.player.stop()
            self.send("STOPPED")
            self.push("playing", False)
            self.push("song", "")
            self.log("Stopped.")

    def _handle_end(self):
        with self.command_lock:
            self.log("END acknowledged.")
            self._stop_end_monitor()
            if self.player.state in (audio.PLAYING, audio.PAUSED):
                self.player.stop()
            self.push("playing", False)

    # ==================================================================
    # End-of-song monitor -> END
    # ==================================================================

    def _start_end_monitor(self):
        self._end_token += 1
        token = self._end_token
        threading.Thread(
            target=self._end_monitor, args=(token,), daemon=True
        ).start()

    def _stop_end_monitor(self):
        self._end_token += 1

    def _end_monitor(self, token):
        time.sleep(0.3)
        while token == self._end_token and self.running:
            time.sleep(END_POLL_S)
            if token != self._end_token:
                return
            if not self.player.is_playing:
                continue
            if self.player.busy:
                continue

            self.player.state = audio.STOPPED
            self.player.current_file = None
            self.push("playing", False)

            if self._suppress_end or not self.connected:
                self.log("Song ended — link down, no END sent.")
                return

            self.log("Song ended — sending END.")
            self.send("END")
            return

    # ==================================================================
    # Transmit path
    # ==================================================================

    def send(self, message, timeout=3.0):
        """Send one command (a newline is appended)."""
        return self.send_lines([message], timeout)

    def send_lines(self, lines, timeout=5.0):
        """Send one or more commands, packed to fit the ATT MTU.

        Writes are serialised so lines always arrive in order. If a packed
        write fails (MTU smaller than assumed) the batch is halved and
        retried down to single lines.
        """
        if not lines:
            return True
        if not self.connected or not self.client or not self.loop:
            self.log(f"Cannot send {len(lines)} line(s): not connected.")
            return False
        with self.send_lock:
            return self._write_lines(list(lines), timeout)

    def _write_lines(self, lines, timeout):
        limit = self._payload_limit()
        batch, size = [], 0

        for line in lines:
            raw = line + "\n"
            length = len(raw.encode("utf-8", "replace"))

            # The firmware keeps a 256-byte receive buffer
            # (BleService::kRxBufferSize) and silently drops anything past
            # 255, so a longer line would arrive truncated.
            if length > protocol.MAX_LINE_BYTES:
                self.log(
                    f"Line exceeds firmware rx buffer ({length}B): "
                    f"{line[:60]}..."
                )

            if length > limit:
                if batch and not self._flush(batch, timeout):
                    return False
                batch, size = [], 0
                if not self._flush([line], timeout):
                    return False
                continue

            if size + length > limit:
                if not self._flush(batch, timeout):
                    return False
                batch, size = [], 0

            batch.append(line)
            size += length

        if batch:
            return self._flush(batch, timeout)
        return True

    def _flush(self, batch, timeout):
        if not batch:
            return True
        payload = "\n".join(batch) + "\n"

        if len(batch) == 1:
            # One line, one attempt, one error message.
            return self._write(payload, timeout, quiet=False)

        if self._write(payload, timeout, quiet=True):
            return True

        # Most likely an MTU limit: halve until it fits, preserving order.
        self.log(
            f"Write of {len(payload.encode())}B rejected — "
            f"splitting batch ({len(batch)} lines)."
        )
        mid = len(batch) // 2
        return self._write_lines(batch[:mid], timeout) and self._write_lines(
            batch[mid:], timeout
        )

    @staticmethod
    def _payload_limit():
        # BlueZ reports 23 regardless of the real MTU, so treat that as
        # "unknown" and rely on the adaptive halving in _flush().
        return DEFAULT_PAYLOAD

    def _write(self, payload, timeout, quiet=False):
        if not self.connected or not self.client or not self.loop:
            if not quiet:
                self.log("Write skipped: disconnected.")
            return False
        try:
            future = asyncio.run_coroutine_threadsafe(
                self._write_async(payload), self.loop
            )
            return future.result(timeout=timeout)
        except concurrent.futures.TimeoutError:
            if not quiet:
                self.log(f"Write timeout: {payload[:60]!r}")
            return False
        except Exception as exc:
            if not quiet:
                self.log(f"Write error: {exc}")
            return False

    async def _write_async(self, payload):
        if not self.client or not self.client.is_connected:
            return False
        await self.client.write_gatt_char(
            protocol.RX_UUID,
            payload.encode("utf-8", "replace"),
            response=True,
        )
        for line in payload.rstrip("\n").split("\n"):
            self.log(f"-> {line[:160]}")
        return True

    # ==================================================================
    # Library helpers
    # ==================================================================

    def _songs(self):
        try:
            entries = os.listdir(self.media_dir)
        except Exception as exc:
            self.log(f"Cannot list media folder: {exc}")
            return []
        names = [
            os.path.splitext(entry)[0]
            for entry in entries
            if entry.lower().endswith(protocol.LYRIC_EXTENSION)
        ]
        names.sort(key=str.lower)
        return names

    def _push_library(self):
        songs = self._songs()
        if not songs:
            self.log("Media folder has no .txt lyric files.")
            return
        # TOTAL_SONGS and SONGS go out in a single write so they cannot be
        # reordered relative to each other.
        batch = songs[: protocol.WINDOW_SIZE]
        self.send_lines(
            [protocol.build_total_songs(len(songs)), protocol.build_songs(batch)]
        )
        self.log(f"Pushed library: {len(songs)} song(s).")

    def _find_lyrics_file(self, song_name):
        if not song_name:
            return None
        direct = os.path.join(self.media_dir, song_name + protocol.LYRIC_EXTENSION)
        if os.path.isfile(direct):
            return direct
        target = (song_name + protocol.LYRIC_EXTENSION).lower()
        try:
            for entry in os.listdir(self.media_dir):
                if entry.lower() == target:
                    return os.path.join(self.media_dir, entry)
        except Exception:
            pass
        return None

    def _find_audio_file(self, song_name):
        if os.path.splitext(song_name)[1]:
            candidates = [song_name]
        else:
            candidates = [song_name + ext for ext in protocol.AUDIO_EXTENSIONS]

        for name in candidates:
            path = os.path.join(self.media_dir, name)
            if os.path.isfile(path):
                return path

        wanted = {name.lower() for name in candidates}
        try:
            for entry in os.listdir(self.media_dir):
                if entry.lower() in wanted:
                    path = os.path.join(self.media_dir, entry)
                    if os.path.isfile(path):
                        return path
        except Exception:
            pass
        return None

    # ==================================================================
    # GUI events
    # ==================================================================

    def log(self, message):
        self.events.put(("log", message))

    def push(self, kind, value):
        self.events.put((kind, value))
