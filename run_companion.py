#!/usr/bin/env python3
"""Entry point for the Boka_Baksho PC companion.

Run directly (``python run_companion.py``) or let PyInstaller turn this
file into a standalone executable (``./build_client.sh``).
"""

from pc_client.main import run

if __name__ == "__main__":
    raise SystemExit(run())
