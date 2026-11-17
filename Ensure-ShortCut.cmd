@echo off
rem ============================================================
rem  Cavalry_ShortCut watchdog
rem  Checks if Cavalry_ShortCut.exe is running; starts it if not.
rem  Usage: double-click, or run from Task Scheduler.
rem ============================================================
setlocal
set EXE=%~dp0Cavalry_ShortCut.exe

if not exist "%EXE%" (
  echo [ERROR] not found: %EXE%
  pause
  exit /b 1
)

tasklist /FI "IMAGENAME eq Cavalry_ShortCut.exe" 2>nul | find /I "Cavalry_ShortCut.exe" >nul
if not errorlevel 1 (
  echo [OK] already running.
  exit /b 0
)

echo [INFO] not running, starting...
start "" "%EXE%"
ping -n 3 127.0.0.1 >nul

tasklist /FI "IMAGENAME eq Cavalry_ShortCut.exe" 2>nul | find /I "Cavalry_ShortCut.exe" >nul
if errorlevel 1 (
  echo [WARN] still not detected. Run Cavalry_ShortCut.exe manually to see the error.
  exit /b 2
) else (
  echo [OK] started.
  exit /b 0
)