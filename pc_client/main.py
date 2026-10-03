"""Application wiring: construct the player, BLE client and GUI."""

import queue
import sys
import traceback


def run():
    """Build the three long-lived objects and enter the Tk main loop."""
    events = queue.Queue()

    try:
        from . import audio, ble, gui
    except ImportError as exc:
        return _report_missing(exc)

    log = lambda message: events.put(("log", message))  # noqa: E731

    player = audio.AudioPlayer(log)
    client = ble.BLEAudioClient(player, events, gui.DEFAULT_FOLDER)
    client.start()

    try:
        gui.run(client, player, events)
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        try:
            player.shutdown()
        except Exception:
            pass
    return 0


def _report_missing(exc):
    """Show a message box if Tk is available, otherwise print to stderr."""
    message = (
        f"Missing dependency: {exc}\n\n"
        "Install the requirements:\n"
        "    pip install -r requirements.txt"
    )
    try:
        import tkinter as tk
        from tkinter import messagebox

        root = tk.Tk()
        root.withdraw()
        messagebox.showerror("BokaBaksho Companion", message)
        root.destroy()
    except Exception:
        print(message, file=sys.stderr)
        traceback.print_exc()
    return 1


if __name__ == "__main__":
    raise SystemExit(run())
