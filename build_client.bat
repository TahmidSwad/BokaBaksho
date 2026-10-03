@echo off
rem Build the Boka_Baksho PC companion into a standalone executable.
rem
rem   build_client.bat
rem
rem Output: dist\BokaBakshoCompanion.exe
rem
setlocal
cd /d "%~dp0"

if not exist .venv (
    echo ==^> Creating virtual environment
    py -3 -m venv .venv || python -m venv .venv
)

echo ==^> Installing dependencies
.venv\Scripts\python -m pip install --quiet --upgrade pip
.venv\Scripts\python -m pip install --quiet -r requirements.txt
if errorlevel 1 goto :error

echo ==^> Building
.venv\Scripts\python -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onefile ^
    --windowed ^
    --name BokaBakshoCompanion ^
    --paths . ^
    run_companion.py
if errorlevel 1 goto :error

echo.
echo ==^> Done: %CD%\dist\BokaBakshoCompanion.exe
exit /b 0

:error
echo Build failed.
exit /b 1
