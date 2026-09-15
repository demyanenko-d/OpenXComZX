@echo off
rem OpenXComZX: start the emulator with tmp\build\oxz.spg (tools\run.ps1).
rem   run.cmd                  - game window only, no console (Win+G recording works)
rem   run.cmd -Script tests\campaign.oxs -Headless   - any run.ps1 arguments, with console
rem Mouse: left click in the window captures it, Shift+Esc releases.
cd /d "%~dp0"
if not exist "tmp\build\oxz.spg" (
  echo tmp\build\oxz.spg not found - build first: powershell -File tools\build.ps1
  pause
  exit /b 1
)
if "%~1"=="" (
  start "" /min powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0tools\run.ps1"
  exit /b 0
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run.ps1" %*
