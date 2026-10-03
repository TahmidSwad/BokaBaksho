"""BLE protocol constants and command parsing.

Mirrors the firmware side defined in ``src/services/ble_service.h``.
Full specification: docs/ble-protocol.md
"""

DEVICE_NAME = "BokaBaksho"

SERVICE_UUID = "12345678-1234-5678-9abc-def012345678"
TX_UUID = "12345678-1234-5678-9abc-def012345679"   # ESP32 -> PC  (notify)
RX_UUID = "12345678-1234-5678-9abc-def012345680"   # PC -> ESP32  (write)

#: The firmware keeps a 5-row scroll window (``LyricsApp::kWindowSize``).
WINDOW_SIZE = 5

#: One lyric line is prefixed with this before being sent.
LYRICS_DATA_PREFIX = "LYRICS_DATA|"

#: Extensions probed when resolving a song name to an audio file.
AUDIO_EXTENSIONS = (".mp3", ".wav", ".ogg", ".m4a", ".flac")

#: The song library is the set of lyric files in the media folder.
LYRIC_EXTENSION = ".txt"

#: ``rx_buffer_[256]`` on the ESP32 — keep any single line well under this.
MAX_LINE_BYTES = 200


def split_command(line):
    """Split ``NAME|rest`` into ``(name_upper, rest)``.

    A line without a separator yields ``(NAME_UPPER, "")``.
    """
    name, sep, rest = line.partition("|")
    return name.strip().upper(), (rest if sep else "")


def parse_request_songs(rest):
    """Parse ``<offset>|<count>`` → ``(offset, count)`` or ``None``."""
    parts = rest.split("|")
    if len(parts) < 2:
        return None
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        return None


def song_name_from(command):
    """Extract the song name from ``PLAY|name`` / ``LYRICS|name``."""
    _, _, rest = command.partition("|")
    return rest.strip()


def build_total_songs(count):
    return f"TOTAL_SONGS|{count}"


def build_songs(names):
    return "SONGS|" + "|".join(names)


def build_lyrics_data(line):
    return LYRICS_DATA_PREFIX + line
