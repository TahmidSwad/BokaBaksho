"""Audio playback backed by ``pygame.mixer``.

``pygame-ce`` is used on Python 3.14 (upstream ``pygame`` has no cp314
wheel); both provide the same ``pygame`` module.

Thread-safety: every public method is called from the BLE worker threads
under the client's ``command_lock``. The GUI never touches this object
directly — it only reads ``state``.
"""

import time

import pygame

IDLE = "IDLE"
LOADED = "LOADED"
PLAYING = "PLAYING"
PAUSED = "PAUSED"
STOPPED = "STOPPED"

STATES = (IDLE, LOADED, PLAYING, PAUSED, STOPPED)


class AudioPlayer:
    def __init__(self, log):
        self._log = log
        self.state = IDLE
        self.current_file = None
        self.loaded_file = None
        self.started_at = None
        self.available = False
        try:
            pygame.mixer.init()
            self.available = True
        except Exception as exc:  # no sound device / no driver
            self._log(f"Audio unavailable: {exc}")

    # ------------------------------------------------------------------
    def load(self, filepath):
        if not self.available:
            return False
        try:
            pygame.mixer.music.load(filepath)
        except Exception as exc:
            self._log(f"Load failed: {exc}")
            self.loaded_file = None
            self.state = IDLE
            return False
        self.loaded_file = filepath
        self.current_file = None
        self.started_at = None
        self.state = LOADED
        return True

    def play_loaded(self):
        if not self.available or not self.loaded_file:
            return False
        try:
            pygame.mixer.music.play()
        except Exception as exc:
            self._log(f"Play failed: {exc}")
            return False
        self.current_file = self.loaded_file
        self.started_at = time.monotonic()
        self.state = PLAYING
        return True

    def play(self, filepath):
        return self.load(filepath) and self.play_loaded()

    def pause(self):
        if not self.available or self.state != PLAYING:
            return False
        pygame.mixer.music.pause()
        self.state = PAUSED
        return True

    def resume(self):
        if not self.available or self.state != PAUSED:
            return False
        pygame.mixer.music.unpause()
        self.state = PLAYING
        self.started_at = time.monotonic()
        return True

    def stop(self):
        if self.available:
            try:
                pygame.mixer.music.stop()
            except Exception as exc:
                self._log(f"Stop failed: {exc}")
        self.current_file = None
        self.started_at = None
        self.loaded_file = None
        self.state = STOPPED
        return True

    # ------------------------------------------------------------------
    @property
    def is_playing(self):
        return self.state == PLAYING

    @property
    def busy(self):
        """True while the mixer is actually pulling samples."""
        if not self.available or self.state not in (PLAYING, PAUSED):
            return False
        try:
            return bool(pygame.mixer.music.get_busy())
        except Exception:
            return False

    @property
    def elapsed(self):
        if self.started_at is None:
            return 0.0
        if self.state == PAUSED:
            return 0.0
        return time.monotonic() - self.started_at

    def shutdown(self):
        if self.available:
            try:
                pygame.mixer.music.stop()
                pygame.mixer.quit()
            except Exception:
                pass
        self.state = IDLE
