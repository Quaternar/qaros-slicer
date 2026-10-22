@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\Update-Live-Plugin.ps1" %*
set "QAR_EXIT=%ERRORLEVEL%"
if not "%QAR_EXIT%"=="0" echo Update failed. See the error above.
pause
exit /b %QAR_EXIT%
