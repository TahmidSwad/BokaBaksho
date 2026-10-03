#!/usr/bin/env bash
#
# Build the Boka_Baksho PC companion into a standalone executable.
#
#   ./build_client.sh
#
# Output: dist/BokaBakshoCompanion
#
set -euo pipefail
cd "$(dirname "$0")"

PYTHON=".venv/bin/python"
if [ ! -x "$PYTHON" ]; then
    PYTHON="python3"
fi

echo "==> Using $($PYTHON --version)"
echo "==> Installing dependencies"
"$PYTHON" -m pip install --quiet --upgrade pip
"$PYTHON" -m pip install --quiet -r requirements.txt

echo "==> Building"
"$PYTHON" -m PyInstaller \
    --noconfirm \
    --clean \
    --onefile \
    --windowed \
    --name BokaBakshoCompanion \
    --paths . \
    run_companion.py

echo
echo "==> Done: $(pwd)/dist/BokaBakshoCompanion"
