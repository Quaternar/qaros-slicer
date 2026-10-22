@echo off
setlocal
cd /d "%~dp0"
echo The standalone Slicer renderer has been retired. Installing the direct Live plugin.
call "%~dp0UPDATE-LIVE-PLUGIN.cmd" %*
exit /b %ERRORLEVEL%
