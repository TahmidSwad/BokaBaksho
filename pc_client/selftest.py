"""Offline self-test for the PC companion — no BLE hardware required.

    python -m pc_client.selftest

Exercises the protocol handling, library resolution, MTU packing and
command dispatch with the transport stubbed out.  It never opens a window
and never touches the radio, so it is safe to run anywhere.

Exit status 0 means every check passed.
"""

import queue
import sys

from . import audio, protocol

MEDIA_HINT = """\
A temporary media folder is created for the song-resolution checks; the
lyric/MTU/dispatch checks use stubs and need no files at all.
"""

_checks = 0
_failures = []


def check(label, condition, detail=""):
    global _checks
    _checks += 1
    if condition:
        print(f"  ok   {label}")
    else:
        print(f"  FAIL {label} {detail}")
        _failures.append(label)


def section(title):
    print(f"\n{title}")


# ======================================================================
def build_client(media_dir):
    events = queue.Queue()
    player = audio.AudioPlayer(lambda m: events.put(("log", m)))
    from . import ble

    client = ble.BLEAudioClient(player, events, media_dir)

    # Keep the real flusher so tests that need it can restore it.
    client._real_flush = client._flush

    # Stub the transport: capture everything that would go on the wire.
    sent = []

    def fake_flush(batch, timeout):
        sent.append("\n".join(batch) + "\n")
        return True

    client._flush = fake_flush
    client.connected = True
    client.client = object()
    client.loop = object()
    return client, sent


# ======================================================================
def test_protocol():
    section("protocol")
    check("REQUEST_SONGS split",
          protocol.split_command("REQUEST_SONGS|3|5") == ("REQUEST_SONGS", "3|5"))
    check("bare command split", protocol.split_command("PAUSE") == ("PAUSE", ""))
    check("song name split",
          protocol.split_command("PLAY|My Song") == ("PLAY", "My Song"))
    check("args parsed", protocol.parse_request_songs("3|5") == (3, 5))
    check("bad args rejected", protocol.parse_request_songs("bogus") is None)
    check("missing args rejected", protocol.parse_request_songs("3") is None)
    check("SONGS built", protocol.build_songs(["a", "b"]) == "SONGS|a|b")
    check("LYRICS_DATA built",
          protocol.build_lyrics_data("hi") == "LYRICS_DATA|hi")
    check("TX/RX differ", protocol.TX_UUID != protocol.RX_UUID)
    check("RX is a plain write (no write-nr)",
          "def012345680" in protocol.RX_UUID)


def test_request_songs(client, sent):
    section("REQUEST_SONGS")
    sent.clear()
    client._handle_request_songs("0|5")
    check("first window served",
          bool(sent) and sent[-1].startswith("SONGS|Closer|Long Name|Tumi"),
          repr(sent))

    sent.clear()
    client._handle_request_songs("99|5")
    check("out-of-range stays silent (empty SONGS would zero total_songs_)",
          not sent, repr(sent))

    sent.clear()
    client._handle_request_songs("bad|args")
    check("malformed stays silent", not sent, repr(sent))

    sent.clear()
    client._handle_request_songs("1|99")
    check("count clamped to the 5-row window",
          bool(sent) and sent[-1].count("|") - 1 <= 5, repr(sent))


def test_library_push(client, sent):
    section("library push")
    sent.clear()
    client._push_library()
    check("TOTAL_SONGS + SONGS in one atomic write",
          bool(sent) and sent[-1].count("\n") == 2
          and sent[-1].startswith("TOTAL_SONGS|3\nSONGS|"),
          repr(sent))


def test_lyrics(client, sent):
    section("lyrics")
    sent.clear()
    client._handle_lyrics("Tumi")
    lines = [l for l in "".join(sent).split("\n") if l]
    check("starts with LYRICS_DATA",
          bool(lines) and lines[0].startswith("LYRICS_DATA|"), repr(lines))
    check("ends with exactly one LYRICS_END",
          lines.count("LYRICS_END") == 1 and lines[-1] == "LYRICS_END",
          repr(lines))

    sent.clear()
    client._handle_lyrics("Nope")
    check("missing lyric file still answers LYRICS_END", sent == ["LYRICS_END\n"],
          repr(sent))


def test_mtu_packing(client, sent):
    section("MTU packing")
    sent.clear()
    lyrics = [f"LYRICS_DATA|<00:0{i // 10}.{i % 10:02d}>word{i}"
              for i in range(60)]
    client.send_lines(lyrics)
    check("60 lyric lines packed into few writes", len(sent) < 60,
          f"{len(sent)} writes")
    check("no lyric line lost while packing",
          sum(l.count("LYRICS_DATA|") for l in sent) == 60)
    check("rounds are newline-terminated",
          all(l.endswith("\n") for l in sent))


def test_adaptive_split(client, sent):
    section("adaptive MTU split")
    rejected = []

    def small_mtu_write(payload, timeout, quiet=False):
        if len(payload.encode()) > 100:
            rejected.append(len(payload))
            return False
        sent.append(payload)
        return True

    # The real _flush() holds the halving logic, so put it back and stub
    # the layer underneath it instead.
    client._flush = client._real_flush
    client._write = small_mtu_write

    sent.clear()
    rejected.clear()
    expected = [f"LYRICS_DATA|line-{i:03d} padding padding pad"
                for i in range(8)]
    client.send_lines(expected)

    delivered = [l for payload in sent for l in payload.rstrip("\n").split("\n")]
    check("every line survives a too-small MTU", delivered == expected,
          f"{len(delivered)}/{len(expected)}: {delivered}")
    check("order preserved across splits", delivered == expected)
    check("the big write was rejected then split", bool(rejected))

    sent.clear()
    rejected.clear()
    client.send_lines(["LYRICS_DATA|" + "x" * 300])
    check("an un-fittable single line is attempted exactly once",
          len(rejected) == 1 and not sent,
          f"rejected={len(rejected)} sent={len(sent)}")

    # Hand the stubs back for the remaining tests.
    del client._write
    client._flush = lambda batch, timeout: (
        sent.append("\n".join(batch) + "\n"), True)[1]


def test_dispatch(client):
    section("dispatch")
    seen = []
    client._spawn = staticmethod(lambda target, *args: seen.append(target.__name__))

    for command in ["PLAY|Tumi", "REQUEST_SONGS|0|5", "LYRICS|Tumi",
                    "PAUSE", "RESUME", "STOP", "END",
                    "TOTAL_SONGS|3",       # PC-only, must be ignored
                    "SONGS|a|b",           # PC-only, must be ignored
                    "GARBAGE"]:
        client._dispatch(command)

    check("PC-only commands are not re-handled",
          seen == ["_handle_play", "_handle_request_songs", "_handle_lyrics",
                   "_handle_pause", "_handle_resume", "_handle_stop",
                   "_handle_end"],
          repr(seen))


def test_guards(client, sent):
    section("connection guards")
    client.connected = False
    sent.clear()
    check("send_lines refuses while disconnected",
          client.send_lines(["PLAY|x"]) is False and not sent, repr(sent))
    client.connected = True

    check("PLAY with no name is dropped",
          client._dispatch("PLAY|") is None)


def test_file_resolution(client):
    section("library resolution")
    songs = client._songs()
    check("lyric files form the library",
          songs == ["Closer", "Long Name", "Tumi"], repr(songs))
    check("audio resolved by stem",
          (client._find_audio_file("Tumi") or "").endswith("Tumi.mp3"))
    check("audio name with spaces resolved",
          (client._find_audio_file("Long Name") or "").endswith("Long Name.flac"))
    check("unknown song has no audio", client._find_audio_file("Nope") is None)
    check("lyric file resolved",
          (client._find_lyrics_file("Tumi") or "").endswith("Tumi.txt"))
    check("unknown song has no lyric file",
          client._find_lyrics_file("Nope") is None)


def test_no_side_effects():
    """The shipped app must never create directories on the user's disk.

    (``selftest.py`` still uses ``tempfile.mkdtemp`` for its own fixture —
    that is a system temp directory, not a library folder.)
    """
    import os

    from . import gui

    section("filesystem side effects")

    check("default media folder is ~/Music/SongWithLyrics",
          gui.DEFAULT_FOLDER == os.path.expanduser("~/Music/SongWithLyrics"),
          gui.DEFAULT_FOLDER)

    here = os.path.dirname(os.path.abspath(__file__))
    needles = ("os.makedirs", "os.mkdir", ".mkdir(")
    offenders = []
    for name in sorted(os.listdir(here)):
        if not name.endswith(".py") or name == os.path.basename(__file__):
            continue  # this module holds the needles themselves
        with open(os.path.join(here, name), encoding="utf-8") as handle:
            source = handle.read()
        for call in needles:
            if call in source:
                offenders.append(f"{name}: {call}")
    check("no pc_client module creates a directory",
          not offenders, "found " + ", ".join(offenders))


# ======================================================================
def make_tmp_media():
    import os
    import tempfile

    root = tempfile.mkdtemp(prefix="boka_selftest_")
    files = {
        "Tumi.txt": "[Verse] <00:01.00>First <00:01.50>word\n"
                    "<00:02.00>Second <00:02.40>line\n",
        "Closer.txt": "<00:00.50>Hello <00:01.00>world\n",
        "Long Name.txt": "<00:00.10>Space <00:00.50>name\n",
        "Tumi.mp3": "not-really-audio",
        "Long Name.flac": "x",
    }
    for name, body in files.items():
        with open(os.path.join(root, name), "w", encoding="utf-8") as handle:
            handle.write(body)
    return root


def main():
    print("BokaBaksho companion self-test")
    print(MEDIA_HINT)

    media = make_tmp_media()
    client, sent = build_client(media)

    test_protocol()
    test_file_resolution(client)
    test_request_songs(client, sent)
    test_library_push(client, sent)
    test_lyrics(client, sent)
    test_mtu_packing(client, sent)
    test_adaptive_split(client, sent)
    test_dispatch(client)
    test_guards(client, sent)
    test_no_side_effects()

    try:
        client.player.shutdown()
    except Exception:
        pass

    print(f"\n{_checks - len(_failures)}/{_checks} checks passed")
    if _failures:
        print("FAILED: " + ", ".join(_failures))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
