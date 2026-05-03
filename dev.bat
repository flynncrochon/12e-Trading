@echo off
REM 12e Trading dev launcher.
REM Uses absolute paths so it works even when PATH is stale (cmd sessions
REM opened before pnpm was installed) or pnpm is not yet on PATH.

setlocal
set "PNPM=%APPDATA%\npm\pnpm.cmd"
if not exist "%PNPM%" (
    echo ERROR: pnpm not found at "%PNPM%".
    echo Install with:  npm install -g pnpm
    pause
    exit /b 1
)

set "REPO_ROOT=%~dp0"
cd /d "%REPO_ROOT%"

echo Starting 12e Trading...
echo (close this window OR press Ctrl-C to stop the app)
echo.
"%PNPM%" dev

echo.
echo --- pnpm dev exited with code %ERRORLEVEL% ---
echo If the Electron window closed, scroll up to see why.
pause
