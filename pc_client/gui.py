"""Minimal Tk front end for the BokaBaksho companion.

Everything the user can do here is something the firmware cannot do on its
own: pick the media folder, open the BLE link, and watch what happened.

All state changes arrive from the BLE thread through a queue and are
applied on the Tk thread by ``_poll_events``.
"""

import os
import queue
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import audio, protocol

POLL_MS = 100
# The default media library. It is only ever read — the app never creates it.
DEFAULT_FOLDER = os.path.expanduser("~/Music/SongWithLyrics")

GREEN = "#2e9e5b"
RED = "#d0492f"
GREY = "#8a8f98"


class CompanionApp:
    def __init__(self, root, client, player, events):
        self.root = root
        self.client = client
        self.player = player
        self.events = events

        self.connected = False
        self.playing_song = ""
        self.media_dir = DEFAULT_FOLDER

        self._build()
        self._set_media_dir(DEFAULT_FOLDER, quiet=True)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._poll_events()

    # ==================================================================
    # Layout
    # ==================================================================

    def _build(self):
        self.root.title("BokaBaksho Companion")
        self.root.geometry("780x540")
        self.root.minsize(660, 460)

        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Status.TLabel", font=("TkDefaultFont", 11, "bold"))
        style.configure("Muted.TLabel", foreground=GREY)

        main = ttk.Frame(self.root, padding=12)
        main.pack(fill="both", expand=True)
        main.columnconfigure(0, weight=1)
        main.rowconfigure(3, weight=3)
        main.rowconfigure(4, weight=2)

        # --- header -------------------------------------------------
        header = ttk.Frame(main)
        header.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        header.columnconfigure(0, weight=1)

        ttk.Label(
            header, text="BokaBaksho Companion", font=("TkDefaultFont", 15, "bold")
        ).grid(row=0, column=0, sticky="w")

        self.status = ttk.Label(
            header, text="● Disconnected", style="Status.TLabel", foreground=RED
        )
        self.status.grid(row=0, column=1, sticky="e")

        # --- connection + folder -----------------------------------
        bar = ttk.Frame(main)
        bar.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        bar.columnconfigure(3, weight=1)

        self.connect_btn = ttk.Button(bar, text="Connect", command=self._connect)
        self.connect_btn.grid(row=0, column=0, sticky="w")

        self.disconnect_btn = ttk.Button(
            bar, text="Disconnect", command=self._disconnect, state="disabled"
        )
        self.disconnect_btn.grid(row=0, column=1, padx=(6, 18))

        ttk.Label(bar, text="Folder").grid(row=0, column=2, sticky="w")

        self.folder_var = tk.StringVar()
        folder_entry = ttk.Entry(bar, textvariable=self.folder_var)
        folder_entry.grid(row=0, column=3, sticky="ew", padx=(6, 6))

        ttk.Button(bar, text="Browse…", command=self._browse).grid(
            row=0, column=4, padx=(0, 4)
        )
        ttk.Button(bar, text="Refresh", command=self._refresh).grid(
            row=0, column=5
        )

        # --- songs --------------------------------------------------
        songs_box = ttk.LabelFrame(main, text="Songs", padding=8)
        songs_box.grid(row=3, column=0, sticky="nsew", padx=(0, 0), pady=(0, 8))
        songs_box.rowconfigure(0, weight=1)
        songs_box.columnconfigure(0, weight=1)

        self.song_list = tk.Listbox(
            songs_box,
            activestyle="none",
            exportselection=False,
            highlightthickness=0,
            borderwidth=1,
            font=("TkDefaultFont", 11),
        )
        self.song_list.grid(row=0, column=0, sticky="nsew")

        scrollbar = ttk.Scrollbar(
            songs_box, orient="vertical", command=self.song_list.yview
        )
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.song_list.configure(yscrollcommand=scrollbar.set)

        self.songs_count = ttk.Label(songs_box, text="0 songs", style="Muted.TLabel")
        self.songs_count.grid(row=1, column=0, sticky="w", pady=(6, 0))

        # --- now playing -------------------------------------------
        now_box = ttk.LabelFrame(main, text="Now playing", padding=10)
        now_box.grid(row=3, column=1, sticky="new", padx=(8, 0), pady=(0, 8))
        now_box.columnconfigure(0, weight=1)

        self.song_label = ttk.Label(
            now_box, text="—", font=("TkDefaultFont", 13, "bold"), wraplength=220
        )
        self.song_label.grid(row=0, column=0, sticky="w")

        self.state_label = ttk.Label(now_box, text="Stopped")
        self.state_label.grid(row=1, column=0, sticky="w", pady=(2, 12))

        ttk.Separator(now_box).grid(row=2, column=0, sticky="ew", pady=(0, 8))

        ttk.Label(now_box, text="Device", style="Muted.TLabel").grid(
            row=3, column=0, sticky="w"
        )
        ttk.Label(now_box, text=protocol.DEVICE_NAME).grid(
            row=4, column=0, sticky="w"
        )

        self.address_label = ttk.Label(now_box, text="—", style="Muted.TLabel")
        self.address_label.grid(row=5, column=0, sticky="w", pady=(6, 0))

        # --- log ----------------------------------------------------
        log_box = ttk.LabelFrame(main, text="Log", padding=8)
        log_box.grid(row=4, column=0, sticky="nsew")
        log_box.columnconfigure(0, weight=1)
        log_box.rowconfigure(1, weight=1)

        ttk.Button(log_box, text="Clear", command=self._clear_log).grid(
            row=0, column=1, sticky="ne"
        )

        self.log_text = tk.Text(
            log_box,
            height=8,
            state="disabled",
            wrap="none",
            font=("TkFixedFont", 9),
            highlightthickness=0,
            borderwidth=1,
        )
        self.log_text.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=(4, 0))

        log_scroll = ttk.Scrollbar(
            log_box, orient="vertical", command=self.log_text.yview
        )
        log_scroll.grid(row=1, column=2, sticky="ns", pady=(4, 0))
        self.log_text.configure(yscrollcommand=log_scroll.set)

    # ==================================================================
    # Connection
    # ==================================================================

    def _connect(self):
        self.connect_btn.config(state="disabled")
        self.log("Connecting…")
        self.client.connect()

    def _disconnect(self):
        self.client.disconnect()

    def _set_connected(self, connected):
        self.connected = connected
        if connected:
            self.status.config(text="● Connected", foreground=GREEN)
            self.connect_btn.config(state="disabled")
            self.disconnect_btn.config(state="normal")
        else:
            self.status.config(text="● Disconnected", foreground=RED)
            self.connect_btn.config(state="normal")
            self.disconnect_btn.config(state="disabled")
            self.address_label.config(text="—")
            self._set_playing(False)

    # ==================================================================
    # Media folder
    # ==================================================================

    def _browse(self):
        chosen = filedialog.askdirectory(initialdir=self.media_dir or "~")
        if chosen:
            self._set_media_dir(chosen)

    def _refresh(self):
        self._load_song_list()
        self.client.refresh_library()
        self.log("Library refreshed.")

    def _set_media_dir(self, path, quiet=False):
        path = os.path.abspath(os.path.expanduser(path))
        # Deliberately never created: an empty folder would silently produce
        # an empty library on the device, which looks like a BLE fault.
        # A missing folder is reported by _load_song_list() instead.
        self.media_dir = path
        self.folder_var.set(path)
        self.client.set_media_dir(path)
        self._load_song_list()
        if not quiet:
            self.log(f"Folder: {path}")

    def _load_song_list(self):
        self.song_list.delete(0, tk.END)

        if not os.path.isdir(self.media_dir):
            self.songs_count.config(text="folder not found")
            self.log(f"Folder not found: {self.media_dir}")
            return

        try:
            entries = os.listdir(self.media_dir)
        except OSError as exc:
            self.songs_count.config(text="unreadable")
            self.log(f"Cannot read folder: {exc}")
            return

        names = sorted(
            (
                os.path.splitext(entry)[0]
                for entry in entries
                if entry.lower().endswith(protocol.LYRIC_EXTENSION)
            ),
            key=str.lower,
        )
        for name in names:
            self.song_list.insert(tk.END, name)

        self.songs_count.config(
            text=f"{len(names)} song{'s' if len(names) != 1 else ''}"
        )
        self._highlight_song(self.playing_song)

    # ==================================================================
    # Now playing
    # ==================================================================

    def _set_song(self, name):
        self.playing_song = name or ""
        self.song_label.config(text=self.playing_song or "—")
        self._highlight_song(self.playing_song)

    def _set_playing(self, playing):
        if playing:
            self.state_label.config(text="Playing")
        elif self.player.state == audio.PAUSED:
            self.state_label.config(text="Paused")
        else:
            self.state_label.config(text="Stopped")
            self._set_song("")

    def _highlight_song(self, name):
        self.song_list.selection_clear(0, tk.END)
        if not name:
            return
        for index in range(self.song_list.size()):
            if self.song_list.get(index) == name:
                self.song_list.selection_set(index)
                self.song_list.see(index)
                return

    # ==================================================================
    # Event pump
    # ==================================================================

    def _poll_events(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "log":
                    self.log(value)
                elif kind == "connection":
                    self._set_connected(value)
                elif kind == "song":
                    self._set_song(value)
                elif kind == "playing":
                    self._set_playing(value)
                elif kind == "device":
                    self.address_label.config(text=value or "—")
        except queue.Empty:
            pass
        self.root.after(POLL_MS, self._poll_events)

    # ==================================================================
    # Log
    # ==================================================================

    def log(self, message):
        stamp = time.strftime("%H:%M:%S")
        self.log_text.config(state="normal")
        self.log_text.insert(tk.END, f"[{stamp}] {message}\n")
        self.log_text.see(tk.END)
        self.log_text.config(state="disabled")

    def _clear_log(self):
        self.log_text.config(state="normal")
        self.log_text.delete("1.0", tk.END)
        self.log_text.config(state="disabled")

    # ==================================================================
    def _on_close(self):
        self.log("Shutting down…")
        try:
            self.client.shutdown()
        except Exception:
            pass
        try:
            self.player.shutdown()
        except Exception:
            pass
        self.root.destroy()


def run(client, player, events):
    root = tk.Tk()
    try:
        CompanionApp(root, client, player, events)
    except Exception as exc:
        messagebox.showerror("Startup error", str(exc))
        raise
    root.mainloop()
