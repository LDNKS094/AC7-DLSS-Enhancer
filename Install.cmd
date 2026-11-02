@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install.ps1" %*
set "AC7_INSTALL_RESULT=%ERRORLEVEL%"
if not "%AC7_INSTALL_RESULT%"=="0" echo Install failed with exit code %AC7_INSTALL_RESULT%. See the message above.
pause
exit /b %AC7_INSTALL_RESULT%
