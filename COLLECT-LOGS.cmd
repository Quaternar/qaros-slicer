@echo off
setlocal
cd /d "%~dp0"
echo Collecting QAROS VTK sender logs and related Windows crash reports...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\Collect-Logs.ps1"
if errorlevel 1 (
  echo Log collection failed. Copy the error above or attach sender.log directly.
  pause
  exit /b 1
)
echo Attach the newest QAROS-VTK-diagnostics ZIP from the diagnostics folder to the chat.
pause
